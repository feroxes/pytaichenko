#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"
#include "pt_settings.h"
#include "pt_tools.h"

typedef enum {
    PT_RT_IGNORED,
    PT_RT_SESSION_READY,
    PT_RT_SPEECH_STARTED,
    PT_RT_SPEECH_STOPPED,
    PT_RT_RESPONSE_CREATED,
    PT_RT_AUDIO_DELTA,
    PT_RT_TRANSCRIPT_DONE,
    PT_RT_RESPONSE_DONE,
    PT_RT_INPUT_CLEARED,
    PT_RT_INPUT_TRANSCRIPT,
    PT_RT_ERROR,
    PT_RT_LIVE_AUDIO,
    PT_RT_INPUT_DELTA,
    PT_RT_OUTPUT_DELTA,
    PT_RT_TOOL_CALL,
    PT_RT_LIVE_CLOSED
} pt_rt_kind_t;

typedef struct {
    pt_rt_kind_t kind;
    char response_id[64];
    char status[16];
    char *text;
    int16_t *pcm;
    size_t pcm_len;
    pt_tool_call_t *calls;
    size_t n_calls;
    char model[64];
    char voice[32];
} pt_rt_event_t;

typedef struct {
    const char *role;
    const char *text;
} pt_rt_msg_t;

#define PT_LIVE_HELPER_MODEL "gpt-6-luna"
extern const char PT_LIVE_SUFFIX[];

typedef void (*pt_rt_send_fn)(void *ctx, const char *json);

char *pt_rt_session_update(const pt_settings_t *s);
char *pt_rt_append_audio(const int16_t *pcm, size_t n);
const char *pt_rt_commit(void);
const char *pt_rt_response_create(void);
const char *pt_rt_clear(void);
const char *pt_rt_cancel(void);
void pt_rt_tool_outputs(const pt_tool_call_t *calls, const char *const *outputs, size_t n, bool respond,
                        pt_rt_send_fn send, void *ctx);
pt_err_t pt_rt_parse(const char *json, size_t len, pt_rt_event_t *out);
void pt_rt_event_free(pt_rt_event_t *e);

bool pt_rt_is_live(const char *model);
char *pt_rt_live_start(const pt_settings_t *s, const pt_rt_msg_t *history, size_t n, bool with_helper);
char *pt_rt_live_append(const int16_t *pcm, size_t n);
void pt_rt_live_tool_output(const pt_tool_call_t *call, const char *output, bool respond, pt_rt_send_fn send, void *ctx);
const char *pt_rt_live_close(void);
