#include "pt_turn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pt_dsp.h"
#include "pt_rt.h"
#include "pt_session.h"
#include "pt_tools.h"

#define UNAVAILABLE "openai-unavailable"
#define START_FAILED "start-failed"
#define EARLY_CHUNK 4800
#define EARLY_FIRST 4800
#define FLUSH_CAP 4096
#define EARLY_MAX_S 30.0f

typedef enum { PH_LISTENING, PH_SPEAKING, PH_ANSWERING } phase_t;
typedef enum { ARM_LISTEN, ARM_SPEECH, ARM_RESPONSE } arm_t;

typedef struct {
    bool on;
    uint32_t at;
} timer_slot_t;

typedef struct {
    bool active;
    bool closed;
    bool ready;
    bool stopped;
    bool streaming;
    bool play_ended;
    bool follow_up;
    int follow_ups;
    phase_t phase;
    uint32_t gen;
    pt_mic_path_t *mic;
    pt_speaker_path_t *speaker;
    char response_id[64];
    bool vad_set;
    uint32_t vad_end;
    arm_t arm_kind;
    timer_slot_t arm;
    timer_slot_t later;
    timer_slot_t end;
    pt_dev_event_t end_event;
    bool end_follow_up;
    char end_code[32];
    int16_t *pending;
    size_t pending_n;
    bool pending_stream;
    uint64_t played;
    int16_t *early;
    size_t early_size;
    size_t early_head;
    size_t early_len;
    size_t early_cap;
    uint64_t early_dropped;
} turn_t;

struct pt_convo {
    pt_port_t port;
    pt_log_t *log;
    pt_session_t *session;
    float volume;
    turn_t turn;
    bool follow_up;
    int follow_ups;
    int16_t *scratch;
    size_t scratch_cap;
};

static uint32_t log_time(uint32_t now_ms)
{
    return now_ms / 1000u;
}

static void arm_slot(timer_slot_t *t, uint32_t now_ms, float seconds)
{
    t->on = true;
    t->at = pt_time_after(now_ms, pt_time_ms(seconds));
}

static int16_t *scratch(pt_convo_t *c, size_t n)
{
    if (n > c->scratch_cap) {
        int16_t *buf = realloc(c->scratch, n * sizeof(int16_t));
        if (buf == NULL) {
            return NULL;
        }
        c->scratch = buf;
        c->scratch_cap = n;
    }
    return c->scratch;
}

static void device(pt_convo_t *c, pt_dev_event_t ev, bool follow_up, const char *code)
{
    if (c->port.device != NULL) {
        c->port.device(c->port.ctx, ev, follow_up, code);
    }
}

static void send_if_current(pt_convo_t *c, pt_dev_event_t ev)
{
    if (c->turn.active) {
        device(c, ev, false, NULL);
    }
}

static void finish_stream(pt_convo_t *c)
{
    turn_t *t = &c->turn;
    if (!t->play_ended) {
        t->play_ended = true;
        if (c->port.play_end != NULL) {
            c->port.play_end(c->port.ctx);
        }
    }
}

static void turn_release(turn_t *t)
{
    pt_mic_path_free(t->mic);
    pt_speaker_path_free(t->speaker);
    free(t->pending);
    free(t->early);
    memset(t, 0, sizeof(*t));
}

static void abandon(pt_convo_t *c, const char *reason, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    if (!t->active) {
        return;
    }
    if (!t->closed) {
        t->closed = true;
        pt_log_add(c->log, log_time(now_ms), "turn ended: %s", reason);
    }
    pt_session_abort(c->session);
    finish_stream(c);
    turn_release(t);
}

static void finish_end(pt_convo_t *c, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    t->end.on = false;
    if (!t->active) {
        return;
    }
    bool follow_up = t->end_follow_up && !t->stopped;
    pt_dev_event_t ev = t->end_event;
    char code[sizeof(t->end_code)];
    snprintf(code, sizeof(code), "%s", t->end_code);
    uint64_t played = t->played;
    turn_release(t);
    device(c, ev, follow_up, code[0] != '\0' ? code : NULL);
    c->follow_up = follow_up;
    const pt_timing_t *timing = pt_session_timing(c->session);
    float idle = timing->session_idle_s + (float)((double)played / PT_CONVO_OUT_RATE);
    pt_session_close_when_idle(c->session, idle, now_ms);
}

