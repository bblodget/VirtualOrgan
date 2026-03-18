#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define MAX_RANKS          64
#define MAX_OUTPUT_CHANNELS 32
#define MAX_PATH_LEN       256

typedef struct {
    char     sample_dir[MAX_PATH_LEN];
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
