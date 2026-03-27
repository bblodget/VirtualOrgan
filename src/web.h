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

#ifndef WEB_H
#define WEB_H

#include "ring_buffer.h"
#include "config.h"

/* Start HTTP server on given port.
 * html_path is the path to the HTML file to serve.
 * Returns 0 on success, -1 on error. */
int web_start(int port, RingBuffer *rb, OrganConfig *config,
              const char *html_path);

/* Stop HTTP server and free resources. */
void web_stop(void);

#endif