static void end_turn(pt_convo_t *c, const char *reason, pt_dev_event_t ev, const char *code, bool follow_up, bool cancel,
                     bool clear, bool drop, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    if (!t->active || t->closed) {
        return;
    }
    t->closed = true;
    t->arm.on = false;
    t->later.on = false;
    pt_session_abort(c->session);
    finish_stream(c);
    pt_log_add(c->log, log_time(now_ms), "turn ended: %s", reason);
    char summary[96];
    pt_mic_path_summary(t->mic, summary, sizeof(summary));
    pt_log_add(c->log, log_time(now_ms), "device audio: %s", summary);
    pt_session_release(c->session, t->gen, cancel, clear, drop, now_ms);
    t->end_event = ev;
    t->end_follow_up = follow_up;
    snprintf(t->end_code, sizeof(t->end_code), "%s", code != NULL ? code : "");
    uint32_t at = now_ms;
    if (t->vad_set) {
        at = pt_time_after(t->vad_end, pt_time_ms(pt_session_timing(c->session)->mic_stop_grace_s));
    }
    if (pt_time_due(at, now_ms)) {
        finish_end(c, now_ms);
        return;
    }
    t->end.on = true;
    t->end.at = at;
}

static void fail(pt_convo_t *c, const char *code, bool cancel, uint32_t now_ms)
{
    char reason[48];
    snprintf(reason, sizeof(reason), "error %s", code);
    end_turn(c, reason, PT_DEV_FAILED, code, false, cancel, false, true, now_ms);
}

static bool send_or_fail(pt_convo_t *c, const char *json, uint32_t now_ms)
{
    if (pt_session_send(c->session, c->turn.gen, json)) {
        return true;
    }
    pt_log_add(c->log, log_time(now_ms), "realtime send failed: realtime session is not open");
    fail(c, UNAVAILABLE, false, now_ms);
    return false;
}

static bool send_audio(pt_convo_t *c, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    char *msg = pt_rt_append_audio(pcm, n);
    if (msg == NULL) {
        return true;
    }
    bool ok = send_or_fail(c, msg, now_ms);
    free(msg);
    return ok;
}

static void early_push(turn_t *t, const int16_t *x, size_t n)
{
    if (t->early_cap == 0) {
        t->early_dropped += n;
        return;
    }
    if (n > t->early_cap) {
        t->early_dropped += n - t->early_cap;
        x += n - t->early_cap;
        n = t->early_cap;
    }
    size_t need = t->early_len + n;
    if (need > t->early_size && t->early_size < t->early_cap) {
        size_t size = t->early_size > 0 ? t->early_size * 2 : EARLY_FIRST;
        while (size < need && size < t->early_cap) {
            size *= 2;
        }
        if (size > t->early_cap) {
            size = t->early_cap;
        }
        int16_t *buf = malloc(size * sizeof(int16_t));
        if (buf == NULL) {
            t->early_dropped += n;
            return;
        }
        for (size_t i = 0; i < t->early_len; i++) {
            buf[i] = t->early[(t->early_head + i) % t->early_size];
        }
        free(t->early);
        t->early = buf;
        t->early_size = size;
        t->early_head = 0;
    }
    if (t->early_len + n > t->early_size) {
        size_t drop = t->early_len + n - t->early_size;
        t->early_head = (t->early_head + drop) % t->early_size;
        t->early_len -= drop;
        t->early_dropped += drop;
    }
    size_t tail = (t->early_head + t->early_len) % t->early_size;
    size_t first = n < t->early_size - tail ? n : t->early_size - tail;
    memcpy(t->early + tail, x, first * sizeof(int16_t));
    memcpy(t->early, x + first, (n - first) * sizeof(int16_t));
    t->early_len += n;
}

