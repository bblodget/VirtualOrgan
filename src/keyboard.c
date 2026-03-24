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
#include <pthread.h>
#include <SDL2/SDL.h>
#include "keyboard.h"
#include "mixer.h"

/* QWERTY piano key mapping: scancode → semitone offset from C.
 * -1 means not a note key. */
static int scancode_to_semitone(SDL_Scancode sc)
{
    switch (sc) {
    /* White keys */
    case SDL_SCANCODE_Q: return 0;   /* C  */
    case SDL_SCANCODE_W: return 2;   /* D  */
    case SDL_SCANCODE_E: return 4;   /* E  */
    case SDL_SCANCODE_R: return 5;   /* F  */
    case SDL_SCANCODE_T: return 7;   /* G  */
    case SDL_SCANCODE_Y: return 9;   /* A  */
    case SDL_SCANCODE_U: return 11;  /* B  */
    case SDL_SCANCODE_I: return 12;  /* C+1 */
    case SDL_SCANCODE_O: return 14;  /* D+1 */
    case SDL_SCANCODE_P: return 16;  /* E+1 */
    /* Black keys (number row) */
    case SDL_SCANCODE_2: return 1;   /* C# */
    case SDL_SCANCODE_3: return 3;   /* D# */
    case SDL_SCANCODE_5: return 6;   /* F# */
    case SDL_SCANCODE_6: return 8;   /* G# */
    case SDL_SCANCODE_7: return 10;  /* A# */
    case SDL_SCANCODE_9: return 13;  /* C#+1 */
    case SDL_SCANCODE_0: return 15;  /* D#+1 */
    default: return -1;
    }
}

/* Stop toggle keys: Z=0, X=1, C=2, V=3, B=4, N=5, M=6 */
static int scancode_to_stop(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_Z: return 0;
    case SDL_SCANCODE_X: return 1;
    case SDL_SCANCODE_C: return 2;
    case SDL_SCANCODE_V: return 3;
    case SDL_SCANCODE_B: return 4;
    case SDL_SCANCODE_N: return 5;
    case SDL_SCANCODE_M: return 6;
    default: return -1;
    }
}

static const char *note_names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static pthread_t kbd_thread;
static volatile int running;
static volatile int quit_requested;
static RingBuffer *ring_buf;
static OrganConfig *organ_config;
static const char *config_path;
static int active_division;  /* index into organ_config->divisions[] */

