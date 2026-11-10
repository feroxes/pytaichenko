#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_log.h"
#include "pt_settings.h"

#define PT_CONVO_MIC_RATE 16000
#define PT_CONVO_RT_RATE 24000
#define PT_CONVO_OUT_RATE 24000

typedef enum {
    PT_DEV_LISTENING,
    PT_DEV_SPEECH_STARTED,
    PT_DEV_SPEECH_STOPPED,
    PT_DEV_ANSWER_STREAMING,
    PT_DEV_ANSWERED,
    PT_DEV_ENDED,
    PT_DEV_FAILED
} pt_dev_event_t;

typedef struct {
    void (*rt_open)(void *ctx, const pt_settings_t *s);
    void (*rt_send)(void *ctx, const char *json);
    void (*rt_close)(void *ctx);
    void (*play)(void *ctx, const int16_t *pcm, size_t n);
    void (*play_end)(void *ctx);
    void (*device)(void *ctx, pt_dev_event_t ev, bool follow_up, const char *code);
    void (*set_volume)(void *ctx, float volume);
    void *ctx;
} pt_port_t;

typedef struct pt_convo pt_convo_t;

pt_convo_t *pt_convo_new(const pt_port_t *port, pt_log_t *log);
void pt_convo_free(pt_convo_t *c);
void pt_convo_settings(pt_convo_t *c, const pt_settings_t *s, uint32_t now_ms);
void pt_convo_start(pt_convo_t *c, uint32_t now_ms);
void pt_convo_stop(pt_convo_t *c, uint32_t now_ms);
void pt_convo_mic(pt_convo_t *c, const int16_t *pcm, size_t n, uint32_t now_ms);
void pt_convo_rt_opened(pt_convo_t *c, uint32_t now_ms);
void pt_convo_rt_failed(pt_convo_t *c, const char *why, uint32_t now_ms);
void pt_convo_rt_message(pt_convo_t *c, const char *json, size_t len, uint32_t now_ms);
void pt_convo_rt_closed(pt_convo_t *c, uint32_t now_ms);
void pt_convo_rt_overflow(pt_convo_t *c, uint32_t now_ms);
bool pt_convo_close_idle(pt_convo_t *c, uint32_t now_ms);
void pt_convo_tick(pt_convo_t *c, uint32_t now_ms);
uint32_t pt_convo_next_deadline(const pt_convo_t *c);
bool pt_convo_follow_up_pending(const pt_convo_t *c);