static void flush_early(pt_convo_t *c, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    if (t->early_dropped > 0) {
        pt_log_add(c->log, log_time(now_ms), "dropped %.2f s of audio heard before the session was ready",
                   (double)t->early_dropped / PT_CONVO_RT_RATE);
    }
    while (t->early_len > 0) {
        size_t k = t->early_len;
        if (k > EARLY_CHUNK) {
            k = EARLY_CHUNK;
        }
        if (k > t->early_size - t->early_head) {
            k = t->early_size - t->early_head;
        }
        const int16_t *at = t->early + t->early_head;
        t->early_head = (t->early_head + k) % t->early_size;
        t->early_len -= k;
        if (!send_audio(c, at, k, now_ms)) {
            return;
        }
    }
    free(t->early);
    t->early = NULL;
    t->early_size = t->early_head = t->early_len = 0;
}

static void play_through(pt_convo_t *c, const int16_t *pcm, size_t n)
{
    turn_t *t = &c->turn;
    if (n == 0) {
        return;
    }
    int16_t *out = scratch(c, pt_speaker_path_out_cap(t->speaker, n));
    if (out == NULL) {
        return;
    }
    size_t produced = pt_speaker_path_run(t->speaker, pcm, n, out, c->scratch_cap);
    if (produced > 0 && c->port.play != NULL) {
        c->port.play(c->port.ctx, out, produced);
    }
    t->played += produced;
}

static void expect_answer(pt_convo_t *c, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    t->arm_kind = ARM_RESPONSE;
    arm_slot(&t->arm, now_ms, pt_session_timing(c->session)->response_timeout_s);
}

static void answer(pt_convo_t *c, bool commit, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    t->phase = PH_ANSWERING;
    send_if_current(c, PT_DEV_SPEECH_STOPPED);
    t->vad_set = true;
    t->vad_end = now_ms;
    expect_answer(c, now_ms);
    arm_slot(&t->later, now_ms, pt_session_timing(c->session)->answer_max_s);
    if (commit && t->gen != 0) {
        if (send_or_fail(c, pt_rt_commit(), now_ms)) {
            send_or_fail(c, pt_rt_response_create(), now_ms);
        }
    }
}

static void on_audio(pt_convo_t *c, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    if (!t->streaming) {
        t->streaming = true;
        pt_log_add(c->log, log_time(now_ms), "first audio %.2f s after speech end",
                   (double)(uint32_t)(now_ms - t->vad_end) / 1000.0);
        uint32_t at = pt_time_after(t->vad_end, pt_time_ms(pt_session_timing(c->session)->mic_stop_grace_s));
        if (!pt_time_due(at, now_ms) && pt_session_hold(c->session, at)) {
            t->pending = malloc((n > 0 ? n : 1) * sizeof(int16_t));
            if (t->pending != NULL && n > 0) {
                memcpy(t->pending, pcm, n * sizeof(int16_t));
            }
            t->pending_n = t->pending != NULL ? n : 0;
            t->pending_stream = true;
            return;
        }
        if (t->closed) {
            return;
        }
        send_if_current(c, PT_DEV_ANSWER_STREAMING);
    }
    play_through(c, pcm, n);
}

static void on_resume(void *ctx, uint32_t now_ms)
{
    (void)now_ms;
    pt_convo_t *c = ctx;
    turn_t *t = &c->turn;
    if (!t->pending_stream) {
        return;
    }
    t->pending_stream = false;
    if (t->active && !t->closed) {
        send_if_current(c, PT_DEV_ANSWER_STREAMING);
        play_through(c, t->pending, t->pending_n);
    }
    free(t->pending);
    t->pending = NULL;
    t->pending_n = 0;
}

