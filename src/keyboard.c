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
static const OrganConfig *organ_config;

static void print_help(void)
{
    printf("\nKeyboard mode (SDL2) — focus the SDL window to play\n");
    printf("  Notes:  Q W E R T Y U I O P  (white keys)\n");
    printf("          2 3   5 6 7   9 0    (black keys)\n");
    printf("  Octave: [ down  ] up\n");
    printf("  Stops:  ");

    /* Map stop keys to rank names from config */
    const char *stop_keys[] = {"Z", "X", "C", "V", "B", "N", "M"};
    int count = 0;
    for (int i = 0; i < organ_config->num_ranks && count < 7; i++) {
        if (organ_config->ranks[i].engage_cc >= 0) {
            printf("%s=%s ", stop_keys[count], organ_config->ranks[i].name);
            count++;
        }
    }
    if (count == 0)
        printf("(no stops configured)");
    printf("\n");
    printf("  -/= = gain down/up  Space = all stops off  H = this help\n");
    printf("  Esc to quit\n\n");
}

/* Build a mapping from stop index (0-6) to rank index in config.
 * Only ranks with engage_cc are included. */
static int stop_to_rank[7];
static int num_stop_keys;

static void build_stop_map(void)
{
    num_stop_keys = 0;
    for (int i = 0; i < organ_config->num_ranks && num_stop_keys < 7; i++) {
        if (organ_config->ranks[i].engage_cc >= 0) {
            stop_to_rank[num_stop_keys] = i;
            num_stop_keys++;
        }
    }
}

/* Track which stop keys are currently toggled on */
static bool stop_state[7];

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

    build_stop_map();
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

                /* Spacebar = all stops off */
                if (sc == SDL_SCANCODE_SPACE) {
                    for (int s = 0; s < num_stop_keys; s++) {
                        if (stop_state[s]) {
                            stop_state[s] = false;
                            int rank_idx = stop_to_rank[s];
                            const RankConfig *rc = &organ_config->ranks[rank_idx];
                            MidiEvent cc = {MIDI_CC, 1, (uint8_t)rc->engage_cc, 0};
                            ring_buffer_push(ring_buf, &cc);
                        }
                    }
                    printf("All stops OFF\n");
                    continue;
                }

                /* Note key? */
                int semi = scancode_to_semitone(sc);
                if (semi >= 0) {
                    int note = (octave + 1) * 12 + semi;  /* MIDI note number */
                    if (note >= 0 && note < 128) {
                        MidiEvent on = {MIDI_NOTE_ON, 1, (uint8_t)note, 100};
                        ring_buffer_push(ring_buf, &on);
                        printf("Note ON:  %s%d (MIDI %d)\n",
                               note_names[note % 12], note / 12 - 1, note);
                    }
                    continue;
                }

                /* Stop toggle key? */
                int stop_idx = scancode_to_stop(sc);
                if (stop_idx >= 0 && stop_idx < num_stop_keys) {
                    int rank_idx = stop_to_rank[stop_idx];
                    const RankConfig *rc = &organ_config->ranks[rank_idx];
                    stop_state[stop_idx] = !stop_state[stop_idx];
                    uint8_t cc_val = stop_state[stop_idx] ? 127 : 0;
                    MidiEvent cc = {MIDI_CC, 1, (uint8_t)rc->engage_cc, cc_val};
                    ring_buffer_push(ring_buf, &cc);
                    printf("Stop %s: %s\n", rc->name,
                           stop_state[stop_idx] ? "ON" : "OFF");
                    continue;
                }
            }

            if (ev.type == SDL_KEYUP) {
                SDL_Scancode sc = ev.key.keysym.scancode;

                /* Note key released? */
                int semi = scancode_to_semitone(sc);
                if (semi >= 0) {
                    int note = (octave + 1) * 12 + semi;
                    if (note >= 0 && note < 128) {
                        MidiEvent off = {MIDI_NOTE_OFF, 1, (uint8_t)note, 0};
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

int keyboard_start(RingBuffer *rb, const OrganConfig *config)
{
    ring_buf = rb;
    organ_config = config;
    running = 1;
    quit_requested = 0;

    for (int i = 0; i < 7; i++)
        stop_state[i] = false;

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
