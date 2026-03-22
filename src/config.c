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

            val = toml_string_in(rank, "sample_dir");
            if (val.ok) {
                strncpy(rc->sample_dir, val.u.s, sizeof(rc->sample_dir) - 1);
                free(val.u.s);
            }

            val = toml_string_in(rank, "filename_pattern");
            if (val.ok) {
                strncpy(rc->filename_pattern, val.u.s, sizeof(rc->filename_pattern) - 1);
                free(val.u.s);
            } else {
                strncpy(rc->filename_pattern, "{note:03d}.wav", sizeof(rc->filename_pattern) - 1);
            }

            toml_array_t *channels = toml_array_in(rank, "output_channels");
            if (channels) {
                int nc = toml_array_nelem(channels);
                for (int j = 0; j < nc && j < MAX_OUTPUT_CHANNELS; j++) {
                    toml_datum_t ch = toml_int_at(channels, j);
                    if (ch.ok) {
                        rc->output_channels[rc->num_output_channels++] = (int)ch.u.i;
                    }
                }
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
                    sc->rank_index = -1;

                    val = toml_string_in(stop, "rank");
                    if (val.ok) {
                        for (int r = 0; r < cfg->num_ranks; r++) {
                            if (strcmp(cfg->ranks[r].name, val.u.s) == 0) {
                                sc->rank_index = r;
                                break;
                            }
                        }
                        if (sc->rank_index < 0)
                            fprintf(stderr, "config: stop '%s' references unknown rank '%s'\n",
                                    stop_key, val.u.s);
                        free(val.u.s);
                    }

                    val = toml_int_in(stop, "engage_cc");
                    if (val.ok) sc->engage_cc = (int)val.u.i;

                    dc->num_stops++;
                }
            }

            cfg->num_divisions++;
        }
    }

    toml_free(root);
    return 0;
}

void config_print(const OrganConfig *cfg)
{
    printf("Config:\n");
    printf("  sample_rate: %d\n", cfg->sample_rate);
    printf("  buffer_size: %d\n", cfg->buffer_size);
    printf("  jack_client: %s\n", cfg->jack_client_name);
    printf("  ranks: %d\n", cfg->num_ranks);

    for (int i = 0; i < cfg->num_ranks; i++) {
        const RankConfig *rc = &cfg->ranks[i];
        printf("    [%s]\n", rc->name);
        printf("      sample_dir: %s\n", rc->sample_dir);
        printf("      filename_pattern: %s\n", rc->filename_pattern);
        printf("      output_channels:");
        for (int j = 0; j < rc->num_output_channels; j++)
            printf(" %d", rc->output_channels[j]);
        printf("\n");
    }

    if (cfg->num_divisions > 0) {
        printf("  divisions: %d\n", cfg->num_divisions);
        for (int d = 0; d < cfg->num_divisions; d++) {
            const DivisionConfig *dc = &cfg->divisions[d];
            printf("    [%s] midi_channel=%d", dc->name, dc->midi_channel);
            if (dc->expression_cc >= 0)
                printf(" expression_cc=%d", dc->expression_cc);
            printf("\n");
            for (int s = 0; s < dc->num_stops; s++) {
                const StopConfig *sc = &dc->stops[s];
                printf("      %s → %s (cc=%d)\n", sc->name,
                       sc->rank_index >= 0 ? cfg->ranks[sc->rank_index].name : "?",
                       sc->engage_cc);
            }
        }
    }
}
