#ifndef JACK_ENGINE_H
#define JACK_ENGINE_H

#include "ring_buffer.h"
#include "voice.h"
#include "sampler.h"
#include "config.h"

typedef struct {
    RingBuffer  *ring_buffer;
    VoicePool   *voice_pool;
    SampleBank  *sample_bank;
    OrganConfig *config;
} JackEngineCtx;

/* Start the JACK client. Returns 0 on success. */
int jack_engine_start(JackEngineCtx *ctx);

/* Stop and disconnect the JACK client. */
void jack_engine_stop(void);

#endif