static void complete(pt_convo_t *c, bool ended, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    int16_t *out = scratch(c, FLUSH_CAP);
    if (out != NULL) {
        size_t produced = pt_speaker_path_flush(t->speaker, out, c->scratch_cap);
        if (produced > 0 && c->port.play != NULL) {
            c->port.play(c->port.ctx, out, produced);
        }
        t->played += produced;
    }
    bool more = !ended && t->follow_ups < pt_session_timing(c->session)->max_follow_ups;
    const char *reason = ended ? "ended by model" : more ? "answered" : "answered, follow-up limit reached";
    end_turn(c, reason, PT_DEV_ANSWERED, NULL, more, false, false, false, now_ms);
}

typedef struct {
    pt_convo_t *c;
    bool failed;
} tool_send_t;

static void tool_send(void *ctx, const char *json)
{
    tool_send_t *s = ctx;
    if (!s->failed && !pt_session_send(s->c->session, s->c->turn.gen, json)) {
        s->failed = true;
    }
}

static char *error_output(const char *message)
{
    cJSON *text = cJSON_CreateString(message);
    char *quoted = text != NULL ? cJSON_PrintUnformatted(text) : NULL;
    cJSON_Delete(text);
    if (quoted == NULL) {
        return NULL;
    }
    size_t len = strlen(quoted) + 16;
    char *out = malloc(len);
    if (out != NULL) {
        snprintf(out, len, "{\"error\": %s}", quoted);
    }
    free(quoted);
    return out;
}

static char *run_tool(pt_convo_t *c, const pt_tool_call_t *call, uint32_t now_ms)
{
    char *out = NULL;
    if (strcmp(call->name, PT_TOOL_END) == 0) {
        return strdup("{\"ok\": true}");
    }
    if (strcmp(call->name, PT_TOOL_VOLUME) != 0) {
        char message[64];
        snprintf(message, sizeof(message), "unknown tool %s", call->name);
        return error_output(message);
    }
    char why[96];
    float volume = pt_tools_next_volume(c->volume, call->arguments, why, sizeof(why));
    if (volume < 0.0f) {
        return error_output(why);
    }
    c->volume = volume;
    if (c->port.set_volume != NULL) {
        c->port.set_volume(c->port.ctx, volume);
    }
    pt_log_add(c->log, log_time(now_ms), "volume set to %.2f", (double)volume);
    out = malloc(32);
    if (out != NULL) {
        snprintf(out, 32, "{\"volume\": %ld}", lroundf(volume * 100.0f));
    }
    return out;
}

static void on_response_done(pt_convo_t *c, const pt_rt_event_t *e, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    t->response_id[0] = '\0';
    if (strcmp(e->status, "completed") != 0 && strcmp(e->status, "incomplete") != 0) {
        fail(c, "openai-error", false, now_ms);
        return;
    }
    if (e->n_calls == 0) {
        complete(c, false, now_ms);
        return;
    }
    bool ending = pt_tools_ends_conversation(e->calls, e->n_calls);
    expect_answer(c, now_ms);
    char **outputs = calloc(e->n_calls, sizeof(char *));
    if (outputs == NULL) {
        fail(c, "openai-error", false, now_ms);
        return;
    }
    for (size_t i = 0; i < e->n_calls; i++) {
        outputs[i] = run_tool(c, &e->calls[i], now_ms);
    }
    tool_send_t sender = {c, false};
    pt_rt_tool_outputs(e->calls, (const char *const *)outputs, e->n_calls, !ending, tool_send, &sender);
    for (size_t i = 0; i < e->n_calls; i++) {
        free(outputs[i]);
    }
    free(outputs);
    if (sender.failed) {
        pt_log_add(c->log, log_time(now_ms), "realtime send failed: realtime session is not open");
        fail(c, UNAVAILABLE, false, now_ms);
        return;
    }
    if (t->closed) {
        return;
    }
    if (ending) {
        complete(c, true, now_ms);
    } else {
        expect_answer(c, now_ms);
    }
}

static bool matches(const turn_t *t, const pt_rt_event_t *e)
{
    return t->response_id[0] != '\0' && strcmp(t->response_id, e->response_id) == 0;
}

