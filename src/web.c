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
#include <pthread.h>
#include "mongoose.h"
#include "web.h"
#include "mixer.h"

static struct mg_mgr mgr;
static pthread_t web_thread;
static volatile int running;
static RingBuffer *ring_buf;
static OrganConfig *organ_config;
static char *html_content;
static size_t html_length;

/* Track connected WebSocket clients */
#define MAX_WS_CLIENTS 8
static struct mg_connection *ws_clients[MAX_WS_CLIENTS];
static int num_ws_clients;

/* State snapshot for change detection */
static char last_state[8192];

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
    if (!html_content) { fclose(f); return -1; }
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

static void apply_preset_full(int div_idx)
{
    if (div_idx < 0 || div_idx >= organ_config->num_divisions) return;
    DivisionConfig *dc = &organ_config->divisions[div_idx];
    for (int s = 0; s < dc->num_stops; s++) {
        MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                        (uint8_t)dc->stops[s].engage_cc, 127};
        ring_buffer_push(ring_buf, &ev);
    }
}

static void apply_preset_off(int div_idx)
{
    if (div_idx < 0 || div_idx >= organ_config->num_divisions) return;
    DivisionConfig *dc = &organ_config->divisions[div_idx];
    for (int s = 0; s < dc->num_stops; s++) {
        MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                        (uint8_t)dc->stops[s].engage_cc, 0};
        ring_buffer_push(ring_buf, &ev);
    }
}

static void apply_preset_quiet(int div_idx)
{
    if (div_idx < 0 || div_idx >= organ_config->num_divisions) return;
    apply_preset_off(div_idx);
    DivisionConfig *dc = &organ_config->divisions[div_idx];
    if (dc->num_stops > 0) {
        MidiEvent ev = {MIDI_CC, (uint8_t)dc->midi_channel,
                        (uint8_t)dc->stops[0].engage_cc, 127};
        ring_buffer_push(ring_buf, &ev);
    }
}

/* ---- WebSocket client management ---- */

static void ws_add_client(struct mg_connection *c)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!ws_clients[i]) {
            ws_clients[i] = c;
            num_ws_clients++;
            return;
        }
    }
}

static void ws_remove_client(struct mg_connection *c)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients[i] == c) {
            ws_clients[i] = NULL;
            num_ws_clients--;
            return;
        }
    }
}

static void ws_broadcast(const char *json, int len)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (ws_clients[i])
            mg_ws_send(ws_clients[i], json, len, WEBSOCKET_OP_TEXT);
    }
}

/* ---- Process command from HTTP POST or WebSocket message ---- */

static void process_command(const char *body, size_t len,
                            char *response, size_t rsize)
{
    /* Null-terminate for string operations */
    char buf[512];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, body, len);
    buf[len] = '\0';

    /* Determine action from "action" field */
    char action[64] = {0};
    const char *ap = strstr(buf, "\"action\":\"");
    if (ap) {
        ap += 10;
        const char *end = strchr(ap, '"');
        if (end && end - ap < 63) {
            memcpy(action, ap, end - ap);
            action[end - ap] = '\0';
        }
    }

    if (strcmp(action, "toggle_stop") == 0) {
        int div_idx = json_get_int(buf, "division");
        int stop_idx = json_get_int(buf, "stop");
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
    } else if (strcmp(action, "toggle_coupler") == 0) {
        int idx = json_get_int(buf, "coupler");
        if (idx >= 0 && idx < organ_config->num_couplers) {
            CouplerConfig *coup = &organ_config->couplers[idx];
            bool new_state = !coup->engaged;
            uint8_t cc_val = new_state ? 127 : 0;
            MidiEvent ev = {MIDI_CC, 1, (uint8_t)coup->engage_cc, cc_val};
            ring_buffer_push(ring_buf, &ev);
        }
    } else if (strcmp(action, "set_gain") == 0) {
        float val = json_get_float(buf, "value");
        if (val >= 0.0f)
            mixer_set_gain(val);
    } else if (strcmp(action, "set_expression") == 0) {
        int div_idx = json_get_int(buf, "division");
        float val = json_get_float(buf, "value");
        if (div_idx >= 0 && div_idx < organ_config->num_divisions && val >= 0.0f) {
            if (val > 1.0f) val = 1.0f;
            organ_config->divisions[div_idx].expression_gain = val;
        }
    } else if (strcmp(action, "preset_full") == 0) {
        apply_preset_full(json_get_int(buf, "division"));
    } else if (strcmp(action, "preset_quiet") == 0) {
        apply_preset_quiet(json_get_int(buf, "division"));
    } else if (strcmp(action, "preset_off") == 0) {
        apply_preset_off(json_get_int(buf, "division"));
    }

    if (response)
        snprintf(response, rsize, "{\"ok\":true}");
}

