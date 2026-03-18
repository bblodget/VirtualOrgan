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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define MAX_RANKS          64
#define MAX_OUTPUT_CHANNELS 32
#define MAX_PATH_LEN       256

typedef struct {
    char     sample_dir[MAX_PATH_LEN];
    char     filename_pattern[MAX_PATH_LEN];  /* e.g. "{note:03d}.wav" or "{note:03d}-{name}.wav" */
    int      midi_channel;
    int      output_channels[MAX_OUTPUT_CHANNELS];
    int      num_output_channels;
    char     name[64];
} RankConfig;

typedef struct {
    int         sample_rate;
    int         buffer_size;
    char        jack_client_name[64];
    RankConfig  ranks[MAX_RANKS];
    int         num_ranks;
} OrganConfig;

/* Load config from TOML file. Returns 0 on success, -1 on error. */
int config_load(OrganConfig *cfg, const char *path);

/* Print config to stdout for debugging. */
void config_print(const OrganConfig *cfg);

#endif
