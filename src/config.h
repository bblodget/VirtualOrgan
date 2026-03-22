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
#include <stdbool.h>

#define MAX_RANKS          64
#define MAX_OUTPUT_CHANNELS 32
#define MAX_PATH_LEN       256
#define MAX_DIVISIONS      8
#define MAX_STOPS_PER_DIV  32
#define MAX_COUPLERS       16

typedef struct {
    char     sample_dir[MAX_PATH_LEN];
    char     filename_pattern[MAX_PATH_LEN];  /* e.g. "{note:03d}.wav" or "{note:03d}-{name}.wav" */
    int      output_channels[MAX_OUTPUT_CHANNELS];
    int      num_output_channels;
    char     name[64];
} RankConfig;

typedef struct {
    char     name[64];
    int      rank_index;    /* index into config.ranks[] */
    int      engage_cc;     /* MIDI CC to toggle this stop */
    bool     engaged;       /* runtime state, starts false */
} StopConfig;

typedef struct {
    char        name[64];
    int         midi_channel;              /* MIDI channel for this division */
    int         expression_cc;             /* CC for expression pedal (-1 = none) */
    float       expression_gain;           /* runtime: 0.0–1.0, default 1.0 */
    StopConfig  stops[MAX_STOPS_PER_DIV];
    int         num_stops;
} DivisionConfig;

typedef struct {
    char    name[64];
    int     from_division;  /* index into divisions[] — where keys are played */
    int     to_division;    /* index into divisions[] — additional pipes triggered */
    int     engage_cc;      /* MIDI CC to toggle this coupler */
    bool    engaged;        /* runtime state, starts false */
} CouplerConfig;

typedef struct {
    int             sample_rate;
    int             buffer_size;
    char            jack_client_name[64];
    RankConfig      ranks[MAX_RANKS];
    int             num_ranks;
    DivisionConfig  divisions[MAX_DIVISIONS];
    int             num_divisions;
    CouplerConfig   couplers[MAX_COUPLERS];
    int             num_couplers;
} OrganConfig;

/* Load config from TOML file. Returns 0 on success, -1 on error. */
int config_load(OrganConfig *cfg, const char *path);

/* Print config to stdout for debugging. */
void config_print(const OrganConfig *cfg);

#endif
