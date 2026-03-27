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
#include <string.h>
#include "config.h"
#include "toml.h"

int config_load(OrganConfig *cfg, const char *path)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Defaults */
    cfg->sample_rate = 48000;
    cfg->buffer_size = 128;
    cfg->num_outputs = 2;
    cfg->release_fade_ms = 250;
    cfg->master_gain = 0.10f;
    strncpy(cfg->jack_client_name, "organ", sizeof(cfg->jack_client_name) - 1);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "config: cannot open '%s'\n", path);
        return -1;
    }

    char errbuf[200];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        fprintf(stderr, "config: parse error: %s\n", errbuf);
        return -1;
    }

    /* [audio] section */
    toml_table_t *audio = toml_table_in(root, "audio");
    if (audio) {
        toml_datum_t val;

        val = toml_int_in(audio, "sample_rate");
        if (val.ok) cfg->sample_rate = (int)val.u.i;

        val = toml_int_in(audio, "buffer_size");
        if (val.ok) cfg->buffer_size = (int)val.u.i;

        val = toml_int_in(audio, "num_outputs");
        if (val.ok) cfg->num_outputs = (int)val.u.i;

        val = toml_int_in(audio, "release_fade_ms");
        if (val.ok) cfg->release_fade_ms = (int)val.u.i;

        toml_datum_t dval = toml_double_in(audio, "master_gain");
        if (dval.ok) cfg->master_gain = (float)dval.u.d;

        val = toml_string_in(audio, "jack_client_name");
        if (val.ok) {
            strncpy(cfg->jack_client_name, val.u.s, sizeof(cfg->jack_client_name) - 1);
            free(val.u.s);
        }
    }

    /* [ranks.*] sections */
    toml_table_t *ranks = toml_table_in(root, "ranks");
    if (ranks) {
        int n = toml_table_ntab(ranks);
        for (int i = 0; i < n && cfg->num_ranks < MAX_RANKS; i++) {
            const char *key = toml_key_in(ranks, i);
            toml_table_t *rank = toml_table_in(ranks, key);
            if (!rank) continue;

            RankConfig *rc = &cfg->ranks[cfg->num_ranks];
            strncpy(rc->name, key, sizeof(rc->name) - 1);

            toml_datum_t val;

            /* sample_dir: string or array of strings */
            rc->num_sample_dirs = 0;
            val = toml_string_in(rank, "sample_dir");
            if (val.ok) {
                /* Single directory */
                strncpy(rc->sample_dirs[0], val.u.s, MAX_PATH_LEN - 1);
                rc->num_sample_dirs = 1;
                free(val.u.s);
            } else {
                /* Array of directories */
                toml_array_t *dirs = toml_array_in(rank, "sample_dir");
                if (dirs) {
                    int nd = toml_array_nelem(dirs);
                    for (int di = 0; di < nd && di < MAX_SAMPLE_DIRS; di++) {
                        toml_datum_t dv = toml_string_at(dirs, di);
                        if (dv.ok) {
                            strncpy(rc->sample_dirs[rc->num_sample_dirs], dv.u.s, MAX_PATH_LEN - 1);
                            rc->num_sample_dirs++;
                            free(dv.u.s);
                        }
                    }
                }
            }
            rc->num_perspectives = rc->num_sample_dirs > 0 ? rc->num_sample_dirs : 1;

            val = toml_string_in(rank, "filename_pattern");
            if (val.ok) {
                strncpy(rc->filename_pattern, val.u.s, sizeof(rc->filename_pattern) - 1);
                free(val.u.s);
            } else {
                strncpy(rc->filename_pattern, "{note:03d}.wav", sizeof(rc->filename_pattern) - 1);
            }

            cfg->num_ranks++;
        }
    }

    /* [divisions.*] sections (optional — if absent, all ranks always play) */
    toml_table_t *divisions = toml_table_in(root, "divisions");
    if (divisions) {
        int nd = toml_table_ntab(divisions);
        for (int d = 0; d < nd && cfg->num_divisions < MAX_DIVISIONS; d++) {
            const char *div_key = toml_key_in(divisions, d);
            toml_table_t *div = toml_table_in(divisions, div_key);
            if (!div) continue;

            DivisionConfig *dc = &cfg->divisions[cfg->num_divisions];
            strncpy(dc->name, div_key, sizeof(dc->name) - 1);
            dc->expression_cc = -1;
            dc->expression_gain = 1.0f;

            toml_datum_t val;
            val = toml_int_in(div, "midi_channel");
            if (val.ok) dc->midi_channel = (int)val.u.i;

            val = toml_int_in(div, "expression_cc");
            if (val.ok) dc->expression_cc = (int)val.u.i;

            /* Velocity sensitivity — default false (organ mode) */
            dc->velocity_sensitive = false;
            val = toml_bool_in(div, "velocity_sensitive");
            if (val.ok) dc->velocity_sensitive = val.u.b;

            /* Optional note_range for keyboard splits */
            dc->has_note_range = false;
            toml_array_t *nrange = toml_array_in(div, "note_range");
            int nelem = nrange ? toml_array_nelem(nrange) : 0;
            if (nelem == 1) {
                toml_datum_t lo = toml_int_at(nrange, 0);
                if (lo.ok) {
                    dc->note_range[0] = (int)lo.u.i;
                    dc->note_range[1] = (int)lo.u.i;
                    dc->has_note_range = true;
                }
            } else if (nelem == 2) {
                toml_datum_t lo = toml_int_at(nrange, 0);
                toml_datum_t hi = toml_int_at(nrange, 1);
                if (lo.ok && hi.ok) {
                    dc->note_range[0] = (int)lo.u.i;
                    dc->note_range[1] = (int)hi.u.i;
                    dc->has_note_range = true;
                }
            }

            /* Parse stops within this division */
            toml_table_t *stops = toml_table_in(div, "stops");
            if (stops) {
                int ns = toml_table_ntab(stops);
                for (int s = 0; s < ns && dc->num_stops < MAX_STOPS_PER_DIV; s++) {
                    const char *stop_key = toml_key_in(stops, s);
                    toml_table_t *stop = toml_table_in(stops, stop_key);
                    if (!stop) continue;

                    StopConfig *sc = &dc->stops[dc->num_stops];
                    strncpy(sc->name, stop_key, sizeof(sc->name) - 1);
                    sc->engaged = false;
                    sc->num_ranks = 0;

                    /* rank can be a string or array of strings */
                    val = toml_string_in(stop, "rank");
                    if (val.ok) {
                        /* Single rank: rank = "name" */
                        int idx = -1;
                        for (int r = 0; r < cfg->num_ranks; r++) {
                            if (strcmp(cfg->ranks[r].name, val.u.s) == 0) {
                                idx = r;
                                break;
                            }
                        }
                        if (idx >= 0)
                            sc->rank_indices[sc->num_ranks++] = idx;
                        else
                            fprintf(stderr, "config: stop '%s' references unknown rank '%s'\n",
                                    stop_key, val.u.s);
                        free(val.u.s);
                    } else {
                        /* Multi-rank: rank = ["name1", "name2"] */
                        toml_array_t *rank_arr = toml_array_in(stop, "rank");
                        if (rank_arr) {
                            int nr = toml_array_nelem(rank_arr);
                            for (int ri = 0; ri < nr && sc->num_ranks < MAX_RANKS_PER_STOP; ri++) {
                                toml_datum_t rv = toml_string_at(rank_arr, ri);
                                if (!rv.ok) continue;
                                int idx = -1;
                                for (int r = 0; r < cfg->num_ranks; r++) {
                                    if (strcmp(cfg->ranks[r].name, rv.u.s) == 0) {
                                        idx = r;
                                        break;
                                    }
                                }
                                if (idx >= 0)
                                    sc->rank_indices[sc->num_ranks++] = idx;
                                else
                                    fprintf(stderr, "config: stop '%s' references unknown rank '%s'\n",
                                            stop_key, rv.u.s);
                                free(rv.u.s);
                            }
                        }
                    }

                    val = toml_int_in(stop, "engage_cc");
                    if (val.ok) sc->engage_cc = (int)val.u.i;

                    val = toml_bool_in(stop, "engaged");
                    if (val.ok) sc->engaged = val.u.b;

                    dc->num_stops++;
                }
            }

            cfg->num_divisions++;
        }
    }

    /* [couplers.*] sections (optional) */
    toml_table_t *couplers = toml_table_in(root, "couplers");
    if (couplers) {
        int nc = toml_table_ntab(couplers);
        for (int c = 0; c < nc && cfg->num_couplers < MAX_COUPLERS; c++) {
            const char *coup_key = toml_key_in(couplers, c);
            toml_table_t *coup = toml_table_in(couplers, coup_key);
            if (!coup) continue;

            CouplerConfig *cc = &cfg->couplers[cfg->num_couplers];
            strncpy(cc->name, coup_key, sizeof(cc->name) - 1);
            cc->from_division = -1;
            cc->to_division = -1;
            cc->engaged = false;

            toml_datum_t val;

            val = toml_string_in(coup, "from");
            if (val.ok) {
                for (int d = 0; d < cfg->num_divisions; d++) {
                    if (strcmp(cfg->divisions[d].name, val.u.s) == 0) {
                        cc->from_division = d;
                        break;
                    }
                }
                if (cc->from_division < 0)
                    fprintf(stderr, "config: coupler '%s' references unknown division '%s'\n",
                            coup_key, val.u.s);
                free(val.u.s);
            }

            val = toml_string_in(coup, "to");
            if (val.ok) {
                for (int d = 0; d < cfg->num_divisions; d++) {
                    if (strcmp(cfg->divisions[d].name, val.u.s) == 0) {
                        cc->to_division = d;
                        break;
                    }
                }
                if (cc->to_division < 0)
                    fprintf(stderr, "config: coupler '%s' references unknown division '%s'\n",
                            coup_key, val.u.s);
                free(val.u.s);
            }

            val = toml_int_in(coup, "engage_cc");
            if (val.ok) cc->engage_cc = (int)val.u.i;

            cfg->num_couplers++;
        }
    }

    /* [routing.*] sections (optional) */
    toml_table_t *routing = toml_table_in(root, "routing");
    if (routing) {
        int nr = toml_table_ntab(routing);
        for (int r = 0; r < nr && cfg->num_routes < MAX_ROUTES; r++) {
            const char *route_key = toml_key_in(routing, r);
            toml_table_t *route = toml_table_in(routing, route_key);
            if (!route) continue;

            RoutingConfig *rc = &cfg->routes[cfg->num_routes];
            strncpy(rc->name, route_key, sizeof(rc->name) - 1);
            rc->source_type = ROUTE_PERSPECTIVE;
            rc->perspective = 1;
            rc->channel = 0;  /* 0 = all channels (default) */
            rc->division_index = -1;
            rc->rank_index = -1;

            /* Parse source = { perspective = N, channel = N } or
             *        source = { division = "name" } or
             *        source = { rank = "name", note_range = [lo, hi] } */
            toml_table_t *source = toml_table_in(route, "source");
            toml_datum_t val;
            rc->has_note_range = false;
            if (source) {
                val = toml_int_in(source, "perspective");
                if (val.ok) {
                    rc->source_type = ROUTE_PERSPECTIVE;
                    rc->perspective = (int)val.u.i;
                }
                val = toml_string_in(source, "division");
                if (val.ok) {
                    rc->source_type = ROUTE_DIVISION;
                    for (int d = 0; d < cfg->num_divisions; d++) {
                        if (strcmp(cfg->divisions[d].name, val.u.s) == 0) {
                            rc->division_index = d;
                            break;
                        }
                    }
                    if (rc->division_index < 0)
                        fprintf(stderr, "config: route '%s' references unknown division '%s'\n",
                                route_key, val.u.s);
                    free(val.u.s);
                }
                val = toml_string_in(source, "rank");
                if (val.ok) {
                    rc->source_type = ROUTE_RANK;
                    for (int ri = 0; ri < cfg->num_ranks; ri++) {
                        if (strcmp(cfg->ranks[ri].name, val.u.s) == 0) {
                            rc->rank_index = ri;
                            break;
                        }
                    }
                    if (rc->rank_index < 0)
                        fprintf(stderr, "config: route '%s' references unknown rank '%s'\n",
                                route_key, val.u.s);
                    free(val.u.s);
                }
                /* note_range inside source: [lo, hi] or [note] */
                toml_array_t *nrange = toml_array_in(source, "note_range");
                int nelem = nrange ? toml_array_nelem(nrange) : 0;
                if (nelem == 1) {
                    toml_datum_t lo = toml_int_at(nrange, 0);
                    if (lo.ok) {
                        rc->note_range[0] = (int)lo.u.i;
                        rc->note_range[1] = (int)lo.u.i;
                        rc->has_note_range = true;
                    }
                } else if (nelem == 2) {
                    toml_datum_t lo = toml_int_at(nrange, 0);
                    toml_datum_t hi = toml_int_at(nrange, 1);
                    if (lo.ok && hi.ok) {
                        rc->note_range[0] = (int)lo.u.i;
                        rc->note_range[1] = (int)hi.u.i;
                        rc->has_note_range = true;
                    }
                }
                /* channel inside source: pick one channel from perspective */
                val = toml_int_in(source, "channel");
                if (val.ok) rc->channel = (int)val.u.i;
            }

            toml_array_t *channels = toml_array_in(route, "output_channels");
            if (channels) {
                int nc = toml_array_nelem(channels);
                for (int j = 0; j < nc && j < MAX_OUTPUT_CHANNELS; j++) {
                    toml_datum_t ch = toml_int_at(channels, j);
                    if (ch.ok)
                        rc->output_channels[rc->num_output_channels++] = (int)ch.u.i;
                }
            }

            cfg->num_routes++;
        }
    }

    /* [midi_devices.*] sections (optional) */
    toml_table_t *midi_devs = toml_table_in(root, "midi_devices");
    if (midi_devs) {
        int nm = toml_table_ntab(midi_devs);
        for (int m = 0; m < nm && cfg->num_midi_devices < MAX_MIDI_DEVICES; m++) {
            const char *dev_key = toml_key_in(midi_devs, m);
            toml_table_t *dev = toml_table_in(midi_devs, dev_key);
            if (!dev) continue;

            MidiDeviceConfig *md = &cfg->midi_devices[cfg->num_midi_devices];
            strncpy(md->name, dev_key, sizeof(md->name) - 1);

            toml_datum_t val = toml_int_in(dev, "midi_channel");
            if (val.ok) md->midi_channel = (int)val.u.i;

            cfg->num_midi_devices++;
        }
    }

    toml_free(root);
    return 0;
}