/* ---- Mongoose event handler ---- */

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
            /* Upgrade to WebSocket */
            mg_ws_upgrade(c, hm, NULL);
            ws_add_client(c);
            /* Send current state immediately */
            char json[8192];
            int len = build_state_json(json, sizeof(json));
            mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);

        } else if (mg_match(hm->uri, mg_str("/"), NULL)) {
            /* Serve HTML */
            mg_http_reply(c, 200, "Content-Type: text/html\r\n",
                          "%.*s", (int)html_length, html_content);

        } else if (mg_match(hm->uri, mg_str("/api/state"), NULL)) {
            /* JSON state */
            char json[8192];
            int len = build_state_json(json, sizeof(json));
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "%.*s", len, json);

        } else if (mg_match(hm->uri, mg_str("/api/#"), NULL) &&
                   mg_strcmp(hm->method, mg_str("POST")) == 0) {
            /* Legacy HTTP POST API — convert to unified command */
            char body[512];
            int blen = hm->body.len < sizeof(body) - 1 ? hm->body.len : sizeof(body) - 1;
            memcpy(body, hm->body.buf, blen);
            body[blen] = '\0';

            /* Map URL to action */
            char cmd[512];
            if (mg_match(hm->uri, mg_str("/api/stop/toggle"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"toggle_stop\",%s", body + 1);
            else if (mg_match(hm->uri, mg_str("/api/coupler/toggle"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"toggle_coupler\",%s", body + 1);
            else if (mg_match(hm->uri, mg_str("/api/gain"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"set_gain\",%s", body + 1);
            else if (mg_match(hm->uri, mg_str("/api/preset/full"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"preset_full\",%s", body + 1);
            else if (mg_match(hm->uri, mg_str("/api/preset/quiet"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"preset_quiet\",%s", body + 1);
            else if (mg_match(hm->uri, mg_str("/api/preset/off"), NULL))
                snprintf(cmd, sizeof(cmd), "{\"action\":\"preset_off\",%s", body + 1);
            else
                cmd[0] = '\0';

            char resp[64];
            if (cmd[0])
                process_command(cmd, strlen(cmd), resp, sizeof(resp));
            else
                snprintf(resp, sizeof(resp), "{\"error\":\"unknown\"}");

            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "%s", resp);

        } else {
            mg_http_reply(c, 404, "", "Not Found");
        }

    } else if (ev == MG_EV_WS_MSG) {
        /* WebSocket message from client */
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        process_command(wm->data.buf, wm->data.len, NULL, 0);

    } else if (ev == MG_EV_CLOSE) {
        ws_remove_client(c);
    }
}

/* ---- Web server thread ---- */

static void *web_thread_fn(void *arg)
{
    (void)arg;

    while (running) {
        mg_mgr_poll(&mgr, 50);  /* 50ms poll interval */

        /* Check for state changes and broadcast to WebSocket clients */
        if (num_ws_clients > 0) {
            char json[8192];
            int len = build_state_json(json, sizeof(json));
            if (len != (int)strlen(last_state) ||
                memcmp(json, last_state, len) != 0) {
                ws_broadcast(json, len);
                memcpy(last_state, json, len);
                last_state[len] = '\0';
            }
        }
    }

    return NULL;
}

/* ---- Public API ---- */

int web_start(int port, RingBuffer *rb, OrganConfig *config,
              const char *html_path)
{
    ring_buf = rb;
    organ_config = config;

    if (load_html_file(html_path) != 0)
        return -1;

    mg_mgr_init(&mgr);

    char url[64];
    snprintf(url, sizeof(url), "http://0.0.0.0:%d", port);

    if (!mg_http_listen(&mgr, url, ev_handler, NULL)) {
        fprintf(stderr, "web: cannot listen on %s\n", url);
        free(html_content);
        html_content = NULL;
        return -1;
    }

    memset(ws_clients, 0, sizeof(ws_clients));
    num_ws_clients = 0;
    last_state[0] = '\0';
    running = 1;

    if (pthread_create(&web_thread, NULL, web_thread_fn, NULL) != 0) {
        fprintf(stderr, "web: cannot create thread\n");
        mg_mgr_free(&mgr);
        free(html_content);
        html_content = NULL;
        return -1;
    }

    printf("web: listening on %s\n", url);
    return 0;
}

void web_stop(void)
{
    if (running) {
        running = 0;
        pthread_join(web_thread, NULL);
    }
    mg_mgr_free(&mgr);
    free(html_content);
    html_content = NULL;
}
