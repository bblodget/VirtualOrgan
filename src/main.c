#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"
#include "sampler.h"
#include "voice.h"
#include "ring_buffer.h"
#include "midi.h"
#include "jack_engine.h"

static volatile int quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    quit = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <config.toml> [--fake-midi]\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *config_path = argv[1];
    int fake_midi = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--fake-midi") == 0)
            fake_midi = 1;
    }

    /* Load config */
    OrganConfig config;
    if (config_load(&config, config_path) != 0)
        return 1;
    config_print(&config);

    /* Load samples from first rank */
    SampleBank bank;
    if (config.num_ranks == 0) {
        fprintf(stderr, "error: no ranks defined in config\n");
        return 1;
    }

    printf("\nLoading samples for rank '%s'...\n", config.ranks[0].name);
    if (sampler_load(&bank, config.ranks[0].sample_dir, config.ranks[0].filename_pattern) < 0)
        return 1;

    /* Initialize voice pool and ring buffer */
    VoicePool voice_pool;
    voice_pool_init(&voice_pool);

    RingBuffer ring_buffer;
    ring_buffer_init(&ring_buffer);

    /* Start MIDI input */
    printf("\nStarting MIDI input%s...\n", fake_midi ? " (fake mode)" : "");
    if (midi_start(&ring_buffer, fake_midi) != 0) {
        fprintf(stderr, "error: cannot start MIDI thread\n");
        sampler_free(&bank);
        return 1;
    }

    /* Start JACK engine */
    printf("Starting JACK engine...\n");
    JackEngineCtx ctx = {
        .ring_buffer  = &ring_buffer,
        .voice_pool   = &voice_pool,
        .sample_bank  = &bank,
        .config       = &config,
    };

    if (jack_engine_start(&ctx) != 0) {
        fprintf(stderr, "error: cannot start JACK engine\n");
        midi_stop();
        sampler_free(&bank);
        return 1;
    }

    /* Wait for signal */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\nOrgan engine running. Press Ctrl+C to stop.\n\n");

    while (!quit) {
        printf("\rVoices: %d  ", voice_pool.active_count);
        fflush(stdout);
        usleep(100000);  /* update display 10x/sec */
    }

    printf("\n\nShutting down...\n");

    jack_engine_stop();
    midi_stop();
    sampler_free(&bank);

    printf("Done.\n");
    return 0;
}
