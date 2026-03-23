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
    OrganConfig *cfg = engine_ctx->config;
    MidiEvent ev;
    while (ring_buffer_pop(engine_ctx->ring_buffer, &ev)) {
        if (ev.type == MIDI_NOTE_ON && ev.velocity > 0) {
            if (cfg->num_divisions > 0) {
                /* Division-aware: find matching division by MIDI channel */
                for (int d = 0; d < cfg->num_divisions; d++) {
                    DivisionConfig *dc = &cfg->divisions[d];
                    if (dc->midi_channel != ev.channel)
                        continue;

                    /* Trigger this division's engaged stops */
                    for (int s = 0; s < dc->num_stops; s++) {
                        StopConfig *sc = &dc->stops[s];
                        if (!sc->engaged || sc->num_ranks == 0)
                            continue;
                        for (int ri = 0; ri < sc->num_ranks; ri++) {
                            const Sample *sample = &engine_ctx->sample_banks[sc->rank_indices[ri]].samples[ev.note];
                            if (sample->data)
                                voice_pool_note_on(engine_ctx->voice_pool, ev.note, ev.velocity, sample, d);
                        }
                    }

                    /* Check couplers: if any couple FROM this division, also trigger the TO division */
                    for (int c = 0; c < cfg->num_couplers; c++) {
                        CouplerConfig *coup = &cfg->couplers[c];
                        if (!coup->engaged || coup->from_division != d)
                            continue;
                        int td = coup->to_division;
                        if (td < 0 || td >= cfg->num_divisions)
                            continue;
                        DivisionConfig *to_dc = &cfg->divisions[td];
                        for (int s = 0; s < to_dc->num_stops; s++) {
                            StopConfig *sc = &to_dc->stops[s];
                            if (!sc->engaged || sc->num_ranks == 0)
                                continue;
                            for (int ri = 0; ri < sc->num_ranks; ri++) {
                                const Sample *sample = &engine_ctx->sample_banks[sc->rank_indices[ri]].samples[ev.note];
                                if (sample->data)
                                    voice_pool_note_on(engine_ctx->voice_pool, ev.note, ev.velocity, sample, td);
                            }
                        }
                    }
                }
            } else {
                /* No divisions: legacy mode, trigger all ranks */
                for (int r = 0; r < engine_ctx->num_banks; r++) {
                    const Sample *sample = &engine_ctx->sample_banks[r].samples[ev.note];
                    if (sample->data)
                        voice_pool_note_on(engine_ctx->voice_pool, ev.note, ev.velocity, sample, -1);
                }
            }
        } else if (ev.type == MIDI_NOTE_OFF ||
                   (ev.type == MIDI_NOTE_ON && ev.velocity == 0)) {
            voice_pool_note_off(engine_ctx->voice_pool, ev.note);
        } else if (ev.type == MIDI_CC) {
            /* Check stop engage CCs and expression CCs across all divisions */
            for (int d = 0; d < cfg->num_divisions; d++) {
                DivisionConfig *dc = &cfg->divisions[d];
                /* Expression pedal */
                if (dc->expression_cc >= 0 && ev.note == dc->expression_cc)
                    dc->expression_gain = (float)ev.velocity / 127.0f;
                /* Stop toggles */
                for (int s = 0; s < dc->num_stops; s++) {
                    if (ev.note == dc->stops[s].engage_cc)
                        dc->stops[s].engaged = (ev.velocity >= 64);
                }
            }
            /* Coupler toggles */
            for (int c = 0; c < cfg->num_couplers; c++) {
                if (ev.note == cfg->couplers[c].engage_cc)
                    cfg->couplers[c].engaged = (ev.velocity >= 64);
            }
        }
    }

    /* Get JACK output buffers */
    float *bufs[2] = {
        (float *)jack_port_get_buffer(port_left, nframes),
        (float *)jack_port_get_buffer(port_right, nframes),
    };

    /* Render all voices into output */
    mixer_render(engine_ctx->voice_pool, bufs, 2, (int)nframes, engine_ctx->config);

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
