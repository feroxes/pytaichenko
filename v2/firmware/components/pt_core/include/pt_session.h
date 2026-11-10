#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_log.h"
#include "pt_rt.h"
#include "pt_settings.h"
#include "pt_turn.h"

#define PT_SESSION_CONNECT_TIMEOUT_MS 10000u
#define PT_DEADLINE_NONE UINT32_MAX

typedef struct {
    void (*ready)(void *ctx, uint32_t now_ms);
    void (*failed)(void *ctx, const char *why, uint32_t now_ms);
    void (*event)(void *ctx, const pt_rt_event_t *e, uint32_t now_ms);
    void (*closed)(void *ctx, uint32_t now_ms);
    void (*resume)(void *ctx, uint32_t now_ms);
    void (*forget)(void *ctx);
    void *ctx;
} pt_session_hooks_t;

typedef struct pt_session pt_session_t;

bool pt_time_due(uint32_t at, uint32_t now_ms);
bool pt_time_before(uint32_t a, uint32_t b);
uint32_t pt_time_ms(float seconds);
uint32_t pt_time_after(uint32_t now_ms, uint32_t ms);

pt_session_t *pt_session_new(const pt_port_t *port, pt_log_t *log, const pt_session_hooks_t *hooks);
void pt_session_free(pt_session_t *s);
void pt_session_settings(pt_session_t *s, const pt_settings_t *settings);
bool pt_session_has_settings(const pt_session_t *s);
const pt_settings_t *pt_session_fresh(const pt_session_t *s);
const pt_timing_t *pt_session_timing(const pt_session_t *s);
const pt_timing_t *pt_session_fresh_timing(const pt_session_t *s);
uint32_t pt_session_gen(const pt_session_t *s);
void pt_session_for_turn(pt_session_t *s, bool cancel_previous, uint32_t now_ms);
void pt_session_abort(pt_session_t *s);
bool pt_session_send(pt_session_t *s, uint32_t gen, const char *json);
void pt_session_release(pt_session_t *s, uint32_t gen, bool cancel, bool clear, bool drop, uint32_t now_ms);
void pt_session_detach(pt_session_t *s);
void pt_session_close(pt_session_t *s);
void pt_session_cancel_idle(pt_session_t *s);
void pt_session_close_when_idle(pt_session_t *s, float delay_s, uint32_t now_ms);
bool pt_session_hold(pt_session_t *s, uint32_t until_ms);
void pt_session_rt_opened(pt_session_t *s, uint32_t now_ms);
void pt_session_rt_failed(pt_session_t *s, const char *why, uint32_t now_ms);
void pt_session_rt_message(pt_session_t *s, const char *json, size_t len, uint32_t now_ms);
void pt_session_rt_closed(pt_session_t *s, uint32_t now_ms);
bool pt_session_tick_one(pt_session_t *s, uint32_t now_ms);
uint32_t pt_session_next_deadline(const pt_session_t *s);
