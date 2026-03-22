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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "ring_buffer.h"
#include "config.h"

/* Start keyboard MIDI input using SDL2 for key-down/key-up events.
 * Opens a small SDL window that must have focus for input.
 * Returns 0 on success, -1 on error. */
int keyboard_start(RingBuffer *rb, const OrganConfig *config);

/* Stop keyboard input, close SDL, and clean up. */
void keyboard_stop(void);

/* Returns true if the SDL window was closed or Escape pressed. */
bool keyboard_quit_requested(void);

#endif
