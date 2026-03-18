#include "ring_buffer.h"

void ring_buffer_init(RingBuffer *rb)
{
    atomic_store(&rb->write_pos, 0);
    atomic_store(&rb->read_pos, 0);
}

bool ring_buffer_push(RingBuffer *rb, const MidiEvent *event)
{
    unsigned w = atomic_load(&rb->write_pos);
    unsigned r = atomic_load(&rb->read_pos);
    unsigned next = (w + 1) & (RING_BUFFER_SIZE - 1);

    if (next == r)
        return false;  /* full */

    rb->events[w] = *event;
    atomic_store(&rb->write_pos, next);
    return true;
}

bool ring_buffer_pop(RingBuffer *rb, MidiEvent *event)
{
    unsigned r = atomic_load(&rb->read_pos);
    unsigned w = atomic_load(&rb->write_pos);

    if (r == w)
        return false;  /* empty */

    *event = rb->events[r];
    atomic_store(&rb->read_pos, (r + 1) & (RING_BUFFER_SIZE - 1));
    return true;
}