int config_reload(OrganConfig *cfg, const char *path)
{
    OrganConfig new_cfg;
    if (config_load(&new_cfg, path) != 0)
        return -1;

    if (new_cfg.num_ranks != cfg->num_ranks) {
        fprintf(stderr, "config_reload: rank count changed (%d → %d) — "
                "restart engine to change ranks\n",
                cfg->num_ranks, new_cfg.num_ranks);
        return -1;
    }

    /* Copy safe-to-change fields */
    cfg->num_outputs = new_cfg.num_outputs;
    cfg->release_fade_ms = new_cfg.release_fade_ms;
    cfg->master_gain = new_cfg.master_gain;

    cfg->num_divisions = new_cfg.num_divisions;
    for (int d = 0; d < new_cfg.num_divisions && d < MAX_DIVISIONS; d++)
        cfg->divisions[d] = new_cfg.divisions[d];

    cfg->num_couplers = new_cfg.num_couplers;
    for (int c = 0; c < new_cfg.num_couplers && c < MAX_COUPLERS; c++)
        cfg->couplers[c] = new_cfg.couplers[c];

    cfg->num_routes = new_cfg.num_routes;
    for (int r = 0; r < new_cfg.num_routes && r < MAX_ROUTES; r++)
        cfg->routes[r] = new_cfg.routes[r];

    cfg->num_midi_devices = new_cfg.num_midi_devices;
    for (int m = 0; m < new_cfg.num_midi_devices && m < MAX_MIDI_DEVICES; m++)
        cfg->midi_devices[m] = new_cfg.midi_devices[m];

    return 0;
}

