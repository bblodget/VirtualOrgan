#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include "ring_buffer.h"

/* Start MIDI input thread. If fake_midi is true, generates test events
 * instead of reading from ALSA sequencer. Returns 0 on success. */
int midi_start(RingBuffer *rb, bool fake_midi);

/* Stop MIDI input thread and clean up. */
void midi_stop(void);

#endif
