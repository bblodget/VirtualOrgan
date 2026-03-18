#ifndef MIXER_H
#define MIXER_H

#include "voice.h"

/* Render all active voices into a stereo output buffer.
 * left and right must have room for nframes floats each. */
void mixer_render(VoicePool *pool, float *left, float *right, int nframes);

#endif
