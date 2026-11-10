#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_ring.h"
#include "pt_turn.h"

typedef enum {
    PT_APP_EV_WIFI_UP,
    PT_APP_EV_WIFI_DOWN,
    PT_APP_EV_SETTINGS,
    PT_APP_EV_WAKE,
    PT_APP_EV_BUTTON,
    PT_APP_EV_BUTTON_LONG,
    PT_APP_EV_DEV
} pt_app_ev_kind_t;

typedef struct {
    pt_app_ev_kind_t kind;
    pt_dev_event_t dev;
    bool follow_up;
    const char *code;
} pt_app_ev_t;

typedef struct {
    pt_ring_state_t ring;
    bool wake_listening;
    bool start_convo;
    bool stop_convo;
    bool play_wake_sound;
    bool play_end_sound;
    bool reset_password;
} pt_app_out_t;

typedef struct pt_appstate pt_appstate_t;

pt_appstate_t *pt_appstate_new(void);
void pt_appstate_free(pt_appstate_t *a);
void pt_appstate_settings(pt_appstate_t *a, bool has_key);
pt_app_out_t pt_appstate_on(pt_appstate_t *a, const pt_app_ev_t *ev);
pt_ring_state_t pt_appstate_ring(const pt_appstate_t *a);
bool pt_appstate_ready(const pt_appstate_t *a);
bool pt_appstate_in_convo(const pt_appstate_t *a);

typedef struct {
    size_t (*push)(void *ctx, const int16_t *pcm, size_t n);
    bool (*wait)(void *ctx);
    void *ctx;
} pt_app_sink_t;

size_t pt_app_feed(const pt_app_sink_t *sink, const int16_t *pcm, size_t n);
