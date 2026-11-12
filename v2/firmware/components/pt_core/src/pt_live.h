#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_dsp.h"
#include "pt_log.h"
#include "pt_rt.h"
#include "pt_session.h"
#include "pt_tools.h"
#include "pt_turn.h"

typedef struct {
    bool on;
    uint32_t at;
} pt_slot_t;

typedef struct {
    pt_port_t port;
    pt_log_t *log;
    pt_session_t *session;
    float volume;
    int16_t *scratch;
    size_t scratch_cap;
} pt_convo_io_t;

typedef struct {
    int16_t *buf;
    size_t size;
    size_t head;
    size_t len;
    size_t cap;
    uint64_t dropped;
} pt_early_t;

typedef bool (*pt_early_send_fn)(void *ctx, const int16_t *pcm, size_t n, uint32_t now_ms);

void pt_slot_arm(pt_slot_t *t, uint32_t now_ms, float seconds);
int16_t *pt_io_scratch(pt_convo_io_t *io, size_t n);
void pt_io_device(pt_convo_io_t *io, pt_dev_event_t ev, bool follow_up, const char *code);
size_t pt_io_play(pt_convo_io_t *io, pt_speaker_path_t *speaker, const int16_t *pcm, size_t n);
size_t pt_io_flush(pt_convo_io_t *io, pt_speaker_path_t *speaker);
char *pt_io_tool(pt_convo_io_t *io, const pt_tool_call_t *call, uint32_t now_ms);

void pt_early_init(pt_early_t *e, float first_turn_s);
void pt_early_push(pt_early_t *e, const int16_t *x, size_t n);
bool pt_early_flush(pt_early_t *e, pt_log_t *log, uint32_t now_ms, pt_early_send_fn send, void *ctx);
void pt_early_free(pt_early_t *e);

typedef struct pt_live pt_live_t;

pt_live_t *pt_live_new(pt_convo_io_t *io);
void pt_live_free(pt_live_t *l);
void pt_live_reset(pt_live_t *l, uint32_t now_ms);
void pt_live_start(pt_live_t *l, uint32_t now_ms);
void pt_live_stop(pt_live_t *l, uint32_t now_ms);
void pt_live_mic(pt_live_t *l, const int16_t *pcm, size_t n, uint32_t now_ms);
void pt_live_overflow(pt_live_t *l, uint32_t now_ms);
bool pt_live_close_idle(pt_live_t *l, uint32_t now_ms);
uint32_t pt_live_next_deadline(pt_live_t *l);
void pt_live_fire(pt_live_t *l, uint32_t now_ms);
bool pt_live_follow_up_pending(const pt_live_t *l);
void pt_live_on_ready(pt_live_t *l, uint32_t now_ms);
void pt_live_on_failed(pt_live_t *l, const char *why, uint32_t now_ms);
void pt_live_on_event(pt_live_t *l, const pt_rt_event_t *e, uint32_t now_ms);
void pt_live_on_closed(pt_live_t *l, uint32_t now_ms);
