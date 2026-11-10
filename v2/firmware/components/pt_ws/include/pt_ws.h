#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"
#include "pt_settings.h"
#include "pt_wsurl.h"

#define PT_WS_MAX_MESSAGE (1024u * 1024u)
#define PT_WS_SEND_QUEUE 64
#define PT_WS_PROBE_MS 10000u

typedef struct {
    void (*opened)(void *ctx);
    void (*message)(void *ctx, char *json, size_t len);
    void (*closed)(void *ctx, pt_ws_status_t why);
    void *ctx;
} pt_ws_events_t;

pt_err_t pt_ws_start(const pt_ws_events_t *ev);
void pt_ws_open(const char *api_key, const char *model);
bool pt_ws_send(const char *json);
void pt_ws_close(void);
pt_ws_status_t pt_ws_probe(const pt_settings_t *s, uint32_t timeout_ms, char *model, size_t model_len, char *voice,
                           size_t voice_len, char *error, size_t error_len);
