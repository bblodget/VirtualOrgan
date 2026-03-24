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

#ifndef CONSOLE_H
#define CONSOLE_H

#include "ring_buffer.h"
#include "config.h"

/* Start terminal console for stop/gain control.
 * Puts terminal in raw mode, reads keypresses in a thread.
 * Returns 0 on success. */
int console_start(RingBuffer *rb, OrganConfig *config);

/* Stop console thread and restore terminal. */
void console_stop(void);

/* Returns true if user pressed 'q' or Esc to quit. */
bool console_quit_requested(void);

#endif
