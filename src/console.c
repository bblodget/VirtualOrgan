/*
 * VirtualOrgan — virtual pipe organ engine for Linux
 * Copyright (C) 2026 Brandon Blodget
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include "console.h"
#include "mixer.h"

static pthread_t console_thread;
static volatile int running;
static volatile int quit_req;
static RingBuffer *ring_buf;
static OrganConfig *organ_config;
static const char *config_path;
static int active_division;

static struct termios orig_termios;
static int termios_saved;

static void restore_terminal(void)
{
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static int set_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
        return -1;
    termios_saved = 1;

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  /* no echo, no line buffering */
    raw.c_cc[VMIN] = 0;               /* non-blocking */
    raw.c_cc[VTIME] = 1;              /* 100ms timeout */

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;

    return 0;
}

/* Map character to stop index (0-6) */
static int char_to_stop(char c)
{
    switch (c) {
    case 'z': case 'Z': return 0;
    case 'x': case 'X': return 1;
    case 'c': case 'C': return 2;
    case 'v': case 'V': return 3;
    case 'b': case 'B': return 4;
    case 'n': case 'N': return 5;
    case 'm': case 'M': return 6;
    default: return -1;
    }
}

static void print_help(void)
{
    printf("\nConsole controls:\n");

    if (organ_config->num_divisions > 0) {
        const DivisionConfig *dc = &organ_config->divisions[active_division];
        printf("  Division: %s (Tab to cycle)\n", dc->name);
        printf("  Stops:  ");
        const char *stop_keys[] = {"Z", "X", "C", "V", "B", "N", "M"};
        for (int s = 0; s < dc->num_stops && s < 7; s++)
            printf("%s=%s%s ", stop_keys[s], dc->stops[s].name,
                   dc->stops[s].engaged ? "*" : "");
        printf("\n");
    }

    if (organ_config->num_couplers > 0) {
        printf("  ` = toggle coupler(s):");
        for (int c = 0; c < organ_config->num_couplers; c++)
            printf(" %s(%s->%s)%s",
                   organ_config->couplers[c].name,
                   organ_config->divisions[organ_config->couplers[c].from_division].name,
                   organ_config->divisions[organ_config->couplers[c].to_division].name,
                   organ_config->couplers[c].engaged ? "*" : "");
        printf("\n");
    }

    printf("  [/] = division volume  -/= = master gain  Space = all stops off\n");
    printf("  R = reload config  H = help  Q = quit\n\n");
}

static void do_reload(void)
{
    if (!config_path) {
        printf("reload: no config path\n");
        return;
    }
    if (config_reload(organ_config, config_path) == 0) {
        if (active_division >= organ_config->num_divisions)
            active_division = 0;
        printf("Config reloaded from %s\n", config_path);
        print_help();
    } else {
        printf("reload: failed\n");
    }
}

static void *console_thread_fn(void *arg)
{
    (void)arg;

    if (set_raw_mode() < 0) {
        fprintf(stderr, "console: cannot set raw terminal mode\n");
        return NULL;
    }

    print_help();

    while (running) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0)
            continue;

        /* Quit */
        if (c == 'q' || c == 'Q' || c == 27) {  /* 27 = Esc */
            quit_req = 1;
            running = 0;
            break;
        }

        /* Help */
        if (c == 'h' || c == 'H') {
            print_help();
            continue;
        }

        /* Reload config */
        if (c == 'r' || c == 'R') {
            do_reload();
            continue;
        }

        /* Tab = cycle division */
        if (c == '\t' && organ_config->num_divisions > 0) {
            active_division = (active_division + 1) % organ_config->num_divisions;
            printf("Division: %s\n", organ_config->divisions[active_division].name);
            print_help();
            continue;
        }

        /* Master gain controls */
        if (c == '-') {
            mixer_set_gain(mixer_get_gain() * 0.7f);
            printf("Master gain: %.3f\n", mixer_get_gain());
            continue;
        }
        if (c == '=') {
            mixer_set_gain(mixer_get_gain() * 1.4f);
            printf("Master gain: %.3f\n", mixer_get_gain());
            continue;
        }

        /* Division expression volume: [ down, ] up */
        if ((c == '[' || c == ']') && organ_config->num_divisions > 0) {
            DivisionConfig *dc = &organ_config->divisions[active_division];
            if (c == '[')
                dc->expression_gain = dc->expression_gain * 0.7f;
            else
                dc->expression_gain = dc->expression_gain * 1.4f;
            if (dc->expression_gain < 0.0f) dc->expression_gain = 0.0f;
            if (dc->expression_gain > 1.0f) dc->expression_gain = 1.0f;
            printf("%s volume: %.0f%%\n", dc->name, dc->expression_gain * 100.0f);
            continue;
        }

        /* Backtick = toggle coupler(s) */
        if (c == '`' && organ_config->num_couplers > 0) {
            for (int i = 0; i < organ_config->num_couplers; i++) {
                CouplerConfig *coup = &organ_config->couplers[i];
                bool new_state = !coup->engaged;
                uint8_t cc_val = new_state ? 127 : 0;
                MidiEvent cc = {MIDI_CC, 1, (uint8_t)coup->engage_cc, cc_val};
                ring_buffer_push(ring_buf, &cc);
                printf("Coupler %s: %s\n", coup->name, new_state ? "ON" : "OFF");
            }
            continue;
        }

        /* Space = all stops off in active division */
        if (c == ' ' && organ_config->num_divisions > 0) {
            DivisionConfig *dc = &organ_config->divisions[active_division];
            int midi_ch = dc->midi_channel;
            for (int s = 0; s < dc->num_stops; s++) {
                MidiEvent cc = {MIDI_CC, (uint8_t)midi_ch,
                                (uint8_t)dc->stops[s].engage_cc, 0};
                ring_buffer_push(ring_buf, &cc);
            }
            printf("All stops OFF (%s)\n", dc->name);
            continue;
        }

        /* Stop toggle keys */
        if (organ_config->num_divisions > 0) {
            int stop_idx = char_to_stop(c);
            DivisionConfig *dc = &organ_config->divisions[active_division];
            if (stop_idx >= 0 && stop_idx < dc->num_stops) {
                StopConfig *stop = &dc->stops[stop_idx];
                bool new_state = !stop->engaged;
                uint8_t cc_val = new_state ? 127 : 0;
                int midi_ch = dc->midi_channel;
                MidiEvent cc = {MIDI_CC, (uint8_t)midi_ch,
                                (uint8_t)stop->engage_cc, cc_val};
                ring_buffer_push(ring_buf, &cc);
                printf("Stop %s: %s\n", stop->name, new_state ? "ON" : "OFF");
            }
        }
    }

    restore_terminal();
    return NULL;
}

int console_start(RingBuffer *rb, OrganConfig *config, const char *path)
{
    ring_buf = rb;
    organ_config = config;
    config_path = path;
    running = 1;
    quit_req = 0;
    active_division = 0;

    return pthread_create(&console_thread, NULL, console_thread_fn, NULL);
}

void console_stop(void)
{
    running = 0;
    pthread_join(console_thread, NULL);
    restore_terminal();
}

bool console_quit_requested(void)
{
    return quit_req;
}
