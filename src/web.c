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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include "web.h"
#include "mixer.h"

static struct MHD_Daemon *daemon_handle;
static RingBuffer *ring_buf;
static OrganConfig *organ_config;
static char *html_content;
static size_t html_length;

/* ---- HTML file loading ---- */

static int load_html_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "web: cannot open '%s'\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    html_content = malloc(size + 1);
    if (!html_content) {
        fclose(f);
        return -1;
    }
    size_t nread = fread(html_content, 1, size, f);
    (void)nread;
    html_content[size] = '\0';
    html_length = size;
    fclose(f);
    return 0;
}

/* ---- JSON state builder ---- */

static int build_state_json(char *buf, size_t bufsize)
{
    int off = 0;
    off += snprintf(buf + off, bufsize - off,
                    "{\"gain\":%.3f,\"divisions\":[", mixer_get_gain());

    for (int d = 0; d < organ_config->num_divisions; d++) {
        const DivisionConfig *dc = &organ_config->divisions[d];
        if (d > 0) off += snprintf(buf + off, bufsize - off, ",");
        off += snprintf(buf + off, bufsize - off,
                        "{\"name\":\"%s\",\"expression\":%.2f,\"stops\":[",
                        dc->name, dc->expression_gain);
        for (int s = 0; s < dc->num_stops; s++) {
            const StopConfig *sc = &dc->stops[s];
            if (s > 0) off += snprintf(buf + off, bufsize - off, ",");
            off += snprintf(buf + off, bufsize - off,
                            "{\"name\":\"%s\",\"engaged\":%s}",
                            sc->name, sc->engaged ? "true" : "false");
        }
        off += snprintf(buf + off, bufsize - off, "]}");
    }

    off += snprintf(buf + off, bufsize - off, "],\"couplers\":[");
    for (int c = 0; c < organ_config->num_couplers; c++) {
        const CouplerConfig *cc = &organ_config->couplers[c];
        if (c > 0) off += snprintf(buf + off, bufsize - off, ",");
        off += snprintf(buf + off, bufsize - off,
                        "{\"name\":\"%s\",\"engaged\":%s}",
                        cc->name, cc->engaged ? "true" : "false");
    }
    off += snprintf(buf + off, bufsize - off, "]}");
    return off;
}

/* ---- Simple JSON parsing helpers ---- */

static int json_get_int(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    return atoi(p);
}

static float json_get_float(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1.0f;
    p += strlen(search);
    while (*p == ' ') p++;
    return (float)atof(p);
}

/* ---- Preset logic ---- */

static void apply_preset_full_organ(void)
{
    for (int d = 0; d < organ_config->num_divisions; d++) {
        DivisionConfig *dc = &organ_config->divisions[d];
        for (int s = 0; s < dc->num_stops; s++) {
            MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                            (uint8_t)dc->stops[s].engage_cc, 127};
            ring_buffer_push(ring_buf, &ev);
        }
    }
}

static void apply_preset_all_off(void)
{
    for (int d = 0; d < organ_config->num_divisions; d++) {
        DivisionConfig *dc = &organ_config->divisions[d];
        for (int s = 0; s < dc->num_stops; s++) {
            MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                            (uint8_t)dc->stops[s].engage_cc, 0};
            ring_buffer_push(ring_buf, &ev);
        }
    }
}

static void apply_preset_quiet(void)
{
    apply_preset_all_off();
    /* Engage first stop of each division */
    for (int d = 0; d < organ_config->num_divisions; d++) {
        DivisionConfig *dc = &organ_config->divisions[d];
        if (dc->num_stops > 0) {
            MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                            (uint8_t)dc->stops[0].engage_cc, 127};
            ring_buffer_push(ring_buf, &ev);
        }
    }
}

/* ---- POST body accumulation ---- */

struct PostData {
    char buf[256];
    int len;
};

/* ---- HTTP request handler ---- */