static void handle_event(pt_convo_t *c, const pt_rt_event_t *e, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    if (e->kind == PT_RT_INPUT_TRANSCRIPT) {
        pt_log_add(c->log, log_time(now_ms), "heard: %s", e->text != NULL ? e->text : "");
        return;
    }
    if (!t->active || t->closed || !t->ready || t->gen != pt_session_gen(c->session)) {
        return;
    }
    switch (e->kind) {
    case PT_RT_SPEECH_STARTED:
        if (t->phase == PH_LISTENING) {
            pt_log_add(c->log, log_time(now_ms), "speech started");
            t->phase = PH_SPEAKING;
            send_if_current(c, PT_DEV_SPEECH_STARTED);
            t->arm_kind = ARM_SPEECH;
            arm_slot(&t->arm, now_ms, pt_session_timing(c->session)->max_speech_s);
        }
        break;
    case PT_RT_SPEECH_STOPPED:
        if (t->phase == PH_SPEAKING) {
            pt_log_add(c->log, log_time(now_ms), "speech stopped");
            answer(c, false, now_ms);
        }
        break;
    case PT_RT_RESPONSE_CREATED:
        if (t->phase == PH_ANSWERING && t->response_id[0] == '\0') {
            snprintf(t->response_id, sizeof(t->response_id), "%s", e->response_id);
            expect_answer(c, now_ms);
        }
        break;
    case PT_RT_AUDIO_DELTA:
        if (matches(t, e)) {
            expect_answer(c, now_ms);
            on_audio(c, e->pcm, e->pcm_len, now_ms);
        }
        break;
    case PT_RT_TRANSCRIPT_DONE:
        if (matches(t, e)) {
            expect_answer(c, now_ms);
        }
        break;
    case PT_RT_RESPONSE_DONE:
        if (matches(t, e)) {
            on_response_done(c, e, now_ms);
        }
        break;
    default:
        break;
    }
}

static void on_event(void *ctx, const pt_rt_event_t *e, uint32_t now_ms)
{
    handle_event(ctx, e, now_ms);
}

static void on_closed(void *ctx, uint32_t now_ms)
{
    pt_convo_t *c = ctx;
    turn_t *t = &c->turn;
    if (t->active && !t->closed && t->ready && t->gen == pt_session_gen(c->session)) {
        fail(c, UNAVAILABLE, false, now_ms);
    }
}

static void on_ready(void *ctx, uint32_t now_ms)
{
    pt_convo_t *c = ctx;
    turn_t *t = &c->turn;
    if (!t->active || t->closed || t->ready) {
        return;
    }
    t->ready = true;
    t->gen = pt_session_gen(c->session);
    const pt_timing_t *timing = pt_session_timing(c->session);
    t->arm_kind = ARM_LISTEN;
    arm_slot(&t->arm, now_ms, t->follow_up ? timing->follow_up_s : timing->first_turn_s);
    flush_early(c, now_ms);
}

static void on_failed(void *ctx, const char *why, uint32_t now_ms)
{
    pt_convo_t *c = ctx;
    pt_log_add(c->log, log_time(now_ms), "realtime unavailable: %s", why != NULL ? why : "unknown");
    if (c->turn.active && !c->turn.closed) {
        fail(c, UNAVAILABLE, false, now_ms);
    }
}

static void on_forget(void *ctx)
{
    pt_convo_t *c = ctx;
    c->follow_up = false;
}

pt_convo_t *pt_convo_new(const pt_port_t *port, pt_log_t *log)
{
    if (port == NULL) {
        return NULL;
    }
    pt_convo_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->port = *port;
    c->log = log;
    pt_session_hooks_t hooks = {on_ready, on_failed, on_event, on_closed, on_resume, on_forget, c};
    c->session = pt_session_new(port, log, &hooks);
    if (c->session == NULL) {
        free(c);
        return NULL;
    }
    return c;
}

void pt_convo_free(pt_convo_t *c)
{
    if (c == NULL) {
        return;
    }
    abandon(c, "conversation closed", 0);
    c->follow_up = false;
    pt_session_free(c->session);
    free(c->scratch);
    free(c);
}

