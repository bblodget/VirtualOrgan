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
#include <string.h>
#include <jack/jack.h>
#include "jack_engine.h"
#include "mixer.h"

static jack_client_t *client;
static jack_port_t   *port_left;
static jack_port_t   *port_right;
static JackEngineCtx *engine_ctx;

static int process_callback(jack_nframes_t nframes, void *arg)
{
    (void)arg;

    /* Drain MIDI events from ring buffer */
    MidiEvent ev;
    while (ring_buffer_pop(engine_ctx->ring_buffer, &ev)) {
        if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
            /* Find the sample for this note in the first rank (Phase 2) */
            const Sample *sample = &engine_ctx->sample_bank->samples[ev.note];
            voice_pool_note_on(engine_ctx->voice_pool, ev.note, ev.velocity, sample);
        } else if (ev.type == MIDI_NOTE_OFF ||
                   (ev.type == MIDI_NOTE_ON && ev.velocity == 0)) {
            voice_pool_note_off(engine_ctx->voice_pool, ev.note);
        }
    }

    /* Get JACK output buffers */
    float *left  = (float *)jack_port_get_buffer(port_left, nframes);
    float *right = (float *)jack_port_get_buffer(port_right, nframes);

    /* Render all voices into output */
    mixer_render(engine_ctx->voice_pool, left, right, (int)nframes);

    return 0;
}

static void shutdown_callback(void *arg)
{
    (void)arg;
    fprintf(stderr, "jack: server shut down\n");
}

int jack_engine_start(JackEngineCtx *ctx)
{
    engine_ctx = ctx;

    jack_status_t status;
    client = jack_client_open(ctx->config->jack_client_name,
                              JackNoStartServer, &status);
    if (!client) {
        fprintf(stderr, "jack: cannot connect to server (status 0x%x)\n", status);
        return -1;
    }

    printf("jack: connected as '%s', sample rate %u, buffer size %u\n",
           jack_get_client_name(client),
           jack_get_sample_rate(client),
           jack_get_buffer_size(client));

    jack_set_process_callback(client, process_callback, NULL);
    jack_on_shutdown(client, shutdown_callback, NULL);

    port_left = jack_port_register(client, "out_left",
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);
    port_right = jack_port_register(client, "out_right",
                                    JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);

    if (!port_left || !port_right) {
        fprintf(stderr, "jack: cannot register ports\n");
        jack_client_close(client);
        client = NULL;
        return -1;
    }

    if (jack_activate(client)) {
        fprintf(stderr, "jack: cannot activate client\n");
        jack_client_close(client);
        client = NULL;
        return -1;
    }

    /* Auto-connect to system playback ports */
    const char **ports = jack_get_ports(client, NULL, NULL,
                                        JackPortIsInput | JackPortIsPhysical);
    if (ports) {
        if (ports[0])
            jack_connect(client, jack_port_name(port_left), ports[0]);
        if (ports[1])
            jack_connect(client, jack_port_name(port_right), ports[1]);
        jack_free(ports);
    }

    return 0;
}

void jack_engine_stop(void)
{
    if (client) {
        jack_deactivate(client);
        jack_client_close(client);
        client = NULL;
    }
}
