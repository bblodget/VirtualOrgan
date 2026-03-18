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
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "midi.h"

static pthread_t midi_thread;
static volatile int running;

/* ---- Fake MIDI mode: play a repeating scale ---- */

static void *fake_midi_thread(void *arg)
{
    RingBuffer *rb = (RingBuffer *)arg;
    const uint8_t scale[] = {60, 62, 64, 65, 67, 69, 71, 72};  /* C major scale */
    int idx = 0;

    printf("midi: fake mode — playing C major scale\n");

    while (running) {
        uint8_t note = scale[idx % 8];

        /* Note on */
        MidiEvent on = {MIDI_NOTE_ON, 1, note, 100};
        ring_buffer_push(rb, &on);

        usleep(400000);  /* hold for 400ms */

        /* Note off */
        MidiEvent off = {MIDI_NOTE_OFF, 1, note, 0};
        ring_buffer_push(rb, &off);

        usleep(100000);  /* gap of 100ms */

        idx++;
    }

    return NULL;
}

/* ---- Real MIDI mode: ALSA sequencer ---- */

static void *alsa_midi_thread(void *arg)
{
    RingBuffer *rb = (RingBuffer *)arg;
    snd_seq_t *seq = NULL;

    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        fprintf(stderr, "midi: cannot open ALSA sequencer\n");
        return NULL;
    }

    snd_seq_set_client_name(seq, "organ-engine");

    int port = snd_seq_create_simple_port(seq, "midi_in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);

    if (port < 0) {
        fprintf(stderr, "midi: cannot create port\n");
        snd_seq_close(seq);
        return NULL;
    }

    printf("midi: ALSA sequencer port created (connect with aconnect)\n");
    printf("midi: client %d, port %d\n", snd_seq_client_id(seq), port);

    /* Set up polling */
    int npfds = snd_seq_poll_descriptors_count(seq, POLLIN);
    struct pollfd *pfds = malloc(npfds * sizeof(struct pollfd));
    snd_seq_poll_descriptors(seq, pfds, npfds, POLLIN);

    while (running) {
        if (poll(pfds, npfds, 100) <= 0)
            continue;

        snd_seq_event_t *ev;
        while (snd_seq_event_input(seq, &ev) >= 0) {
            MidiEvent me = {0};

            switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON:
                me.type     = MIDI_NOTE_ON;
                me.channel  = ev->data.note.channel + 1;
                me.note     = ev->data.note.note;
                me.velocity = ev->data.note.velocity;
                ring_buffer_push(rb, &me);
                break;

            case SND_SEQ_EVENT_NOTEOFF:
                me.type     = MIDI_NOTE_OFF;
                me.channel  = ev->data.note.channel + 1;
                me.note     = ev->data.note.note;
                me.velocity = 0;
                ring_buffer_push(rb, &me);
                break;

            case SND_SEQ_EVENT_CONTROLLER:
                me.type     = MIDI_CC;
                me.channel  = ev->data.control.channel + 1;
                me.note     = ev->data.control.param;
                me.velocity = ev->data.control.value;
                ring_buffer_push(rb, &me);
                break;

            default:
                break;
            }
        }
    }

    free(pfds);
    snd_seq_close(seq);
    return NULL;
}

int midi_start(RingBuffer *rb, bool fake_midi)
{
    running = 1;

    if (fake_midi)
        return pthread_create(&midi_thread, NULL, fake_midi_thread, rb);
    else
        return pthread_create(&midi_thread, NULL, alsa_midi_thread, rb);
}

void midi_stop(void)
{
    running = 0;
    pthread_join(midi_thread, NULL);
}