void pt_convo_settings(pt_convo_t *c, const pt_settings_t *s, uint32_t now_ms)
{
    if (c == NULL || s == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    pt_session_settings(c->session, s);
    c->volume = s->sound.volume;
}

void pt_convo_start(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    if (!pt_session_has_settings(c->session)) {
        pt_log_add(c->log, log_time(now_ms), "could not start a turn: no settings yet");
        c->follow_up = false;
        device(c, PT_DEV_FAILED, false, START_FAILED);
        return;
    }
    const pt_settings_t *fresh = pt_session_fresh(c->session);
    pt_mic_path_t *mic = pt_mic_path_new(PT_CONVO_MIC_RATE, PT_CONVO_RT_RATE, fresh);
    pt_speaker_path_t *speaker = pt_speaker_path_new(PT_CONVO_RT_RATE, PT_CONVO_OUT_RATE, fresh);
    if (mic == NULL || speaker == NULL) {
        pt_mic_path_free(mic);
        pt_speaker_path_free(speaker);
        pt_log_add(c->log, log_time(now_ms), "could not start a turn");
        c->follow_up = false;
        device(c, PT_DEV_FAILED, false, START_FAILED);
        return;
    }
    turn_t *old = &c->turn;
    bool cancel_previous = old->active && !old->closed && old->phase != PH_LISTENING;
    c->follow_ups = c->follow_up ? c->follow_ups + 1 : 0;
    bool follow_up = c->follow_up;
    c->follow_up = false;
    abandon(c, "replaced by a new start", now_ms);
    pt_session_cancel_idle(c->session);
    turn_t *t = &c->turn;
    t->active = true;
    t->follow_up = follow_up;
    t->follow_ups = c->follow_ups;
    t->phase = PH_LISTENING;
    t->mic = mic;
    t->speaker = speaker;
    float early_s = fresh->timing.first_turn_s < EARLY_MAX_S ? fresh->timing.first_turn_s : EARLY_MAX_S;
    t->early_cap = (size_t)pt_time_ms(early_s) * (PT_CONVO_RT_RATE / 1000);
    pt_log_add(c->log, log_time(now_ms), "turn started (%s)", follow_up ? "follow-up" : "first turn");
    device(c, PT_DEV_LISTENING, false, NULL);
    pt_session_for_turn(c->session, cancel_previous, now_ms);
    if (t->active && !t->closed && !t->ready) {
        t->gen = pt_session_gen(c->session);
    }
}

void pt_convo_stop(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    c->follow_up = false;
    turn_t *t = &c->turn;
    if (!t->active) {
        pt_log_add(c->log, log_time(now_ms), "stop with no active turn; follow-up cleared");
        device(c, PT_DEV_ENDED, false, NULL);
        return;
    }
    if (t->closed) {
        pt_log_add(c->log, log_time(now_ms), "stop while the turn is ending; follow-up cleared");
        t->stopped = true;
        return;
    }
    end_turn(c, "stopped by device", PT_DEV_ENDED, NULL, false, t->phase != PH_LISTENING, true, false, now_ms);
}

void pt_convo_mic(pt_convo_t *c, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    if (c == NULL || pcm == NULL || n == 0) {
        return;
    }
    pt_convo_tick(c, now_ms);
    turn_t *t = &c->turn;
    if (!t->active || t->closed || t->phase == PH_ANSWERING) {
        return;
    }
    int16_t *out = scratch(c, pt_mic_path_out_cap(t->mic, n));
    if (out == NULL) {
        return;
    }
    size_t produced = pt_mic_path_run(t->mic, pcm, n, out, c->scratch_cap);
    if (produced == 0) {
        return;
    }
    if (t->ready) {
        send_audio(c, out, produced, now_ms);
    } else {
        early_push(t, out, produced);
    }
}

void pt_convo_rt_opened(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    pt_session_rt_opened(c->session, now_ms);
}

void pt_convo_rt_failed(pt_convo_t *c, const char *why, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    pt_session_rt_failed(c->session, why, now_ms);
}

void pt_convo_rt_message(pt_convo_t *c, const char *json, size_t len, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    pt_session_rt_message(c->session, json, len, now_ms);
}

void pt_convo_rt_closed(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    pt_session_rt_closed(c->session, now_ms);
}

bool pt_convo_close_idle(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return false;
    }
    pt_convo_tick(c, now_ms);
    if (c->turn.active) {
        return false;
    }
    c->follow_up = false;
    pt_session_close(c->session);
    return true;
}