void config_print(const OrganConfig *cfg)
{
    printf("Config:\n");
    printf("  sample_rate: %d\n", cfg->sample_rate);
    printf("  buffer_size: %d\n", cfg->buffer_size);
    printf("  jack_client: %s\n", cfg->jack_client_name);
    printf("  num_outputs: %d\n", cfg->num_outputs);
    printf("  master_gain: %.2f\n", cfg->master_gain);
    printf("  ranks: %d\n", cfg->num_ranks);

    for (int i = 0; i < cfg->num_ranks; i++) {
        const RankConfig *rc = &cfg->ranks[i];
        printf("    [%s]\n", rc->name);
        for (int d = 0; d < rc->num_sample_dirs; d++)
            printf("      sample_dir[%d]: %s\n", d + 1, rc->sample_dirs[d]);
        printf("      filename_pattern: %s\n", rc->filename_pattern);
        if (rc->num_perspectives > 1)
            printf("      perspectives: %d\n", rc->num_perspectives);
    }

    if (cfg->num_divisions > 0) {
        printf("  divisions: %d\n", cfg->num_divisions);
        for (int d = 0; d < cfg->num_divisions; d++) {
            const DivisionConfig *dc = &cfg->divisions[d];
            printf("    [%s] midi_channel=%d", dc->name, dc->midi_channel);
            if (dc->has_note_range)
                printf(" notes=%d-%d", dc->note_range[0], dc->note_range[1]);
            if (dc->expression_cc >= 0)
                printf(" expression_cc=%d", dc->expression_cc);
            printf("\n");
            for (int s = 0; s < dc->num_stops; s++) {
                const StopConfig *sc = &dc->stops[s];
                printf("      %s → ", sc->name);
                for (int ri = 0; ri < sc->num_ranks; ri++) {
                    if (ri > 0) printf("+");
                    printf("%s", cfg->ranks[sc->rank_indices[ri]].name);
                }
                printf(" (cc=%d)\n", sc->engage_cc);
            }
        }
    }

    if (cfg->num_couplers > 0) {
        printf("  couplers: %d\n", cfg->num_couplers);
        for (int c = 0; c < cfg->num_couplers; c++) {
            const CouplerConfig *cc = &cfg->couplers[c];
            printf("    [%s] %s → %s (cc=%d)\n", cc->name,
                   cc->from_division >= 0 ? cfg->divisions[cc->from_division].name : "?",
                   cc->to_division >= 0 ? cfg->divisions[cc->to_division].name : "?",
                   cc->engage_cc);
        }
    }

    if (cfg->num_routes > 0) {
        printf("  routing: %d\n", cfg->num_routes);
        for (int r = 0; r < cfg->num_routes; r++) {
            const RoutingConfig *rc = &cfg->routes[r];
            printf("    [%s] ", rc->name);
            switch (rc->source_type) {
            case ROUTE_PERSPECTIVE:
                printf("perspective=%d", rc->perspective);
                break;
            case ROUTE_DIVISION:
                printf("division=%s",
                       rc->division_index >= 0 ? cfg->divisions[rc->division_index].name : "?");
                break;
            case ROUTE_RANK:
                printf("rank=%s",
                       rc->rank_index >= 0 ? cfg->ranks[rc->rank_index].name : "?");
                break;
            }
            if (rc->channel > 0)
                printf(" ch=%d", rc->channel);
            if (rc->has_note_range)
                printf(" notes=%d-%d", rc->note_range[0], rc->note_range[1]);
            printf(" → outputs");
            for (int j = 0; j < rc->num_output_channels; j++)
                printf(" %d", rc->output_channels[j]);
            printf("\n");
        }
    }

    if (cfg->num_midi_devices > 0) {
        printf("  midi_devices: %d\n", cfg->num_midi_devices);
        for (int m = 0; m < cfg->num_midi_devices; m++) {
            const MidiDeviceConfig *md = &cfg->midi_devices[m];
            printf("    \"%s\" → midi_channel %d\n", md->name, md->midi_channel);
        }
    }
}
