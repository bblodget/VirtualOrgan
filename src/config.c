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

            val = toml_int_in(rank, "midi_channel");
            if (val.ok) rc->midi_channel = (int)val.u.i;

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
        printf("      midi_channel: %d\n", rc->midi_channel);
        printf("      output_channels:");
        for (int j = 0; j < rc->num_output_channels; j++)
            printf(" %d", rc->output_channels[j]);
        printf("\n");
    }
}