void pt_convo_rt_overflow(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    pt_convo_tick(c, now_ms);
    turn_t *t = &c->turn;
    if (t->active && !t->closed) {
        pt_log_add(c->log, log_time(now_ms), "answer too long — cut after %.0f s",
                   (double)t->played / PT_CONVO_OUT_RATE);
        t->response_id[0] = '\0';
        int16_t *out = t->speaker != NULL ? scratch(c, FLUSH_CAP) : NULL;
        if (out != NULL) {
            size_t produced = pt_speaker_path_flush(t->speaker, out, c->scratch_cap);
            if (produced > 0 && c->port.play != NULL) {
                c->port.play(c->port.ctx, out, produced);
            }
            t->played += produced;
        }
        end_turn(c, "answer cut", PT_DEV_ANSWERED, NULL, false, true, false, true, now_ms);
    } else {
        pt_log_add(c->log, log_time(now_ms), "messages from OpenAI did not fit in memory; the connection is closed");
    }
    c->follow_up = false;
    pt_session_close(c->session);
}

static timer_slot_t *turn_due(pt_convo_t *c, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    timer_slot_t *order[] = {&t->end, &t->arm, &t->later};
    timer_slot_t *best = NULL;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (order[i]->on && pt_time_due(order[i]->at, now_ms) && (best == NULL || pt_time_before(order[i]->at, best->at))) {
            best = order[i];
        }
    }
    return t->active ? best : NULL;
}

static void fire(pt_convo_t *c, timer_slot_t *slot, uint32_t now_ms)
{
    turn_t *t = &c->turn;
    slot->on = false;
    if (slot == &t->end) {
        finish_end(c, now_ms);
    } else if (slot == &t->later) {
        pt_log_add(c->log, log_time(now_ms), "answer limit reached");
        fail(c, "timeout", true, now_ms);
    } else if (t->arm_kind == ARM_LISTEN) {
        if (t->phase == PH_LISTENING) {
            end_turn(c, "no speech", PT_DEV_ENDED, NULL, false, false, true, false, now_ms);
        }
    } else if (t->arm_kind == ARM_SPEECH) {
        if (t->phase == PH_SPEAKING) {
            pt_log_add(c->log, log_time(now_ms), "speech limit reached");
            answer(c, true, now_ms);
        }
    } else {
        fail(c, "timeout", false, now_ms);
    }
}

void pt_convo_tick(pt_convo_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    for (int guard = 0; guard < 64; guard++) {
        timer_slot_t *slot = turn_due(c, now_ms);
        uint32_t session_at = pt_session_next_deadline(c->session);
        bool session_due = session_at != PT_DEADLINE_NONE && pt_time_due(session_at, now_ms);
        if (slot != NULL && (!session_due || !pt_time_before(session_at, slot->at))) {
            fire(c, slot, now_ms);
        } else if (session_due) {
            pt_session_tick_one(c->session, now_ms);
        } else {
            return;
        }
    }
}

uint32_t pt_convo_next_deadline(const pt_convo_t *c)
{
    if (c == NULL) {
        return PT_DEADLINE_NONE;
    }
    uint32_t best = pt_session_next_deadline(c->session);
    const turn_t *t = &c->turn;
    if (t->active) {
        const timer_slot_t *order[] = {&t->end, &t->arm, &t->later};
        for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
            if (order[i]->on && (best == PT_DEADLINE_NONE || pt_time_before(order[i]->at, best))) {
                best = order[i]->at;
            }
        }
    }
    return best;
}

bool pt_convo_follow_up_pending(const pt_convo_t *c)
{
    return c != NULL && c->follow_up;
}
