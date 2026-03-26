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
    char     name[64];
    int      num_perspectives;  /* default 1; channels_per_perspective = sample_channels / num_perspectives */
} RankConfig;

#define MAX_ROUTES 32

typedef enum {
    ROUTE_PERSPECTIVE,  /* source = { perspective = N } */
    ROUTE_DIVISION,     /* source = { division = "name" } */
    ROUTE_RANK,         /* source = { rank = "name" } */
} RouteSourceType;

typedef struct {
    char            name[64];
    RouteSourceType source_type;
    int             perspective;        /* for ROUTE_PERSPECTIVE: 1-indexed */
    int             division_index;     /* for ROUTE_DIVISION: index into divisions[] */
    int             rank_index;         /* for ROUTE_RANK: index into ranks[] */
    int             note_range[2];      /* [low, high] MIDI note range (0 = unused) */
    bool            has_note_range;
    int             output_channels[MAX_OUTPUT_CHANNELS];
    int             num_output_channels;
} RoutingConfig;

#define MAX_RANKS_PER_STOP 8

typedef struct {
    char     name[64];
    int      rank_indices[MAX_RANKS_PER_STOP]; /* indices into config.ranks[] */
    int      num_ranks;                        /* 1 for normal stops, >1 for multi-rank */
    int      engage_cc;     /* MIDI CC to toggle this stop */
    bool     engaged;       /* runtime state, starts false */
} StopConfig;

typedef struct {
    char        name[64];
    int         midi_channel;              /* MIDI channel for this division */
    int         expression_cc;             /* CC for expression pedal (-1 = none) */
    float       expression_gain;           /* runtime: 0.0–1.0, default 1.0 */
    int         note_range[2];             /* [low, high] MIDI note range (optional) */
    bool        has_note_range;
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

#define MAX_MIDI_DEVICES 8

typedef struct {
    char    name[64];       /* ALSA client name to match */
    int     channel;        /* MIDI channel to remap to (1-indexed) */
} MidiDeviceConfig;

typedef struct {
    int             sample_rate;
    int             buffer_size;
    int             num_outputs;    /* JACK output ports (default 2) */
    int             release_fade_ms; /* release fade-out duration in ms (default 250) */
    char            jack_client_name[64];
    RankConfig      ranks[MAX_RANKS];
    int             num_ranks;
    DivisionConfig  divisions[MAX_DIVISIONS];
    int             num_divisions;
    CouplerConfig   couplers[MAX_COUPLERS];
    int             num_couplers;
    RoutingConfig   routes[MAX_ROUTES];
    int             num_routes;
    MidiDeviceConfig midi_devices[MAX_MIDI_DEVICES];
    int             num_midi_devices;
} OrganConfig;

/* Load config from TOML file. Returns 0 on success, -1 on error. */
int config_load(OrganConfig *cfg, const char *path);

/* Reload config from disk into a live config.
 * Re-parses the TOML and copies safe-to-change fields (divisions,
 * couplers, routing, midi_devices). Ranks are NOT reloaded.
 * Returns 0 on success, -1 on error. */
int config_reload(OrganConfig *cfg, const char *path);

/* Print config to stdout for debugging. */
void config_print(const OrganConfig *cfg);

#endif
