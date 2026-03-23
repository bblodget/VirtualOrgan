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
static jack_port_t   *ports[MAX_OUTPUT_CHANNELS];
static int            num_ports;
static JackEngineCtx *engine_ctx;

/* Apply a routing entry: set output channels and sample channel offset. */
static void apply_route(const RoutingConfig *rc,
                        int *out_channels, int *num_out, int *src_offset)
{
    int persp = rc->perspective;  /* 1-indexed, only used for perspective routes */
    int cpp = 2;  /* assume stereo perspectives */

    *src_offset = (persp - 1) * cpp;
    *num_out = rc->num_output_channels;
    for (int j = 0; j < rc->num_output_channels; j++)
        out_channels[j] = rc->output_channels[j] - 1;  /* 1-indexed → 0-indexed */
}

/* Look up output routing for a given rank and division.
 * Precedence: rank > division > perspective.
 * Returns default stereo routing (channels 0,1) if no routes configured. */
static void get_routing(const OrganConfig *cfg, int rank_index, int div_index,
                        int *out_channels, int *num_out, int *src_offset)
{
    const RoutingConfig *perspective_route = NULL;
    const RoutingConfig *division_route = NULL;
    const RoutingConfig *rank_route = NULL;

    for (int r = 0; r < cfg->num_routes; r++) {
        const RoutingConfig *rc = &cfg->routes[r];
        switch (rc->source_type) {
        case ROUTE_PERSPECTIVE:
            if (!perspective_route)
                perspective_route = rc;
            break;
        case ROUTE_DIVISION:
            if (div_index >= 0 && rc->division_index == div_index)
                division_route = rc;
            break;
        case ROUTE_RANK:
            if (rc->rank_index == rank_index)
                rank_route = rc;
            break;
        }
    }

    /* Apply most specific match */
    if (rank_route) {
        apply_route(rank_route, out_channels, num_out, src_offset);
    } else if (division_route) {
        apply_route(division_route, out_channels, num_out, src_offset);
    } else if (perspective_route) {
        apply_route(perspective_route, out_channels, num_out, src_offset);
    } else {
        /* No routing configured: default stereo to channels 0,1 */
        *src_offset = 0;
        *num_out = 2;
        if (num_ports < 2) *num_out = num_ports;
        out_channels[0] = 0;
        if (*num_out > 1) out_channels[1] = 1;
    }
}

/* Helper: trigger engaged stops for a division, with routing */
static void trigger_division(int div_idx, uint8_t note, uint8_t velocity)
{
    OrganConfig *cfg = engine_ctx->config;
    DivisionConfig *dc = &cfg->divisions[div_idx];

    for (int s = 0; s < dc->num_stops; s++) {
        StopConfig *sc = &dc->stops[s];
        if (!sc->engaged || sc->num_ranks == 0)
            continue;
        for (int ri = 0; ri < sc->num_ranks; ri++) {
            int rank_idx = sc->rank_indices[ri];
            const Sample *sample = &engine_ctx->sample_banks[rank_idx].samples[note];
            if (!sample->data)
                continue;

            int out_ch[MAX_OUTPUT_CHANNELS];
            int num_out, src_offset;
            get_routing(cfg, rank_idx, div_idx, out_ch, &num_out, &src_offset);

            voice_pool_note_on(engine_ctx->voice_pool, note, velocity,
                               sample, div_idx, out_ch, num_out, src_offset);
        }
    }
}

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
                    if (cfg->divisions[d].midi_channel != ev.channel)
                        continue;

                    trigger_division(d, ev.note, ev.velocity);

                    /* Check couplers from this division */
                    for (int c = 0; c < cfg->num_couplers; c++) {
                        CouplerConfig *coup = &cfg->couplers[c];
                        if (coup->engaged && coup->from_division == d &&
                            coup->to_division >= 0 && coup->to_division < cfg->num_divisions)
                            trigger_division(coup->to_division, ev.note, ev.velocity);
                    }
                }
            } else {
                /* No divisions: legacy mode, trigger all ranks */
                int out_ch[] = {0, 1};
                int num_out = (num_ports < 2) ? num_ports : 2;
                for (int r = 0; r < engine_ctx->num_banks; r++) {
                    const Sample *sample = &engine_ctx->sample_banks[r].samples[ev.note];
                    if (sample->data)
                        voice_pool_note_on(engine_ctx->voice_pool, ev.note, ev.velocity,
                                           sample, -1, out_ch, num_out, 0);
                }
            }
        } else if (ev.type == MIDI_NOTE_OFF ||
                   (ev.type == MIDI_NOTE_ON && ev.velocity == 0)) {
            voice_pool_note_off(engine_ctx->voice_pool, ev.note);
        } else if (ev.type == MIDI_CC) {
            /* Check stop engage CCs and expression CCs across all divisions */
            for (int d = 0; d < cfg->num_divisions; d++) {
                DivisionConfig *dc = &cfg->divisions[d];
                if (dc->expression_cc >= 0 && ev.note == dc->expression_cc)
                    dc->expression_gain = (float)ev.velocity / 127.0f;
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
    float *bufs[MAX_OUTPUT_CHANNELS];
    for (int i = 0; i < num_ports; i++)
        bufs[i] = (float *)jack_port_get_buffer(ports[i], nframes);

    /* Render all voices into output */
    mixer_render(engine_ctx->voice_pool, bufs, num_ports, (int)nframes, engine_ctx->config);

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
    num_ports = ctx->config->num_outputs;
    if (num_ports > MAX_OUTPUT_CHANNELS) num_ports = MAX_OUTPUT_CHANNELS;

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

    /* Register output ports */
    for (int i = 0; i < num_ports; i++) {
        char name[32];
        snprintf(name, sizeof(name), "out_%d", i + 1);
        ports[i] = jack_port_register(client, name,
                                       JACK_DEFAULT_AUDIO_TYPE,
                                       JackPortIsOutput, 0);
        if (!ports[i]) {
            fprintf(stderr, "jack: cannot register port %s\n", name);
            jack_client_close(client);
            client = NULL;
            return -1;
        }
    }
    printf("jack: registered %d output ports\n", num_ports);

    if (jack_activate(client)) {
        fprintf(stderr, "jack: cannot activate client\n");
        jack_client_close(client);
        client = NULL;
        return -1;
    }

    /* Auto-connect to system playback ports */
    const char **sys_ports = jack_get_ports(client, NULL, NULL,
                                            JackPortIsInput | JackPortIsPhysical);
    if (sys_ports) {
        for (int i = 0; sys_ports[i] && i < num_ports; i++)
            jack_connect(client, jack_port_name(ports[i]), sys_ports[i]);
        jack_free(sys_ports);
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