static enum MHD_Result handle_request(
    void *cls, struct MHD_Connection *connection,
    const char *url, const char *method,
    const char *version, const char *upload_data,
    size_t *upload_data_size, void **con_cls)
{
    (void)cls;
    (void)version;

    /* GET requests */
    if (strcmp(method, "GET") == 0) {
        struct MHD_Response *response;
        enum MHD_Result ret;

        if (strcmp(url, "/") == 0 && html_content) {
            response = MHD_create_response_from_buffer(
                html_length, html_content, MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(response, "Content-Type", "text/html");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
            return ret;
        }

        if (strcmp(url, "/api/state") == 0) {
            char json[8192];
            int len = build_state_json(json, sizeof(json));
            response = MHD_create_response_from_buffer(
                len, json, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* 404 */
        const char *nf = "Not Found";
        response = MHD_create_response_from_buffer(
            strlen(nf), (void *)nf, MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* POST requests — accumulate body */
    if (strcmp(method, "POST") == 0) {
        struct PostData *pd = *con_cls;

        /* First call: allocate buffer */
        if (!pd) {
            pd = calloc(1, sizeof(struct PostData));
            *con_cls = pd;
            return MHD_YES;
        }

        /* Accumulate upload data */
        if (*upload_data_size > 0) {
            int space = sizeof(pd->buf) - pd->len - 1;
            size_t copy = (*upload_data_size < (size_t)space) ? *upload_data_size : (size_t)space;
            memcpy(pd->buf + pd->len, upload_data, copy);
            pd->len += copy;
            pd->buf[pd->len] = '\0';
            *upload_data_size = 0;
            return MHD_YES;
        }

        /* Final call: process the request */
        const char *body = pd->buf;
        struct MHD_Response *response;
        enum MHD_Result ret;
        const char *ok = "{\"ok\":true}";

        if (strcmp(url, "/api/stop/toggle") == 0) {
            int div_idx = json_get_int(body, "division");
            int stop_idx = json_get_int(body, "stop");
            if (div_idx >= 0 && div_idx < organ_config->num_divisions &&
                stop_idx >= 0 && stop_idx < organ_config->divisions[div_idx].num_stops) {
                DivisionConfig *dc = &organ_config->divisions[div_idx];
                StopConfig *sc = &dc->stops[stop_idx];
                bool new_state = !sc->engaged;
                uint8_t cc_val = new_state ? 127 : 0;
                MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                                (uint8_t)sc->engage_cc, cc_val};
                ring_buffer_push(ring_buf, &ev);
            }
        } else if (strcmp(url, "/api/coupler/toggle") == 0) {
            int idx = json_get_int(body, "coupler");
            if (idx >= 0 && idx < organ_config->num_couplers) {
                CouplerConfig *coup = &organ_config->couplers[idx];
                bool new_state = !coup->engaged;
                uint8_t cc_val = new_state ? 127 : 0;
                MidiEvent ev = {MIDI_CC, 1, (uint8_t)coup->engage_cc, cc_val};
                ring_buffer_push(ring_buf, &ev);
            }
        } else if (strcmp(url, "/api/gain") == 0) {
            float val = json_get_float(body, "value");
            if (val >= 0.0f)
                mixer_set_gain(val);
        } else if (strcmp(url, "/api/preset/full") == 0) {
            apply_preset_full_organ();
        } else if (strcmp(url, "/api/preset/quiet") == 0) {
            apply_preset_quiet();
        } else if (strcmp(url, "/api/preset/off") == 0) {
            apply_preset_all_off();
        }

        response = MHD_create_response_from_buffer(
            strlen(ok), (void *)ok, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        free(pd);
        *con_cls = NULL;
        return ret;
    }

    return MHD_NO;
}

/* ---- Public API ---- */

int web_start(int port, RingBuffer *rb, OrganConfig *config,
              const char *html_path)
{
    ring_buf = rb;
    organ_config = config;

    if (load_html_file(html_path) != 0)
        return -1;

    daemon_handle = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        port, NULL, NULL,
        &handle_request, NULL,
        MHD_OPTION_END);

    if (!daemon_handle) {
        fprintf(stderr, "web: cannot start HTTP server on port %d\n", port);
        free(html_content);
        html_content = NULL;
        return -1;
    }

    printf("web: listening on http://0.0.0.0:%d/\n", port);
    return 0;
}

void web_stop(void)
{
    if (daemon_handle) {
        MHD_stop_daemon(daemon_handle);
        daemon_handle = NULL;
    }
    free(html_content);
    html_content = NULL;
}