static void print_help(void)
{
    printf("\nKeyboard mode (SDL2) — focus the SDL window to play\n");
    printf("  Notes:  Q W E R T Y U I O P  (white keys)\n");
    printf("          2 3   5 6 7   9 0    (black keys)\n");
    printf("  Octave: [ down  ] up\n");

    if (organ_config->num_divisions > 0) {
        printf("  Tab = cycle division  (current: %s)\n",
               organ_config->divisions[active_division].name);
        printf("  Stops:  ");
        const char *stop_keys[] = {"Z", "X", "C", "V", "B", "N", "M"};
        const DivisionConfig *dc = &organ_config->divisions[active_division];
        for (int s = 0; s < dc->num_stops && s < 7; s++)
            printf("%s=%s ", stop_keys[s], dc->stops[s].name);
        printf("\n");
    } else {
        printf("  (no divisions configured — all ranks always play)\n");
    }

    if (organ_config->num_couplers > 0) {
        printf("  ` = toggle coupler(s):");
        for (int c = 0; c < organ_config->num_couplers; c++)
            printf(" %s(%s→%s)", organ_config->couplers[c].name,
                   organ_config->divisions[organ_config->couplers[c].from_division].name,
                   organ_config->divisions[organ_config->couplers[c].to_division].name);
        printf("\n");
    }
    printf("  -/= = gain down/up  Space = all stops off\n");
    printf("  Shift+R = reload config  H = help  Esc = quit\n\n");
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

static void *keyboard_thread(void *arg)
{
    (void)arg;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "keyboard: SDL_Init failed: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Window *win = SDL_CreateWindow("VirtualOrgan — Keyboard",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       400, 100,
                                       SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "keyboard: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    print_help();

    int octave = 4;  /* base octave: Q = C4 = MIDI note 60 */
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                quit_requested = 1;
                running = 0;
                break;
            }

            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.repeat)
                    continue;  /* ignore key repeat */

                SDL_Scancode sc = ev.key.keysym.scancode;

                /* Escape = quit */
                if (sc == SDL_SCANCODE_ESCAPE) {
                    quit_requested = 1;
                    running = 0;
                    break;
                }

                /* Backtick = toggle coupler(s) */
                if (sc == SDL_SCANCODE_GRAVE && organ_config->num_couplers > 0) {
                    for (int c = 0; c < organ_config->num_couplers; c++) {
                        const CouplerConfig *coup = &organ_config->couplers[c];
                        bool new_state = !coup->engaged;
                        uint8_t cc_val = new_state ? 127 : 0;
                        MidiEvent cc = {MIDI_CC, 1, (uint8_t)coup->engage_cc, cc_val};
                        ring_buffer_push(ring_buf, &cc);
                        printf("Coupler %s: %s\n", coup->name,
                               new_state ? "ON" : "OFF");
                    }
                    continue;
                }

                /* Tab = cycle division */
                if (sc == SDL_SCANCODE_TAB && organ_config->num_divisions > 0) {
                    active_division = (active_division + 1) % organ_config->num_divisions;
                    printf("Division: %s (ch %d)\n",
                           organ_config->divisions[active_division].name,
                           organ_config->divisions[active_division].midi_channel);
                    print_help();
                    continue;
                }

                /* Octave shift */
                if (sc == SDL_SCANCODE_LEFTBRACKET) {
                    if (octave > 1) octave--;
                    printf("Octave: %d\n", octave);
                    continue;
                }
                if (sc == SDL_SCANCODE_RIGHTBRACKET) {
                    if (octave < 7) octave++;
                    printf("Octave: %d\n", octave);
                    continue;
                }

                /* H = print help */
                if (sc == SDL_SCANCODE_H) {
                    print_help();
                    continue;
                }

                /* Shift+R = reload config */
                if (sc == SDL_SCANCODE_R && (ev.key.keysym.mod & KMOD_SHIFT)) {
                    do_reload();
                    continue;
                }

                /* Gain controls: - / = */
                if (sc == SDL_SCANCODE_MINUS) {
                    mixer_set_gain(mixer_get_gain() * 0.7f);
                    printf("Gain: %.3f\n", mixer_get_gain());
                    continue;
                }
                if (sc == SDL_SCANCODE_EQUALS) {
                    mixer_set_gain(mixer_get_gain() * 1.4f);
                    printf("Gain: %.3f\n", mixer_get_gain());
                    continue;
                }

                /* Get active division's MIDI channel (or 1 for legacy) */
                int midi_ch = 1;
                if (organ_config->num_divisions > 0)
                    midi_ch = organ_config->divisions[active_division].midi_channel;

                /* Spacebar = all stops off in active division */
                if (sc == SDL_SCANCODE_SPACE && organ_config->num_divisions > 0) {
                    const DivisionConfig *dc = &organ_config->divisions[active_division];
                    for (int s = 0; s < dc->num_stops; s++) {
                        MidiEvent cc = {MIDI_CC, (uint8_t)midi_ch,
                                        (uint8_t)dc->stops[s].engage_cc, 0};
                        ring_buffer_push(ring_buf, &cc);
                    }
                    printf("All stops OFF (%s)\n", dc->name);
                    continue;
                }

                /* Note key? */
                int semi = scancode_to_semitone(sc);
                if (semi >= 0) {
                    int note = (octave + 1) * 12 + semi;
                    if (note >= 0 && note < 128) {
                        MidiEvent on = {MIDI_NOTE_ON, (uint8_t)midi_ch,
                                        (uint8_t)note, 100};
                        ring_buffer_push(ring_buf, &on);
                        printf("Note ON:  %s%d (MIDI %d, ch %d)\n",
                               note_names[note % 12], note / 12 - 1, note, midi_ch);
                    }
                    continue;
                }

                /* Stop toggle key? (operates on active division) */
                if (organ_config->num_divisions > 0) {
                    int stop_idx = scancode_to_stop(sc);
                    const DivisionConfig *dc = &organ_config->divisions[active_division];
                    if (stop_idx >= 0 && stop_idx < dc->num_stops) {
                        const StopConfig *stop = &dc->stops[stop_idx];
                        /* Toggle: send CC 127 or 0 */
                        bool new_state = !stop->engaged;
                        uint8_t cc_val = new_state ? 127 : 0;
                        MidiEvent cc = {MIDI_CC, (uint8_t)midi_ch,
                                        (uint8_t)stop->engage_cc, cc_val};
                        ring_buffer_push(ring_buf, &cc);
                        printf("Stop %s: %s\n", stop->name,
                               new_state ? "ON" : "OFF");
                        continue;
                    }
                }
            }

            if (ev.type == SDL_KEYUP) {
                SDL_Scancode sc = ev.key.keysym.scancode;

                /* Note key released? */
                int semi = scancode_to_semitone(sc);
                if (semi >= 0) {
                    int midi_ch = 1;
                    if (organ_config->num_divisions > 0)
                        midi_ch = organ_config->divisions[active_division].midi_channel;
                    int note = (octave + 1) * 12 + semi;
                    if (note >= 0 && note < 128) {
                        MidiEvent off = {MIDI_NOTE_OFF, (uint8_t)midi_ch,
                                         (uint8_t)note, 0};
                        ring_buffer_push(ring_buf, &off);
                    }
                }
            }
        }

        SDL_Delay(10);  /* ~100 Hz poll rate */
    }

    SDL_DestroyWindow(win);
    SDL_Quit();
    return NULL;
}

int keyboard_start(RingBuffer *rb, OrganConfig *config, const char *path)
{
    ring_buf = rb;
    organ_config = config;
    config_path = path;
    running = 1;
    quit_requested = 0;
    active_division = 0;

    return pthread_create(&kbd_thread, NULL, keyboard_thread, NULL);
}

void keyboard_stop(void)
{
    running = 0;
    pthread_join(kbd_thread, NULL);
}

bool keyboard_quit_requested(void)
{
    return quit_requested;
}
