#include "pt_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNAVAILABLE "openai-unavailable"
#define START_FAILED "start-failed"
#define QUIET_S 0.8f
#define SILENT_LEVEL 2
#define SEGMENT_END_SAMPLES 12000u
#define PREBUFFER_SAMPLES 6000u
#define PASS_ZERO_SAMPLES 6000u
#define STALL_S 2.0f
#define KEEP_MS 100u
#define KEEP_SAMPLES 2400u
#define LIVE_EARLY_CHUNK 12000u
#define LIVE_NO_ANSWER_S 8.0f
#define TEXT_CAP 4000u
#define HISTORY_MAX 24u
#define HISTORY_CHARS 4000u

typedef enum { PH_LISTENING, PH_SPEAKING, PH_THINKING, PH_ANSWERING } phase_t;
typedef enum { ARM_LISTEN, ARM_SPEECH, ARM_RESPONSE } arm_t;

typedef struct {
    char *buf;
    size_t len;
} text_t;

typedef struct {
    bool user;
    char *text;
} msg_t;

typedef struct {
    bool active;
    bool ready;
    bool follow_up;
    bool stopped_sent;
    bool speech_end_set;
    phase_t phase;
    arm_t arm_kind;
    pt_slot_t arm;
    pt_slot_t quiet;
    pt_slot_t cap;
    bool cap_set;
    uint32_t started_at;
    uint32_t speech_start;
    uint32_t speech_end;
    pt_mic_path_t *mic;
    pt_early_t early;
    text_t heard;
} live_turn_t;

struct pt_live {
    pt_convo_io_t *io;
    live_turn_t turn;
    bool convo;
    bool follow_up;
    int follow_ups;
    bool ending;
    uint32_t gen;
    pt_speaker_path_t *speaker;
    bool seg_on;
    bool seg_playing;
    size_t zero_run;
    int16_t *pre;
    size_t pre_n;
    uint64_t played;
    text_t answer;
    bool answer_tail;
    msg_t hist[HISTORY_MAX];
    size_t hist_n;
    pt_slot_t seg_limit;
    pt_slot_t stall;
    pt_slot_t keep;
    pt_slot_t forget;
    int16_t tx[KEEP_SAMPLES];
    size_t tx_n;
};

static const int16_t ZEROS[KEEP_SAMPLES];

static void fail(pt_live_t *l, const char *code, uint32_t now_ms);
static void end_turn(pt_live_t *l, const char *reason, pt_dev_event_t ev, const char *code, bool follow_up,
                     uint32_t now_ms);

static uint32_t log_time(uint32_t now_ms)
{
    return now_ms / 1000u;
}

static pt_session_t *session(const pt_live_t *l)
{
    return l->io->session;
}

static void text_add(text_t *t, const char *s)
{
    if (s == NULL) {
        return;
    }
    size_t n = strlen(s);
    if (t->len + n > TEXT_CAP) {
        n = TEXT_CAP - t->len;
        while (n > 0 && ((unsigned char)s[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    if (n == 0) {
        return;
    }
    char *buf = realloc(t->buf, t->len + n + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf + t->len, s, n);
    t->len += n;
    buf[t->len] = '\0';
    t->buf = buf;
}

static void text_clear(text_t *t)
{
    free(t->buf);
    t->buf = NULL;
    t->len = 0;
}

static size_t chars(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++) {
        n += ((unsigned char)*s & 0xC0u) != 0x80u;
    }
    return n;
}

static void drop_oldest(pt_live_t *l)
{
    free(l->hist[0].text);
    memmove(l->hist, l->hist + 1, (l->hist_n - 1) * sizeof(l->hist[0]));
    l->hist_n--;
}

static void trim_history(pt_live_t *l)
{
    size_t total = 0;
    for (size_t i = 0; i < l->hist_n; i++) {
        total += chars(l->hist[i].text);
    }
    while (total > HISTORY_CHARS && l->hist_n > 0) {
        total -= chars(l->hist[0].text);
        drop_oldest(l);
    }
}

static bool remember(pt_live_t *l, bool user, text_t *t)
{
    if (t->len == 0) {
        return false;
    }
    if (l->hist_n == HISTORY_MAX) {
        drop_oldest(l);
    }
    l->hist[l->hist_n].user = user;
    l->hist[l->hist_n].text = t->buf;
    l->hist_n++;
    l->answer_tail = false;
    t->buf = NULL;
    t->len = 0;
    trim_history(l);
    return true;
}

static void on_said(pt_live_t *l, const char *text)
{
    if (l->seg_on || !l->answer_tail || l->hist_n == 0) {
        text_add(&l->answer, text);
        return;
    }
    msg_t *last = &l->hist[l->hist_n - 1];
    text_t tail = {last->text, strlen(last->text)};
    text_add(&tail, text);
    last->text = tail.buf;
    trim_history(l);
    if (l->hist_n == 0) {
        l->answer_tail = false;
    }
}

static void forget_history(pt_live_t *l)
{
    for (size_t i = 0; i < l->hist_n; i++) {
        free(l->hist[i].text);
    }
    l->hist_n = 0;
    l->answer_tail = false;
    l->forget.on = false;
}

static void give_history(pt_live_t *l)
{
    pt_rt_msg_t msgs[HISTORY_MAX];
    for (size_t i = 0; i < l->hist_n; i++) {
        msgs[i].role = l->hist[i].user ? "user" : "assistant";
        msgs[i].text = l->hist[i].text;
    }
    pt_session_set_history(session(l), l->hist_n > 0 ? msgs : NULL, l->hist_n);
}

static bool send_pcm(pt_live_t *l, const int16_t *pcm, size_t n)
{
    char *msg = pt_rt_live_append(pcm, n);
    if (msg == NULL) {
        return true;
    }
    bool ok = pt_session_send(session(l), l->gen, msg);
    free(msg);
    return ok;
}

static void send_failed(pt_live_t *l, uint32_t now_ms)
{
    pt_log_add(l->io->log, log_time(now_ms), "live send failed: the live session is not open");
    fail(l, UNAVAILABLE, now_ms);
}

static bool early_send(void *ctx, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    pt_live_t *l = ctx;
    if (send_pcm(l, pcm, n)) {
        return true;
    }
    send_failed(l, now_ms);
    return false;
}

static bool tx_flush(pt_live_t *l)
{
    size_t n = l->tx_n;
    l->tx_n = 0;
    return n == 0 || send_pcm(l, l->tx, n);
}

static bool forwarding(const pt_live_t *l)
{
    const live_turn_t *t = &l->turn;
    return t->active && t->ready && t->phase != PH_ANSWERING && !l->seg_on;
}

static void play_end(pt_live_t *l)
{
    if (l->io->port.play_end != NULL) {
        l->io->port.play_end(l->io->port.ctx);
    }
}

static void play(pt_live_t *l, const int16_t *pcm, size_t n)
{
    l->played += pt_io_play(l->io, l->speaker, pcm, n);
}

static void release_prebuffer(pt_live_t *l)
{
    l->seg_playing = true;
    if (l->pre != NULL) {
        play(l, l->pre, l->pre_n);
        free(l->pre);
        l->pre = NULL;
        l->pre_n = 0;
    }
}

static void seg_out(pt_live_t *l, const int16_t *x, size_t n)
{
    while (n > 0 && !l->seg_playing) {
        size_t k = PREBUFFER_SAMPLES - l->pre_n;
        if (k > n) {
            k = n;
        }
        memcpy(l->pre + l->pre_n, x, k * sizeof(int16_t));
        l->pre_n += k;
        x += k;
        n -= k;
        if (l->pre_n == PREBUFFER_SAMPLES) {
            release_prebuffer(l);
        }
    }
    if (n > 0) {
        play(l, x, n);
    }
}

static void seg_zeros(pt_live_t *l, size_t n)
{
    while (n > 0) {
        size_t k = n < KEEP_SAMPLES ? n : KEEP_SAMPLES;
        seg_out(l, ZEROS, k);
        n -= k;
    }
}

static void seg_clear(pt_live_t *l)
{
    free(l->pre);
    l->pre = NULL;
    l->pre_n = 0;
    l->seg_on = false;
    l->seg_playing = false;
    l->zero_run = 0;
    l->seg_limit.on = false;
    l->stall.on = false;
}

static void seg_drop(pt_live_t *l)
{
    if (!l->seg_on) {
        return;
    }
    seg_clear(l);
    text_clear(&l->answer);
    play_end(l);
}

static void release_turn(live_turn_t *t)
{
    pt_mic_path_free(t->mic);
    pt_early_free(&t->early);
    text_clear(&t->heard);
    memset(t, 0, sizeof(*t));
}

static void close_turn(pt_live_t *l, const char *reason, uint32_t now_ms)
{
    live_turn_t *t = &l->turn;
    pt_log_add(l->io->log, log_time(now_ms), "turn ended: %s", reason);
    char summary[96];
    pt_mic_path_summary(t->mic, summary, sizeof(summary));
    pt_log_add(l->io->log, log_time(now_ms), "device audio: %s", summary);
    remember(l, true, &t->heard);
    release_turn(t);
}

static void end_convo(pt_live_t *l, uint32_t now_ms)
{
    if (!l->convo) {
        return;
    }
    seg_drop(l);
    remember(l, false, &l->answer);
    l->tx_n = 0;
    l->convo = false;
    l->follow_up = false;
    l->follow_ups = 0;
    l->ending = false;
    l->answer_tail = false;
    l->keep.on = false;
    pt_session_t *s = session(l);
    pt_session_abort(s);
    pt_session_end_live(s);
    pt_session_set_history(s, NULL, 0);
    pt_speaker_path_free(l->speaker);
    l->speaker = NULL;
    if (l->hist_n > 0) {
        pt_slot_arm(&l->forget, now_ms, pt_session_fresh_timing(s)->session_idle_s);
    }
}

static void end_turn(pt_live_t *l, const char *reason, pt_dev_event_t ev, const char *code, bool follow_up,
                     uint32_t now_ms)
{
    if (!l->turn.active) {
        return;
    }
    close_turn(l, reason, now_ms);
    pt_io_device(l->io, ev, follow_up, code);
    l->follow_up = follow_up;
    if (!follow_up) {
        end_convo(l, now_ms);
    }
}

static void fail(pt_live_t *l, const char *code, uint32_t now_ms)
{
    seg_drop(l);
    if (l->turn.active) {
        char reason[48];
        snprintf(reason, sizeof(reason), "error %s", code);
        end_turn(l, reason, PT_DEV_FAILED, code, false, now_ms);
        return;
    }
    if (l->convo) {
        pt_io_device(l->io, PT_DEV_FAILED, false, code);
        end_convo(l, now_ms);
    }
}

static void finish_ending(pt_live_t *l, uint32_t now_ms)
{
    if (l->turn.active) {
        end_turn(l, "ended by model", PT_DEV_ANSWERED, NULL, false, now_ms);
        return;
    }
    pt_log_add(l->io->log, log_time(now_ms), "conversation ended by model");
    pt_io_device(l->io, PT_DEV_ANSWERED, false, NULL);
    end_convo(l, now_ms);
}

static float no_answer_s(const pt_timing_t *timing)
{
    return timing->response_timeout_s < LIVE_NO_ANSWER_S ? timing->response_timeout_s : LIVE_NO_ANSWER_S;
}

static void speech_stopped(pt_live_t *l, uint32_t now_ms)
{
    live_turn_t *t = &l->turn;
    const pt_timing_t *timing = pt_session_timing(session(l));
    if (!t->cap_set) {
        t->cap_set = true;
        pt_slot_arm(&t->cap, t->speech_start, timing->max_speech_s + timing->response_timeout_s);
    }
    t->phase = PH_THINKING;
    t->quiet.on = false;
    pt_log_add(l->io->log, log_time(now_ms), "speech stopped");
    if (pt_session_fresh(session(l))->openai.log_heard) {
        pt_log_add(l->io->log, log_time(now_ms), "heard: %s", t->heard.buf != NULL ? t->heard.buf : "");
    }
    t->speech_end = now_ms;
    t->speech_end_set = true;
    if (!t->stopped_sent) {
        t->stopped_sent = true;
        pt_io_device(l->io, PT_DEV_SPEECH_STOPPED, false, NULL);
    }
    t->arm_kind = ARM_RESPONSE;
    pt_slot_arm(&t->arm, now_ms, no_answer_s(timing));
}

static void own_segment(pt_live_t *l)
{
    live_turn_t *t = &l->turn;
    if (!t->stopped_sent) {
        t->stopped_sent = true;
        pt_io_device(l->io, PT_DEV_SPEECH_STOPPED, false, NULL);
    }
    t->phase = PH_ANSWERING;
    t->arm.on = false;
    t->quiet.on = false;
    t->cap.on = false;
    pt_io_device(l->io, PT_DEV_ANSWER_STREAMING, false, NULL);
}

static void seg_begin(pt_live_t *l, uint32_t now_ms)
{
    if (!tx_flush(l)) {
        send_failed(l, now_ms);
        return;
    }
    l->seg_on = true;
    l->zero_run = 0;
    l->played = 0;
    l->pre_n = 0;
    l->pre = malloc(PREBUFFER_SAMPLES * sizeof(int16_t));
    l->seg_playing = l->pre == NULL;
    l->answer_tail = false;
    pt_slot_arm(&l->seg_limit, now_ms, pt_session_timing(session(l))->answer_max_s);
    live_turn_t *t = &l->turn;
    if (!t->active) {
        pt_log_add(l->io->log, log_time(now_ms), "first audio between turns");
        return;
    }
    if (t->phase == PH_SPEAKING) {
        speech_stopped(l, now_ms);
    }
    if (t->speech_end_set) {
        pt_log_add(l->io->log, log_time(now_ms), "first audio %.2f s after speech end",
                   (double)(uint32_t)(now_ms - t->speech_end) / 1000.0);
    } else {
        pt_log_add(l->io->log, log_time(now_ms), "first audio %.2f s after turn start",
                   (double)(uint32_t)(now_ms - t->started_at) / 1000.0);
    }
    own_segment(l);
}

static void seg_end(pt_live_t *l, uint32_t now_ms)
{
    if (!l->seg_playing) {
        release_prebuffer(l);
    }
    l->played += pt_io_flush(l->io, l->speaker);
    seg_clear(l);
    play_end(l);
    live_turn_t *t = &l->turn;
    if (t->active && t->phase == PH_ANSWERING) {
        remember(l, true, &t->heard);
        l->answer_tail = remember(l, false, &l->answer);
        bool more = !l->ending && l->follow_ups < pt_session_timing(session(l))->max_follow_ups;
        const char *reason = l->ending ? "ended by model" : more ? "answered" : "answered, follow-up limit reached";
        end_turn(l, reason, PT_DEV_ANSWERED, NULL, more, now_ms);
        return;
    }
    l->answer_tail = remember(l, false, &l->answer);
    if (l->ending) {
        finish_ending(l, now_ms);
    }
}

static bool silent(int16_t x)
{
    return x >= -SILENT_LEVEL && x <= SILENT_LEVEL;
}

static size_t held_zeros(const pt_live_t *l)
{
    return l->zero_run > PASS_ZERO_SAMPLES ? l->zero_run - PASS_ZERO_SAMPLES : 0;
}

static void on_audio(pt_live_t *l, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    size_t i = 0;
    while (i < n && l->convo) {
        if (!l->seg_on) {
            while (i < n && silent(pcm[i])) {
                i++;
            }
            if (i == n) {
                return;
            }
            seg_begin(l, now_ms);
        }
        size_t from = i;
        bool ended = false;
        for (; i < n && !ended; i++) {
            if (!silent(pcm[i])) {
                size_t held = held_zeros(l);
                if (held > 0) {
                    seg_zeros(l, held);
                    from = i;
                }
                l->zero_run = 0;
            } else if (++l->zero_run > PASS_ZERO_SAMPLES) {
                if (i > from) {
                    seg_out(l, pcm + from, i - from);
                }
                from = i + 1;
                ended = l->zero_run >= SEGMENT_END_SAMPLES;
            }
        }
        if (i > from) {
            seg_out(l, pcm + from, i - from);
        }
        if (ended) {
            seg_end(l, now_ms);
        }
    }
    if (l->seg_on) {
        pt_slot_arm(&l->stall, now_ms, STALL_S);
    }
}

static void on_heard(pt_live_t *l, const char *text, uint32_t now_ms)
{
    live_turn_t *t = &l->turn;
    if (!t->active || !t->ready || text == NULL || text[0] == '\0') {
        return;
    }
    text_add(&t->heard, text);
    const pt_timing_t *timing = pt_session_timing(session(l));
    switch (t->phase) {
    case PH_LISTENING:
        pt_log_add(l->io->log, log_time(now_ms), "speech started");
        t->phase = PH_SPEAKING;
        t->speech_start = now_ms;
        pt_io_device(l->io, PT_DEV_SPEECH_STARTED, false, NULL);
        t->arm_kind = ARM_SPEECH;
        pt_slot_arm(&t->arm, now_ms, timing->max_speech_s);
        pt_slot_arm(&t->quiet, now_ms, QUIET_S);
        break;
    case PH_SPEAKING:
        pt_slot_arm(&t->quiet, now_ms, QUIET_S);
        break;
    case PH_THINKING:
        pt_log_add(l->io->log, log_time(now_ms), "speech resumed");
        t->phase = PH_SPEAKING;
        t->arm.on = false;
        pt_slot_arm(&t->quiet, now_ms, QUIET_S);
        break;
    case PH_ANSWERING:
        break;
    }
}

typedef struct {
    pt_live_t *l;
    bool failed;
} tool_send_t;

static void tool_send(void *ctx, const char *json)
{
    tool_send_t *s = ctx;
    if (!s->failed && !pt_session_send(session(s->l), s->l->gen, json)) {
        s->failed = true;
    }
}

static void on_tool(pt_live_t *l, const pt_rt_event_t *e, uint32_t now_ms)
{
    if (e->n_calls == 0 || e->calls == NULL) {
        return;
    }
    const pt_tool_call_t *call = &e->calls[0];
    pt_log_add(l->io->log, log_time(now_ms), "tool call: %s", call->name);
    bool ending = pt_tools_ends_conversation(call, 1);
    char *out = pt_io_tool(l->io, call, now_ms);
    tool_send_t sender = {l, false};
    pt_rt_live_tool_output(call, out, !ending, tool_send, &sender);
    free(out);
    if (sender.failed) {
        send_failed(l, now_ms);
        return;
    }
    if (ending) {
        l->ending = true;
        if (!l->seg_on) {
            finish_ending(l, now_ms);
        }
        return;
    }
    live_turn_t *t = &l->turn;
    if (t->active && t->phase == PH_THINKING) {
        pt_slot_arm(&t->arm, now_ms, no_answer_s(pt_session_timing(session(l))));
    }
}

static void keep_alive(pt_live_t *l, uint32_t at, uint32_t now_ms)
{
    uint32_t next = pt_time_after(at, KEEP_MS);
    if (pt_time_due(next, now_ms)) {
        next = pt_time_after(now_ms, KEEP_MS);
    }
    l->keep.on = true;
    l->keep.at = next;
    uint32_t gen = pt_session_gen(session(l));
    if (forwarding(l) || gen == 0 || gen != l->gen) {
        return;
    }
    tx_flush(l);
    send_pcm(l, ZEROS, KEEP_SAMPLES);
}

pt_live_t *pt_live_new(pt_convo_io_t *io)
{
    if (io == NULL) {
        return NULL;
    }
    pt_live_t *l = calloc(1, sizeof(*l));
    if (l != NULL) {
        l->io = io;
    }
    return l;
}

void pt_live_free(pt_live_t *l)
{
    if (l == NULL) {
        return;
    }
    release_turn(&l->turn);
    free(l->pre);
    pt_speaker_path_free(l->speaker);
    text_clear(&l->answer);
    forget_history(l);
    free(l);
}

void pt_live_reset(pt_live_t *l, uint32_t now_ms)
{
    if (l->turn.active) {
        close_turn(l, "replaced by a new start", now_ms);
    }
    end_convo(l, now_ms);
    forget_history(l);
}

void pt_live_start(pt_live_t *l, uint32_t now_ms)
{
    pt_session_t *s = session(l);
    const pt_settings_t *fresh = pt_session_fresh(s);
    pt_mic_path_t *mic = pt_mic_path_new(PT_CONVO_MIC_RATE, PT_CONVO_RT_RATE, fresh);
    pt_speaker_path_t *speaker = pt_speaker_path_new(PT_CONVO_RT_RATE, PT_CONVO_OUT_RATE, fresh);
    if (mic == NULL || speaker == NULL) {
        pt_mic_path_free(mic);
        pt_speaker_path_free(speaker);
        pt_log_add(l->io->log, log_time(now_ms), "could not start a turn");
        if (l->turn.active) {
            close_turn(l, "replaced by a new start", now_ms);
        }
        end_convo(l, now_ms);
        pt_io_device(l->io, PT_DEV_FAILED, false, START_FAILED);
        return;
    }
    bool follow_up = l->convo && l->follow_up;
    if (l->turn.active) {
        close_turn(l, "replaced by a new start", now_ms);
    }
    if (!l->convo) {
        l->convo = true;
        l->ending = false;
        l->forget.on = false;
        l->keep.on = true;
        l->keep.at = pt_time_after(now_ms, KEEP_MS);
    }
    l->follow_ups = follow_up ? l->follow_ups + 1 : 0;
    l->follow_up = false;
    l->answer_tail = false;
    pt_session_cancel_idle(s);
    if (l->seg_on) {
        pt_speaker_path_free(speaker);
    } else {
        pt_speaker_path_free(l->speaker);
        l->speaker = speaker;
    }
    live_turn_t *t = &l->turn;
    t->active = true;
    t->follow_up = follow_up;
    t->phase = PH_LISTENING;
    t->mic = mic;
    t->started_at = now_ms;
    pt_early_init(&t->early, fresh->timing.first_turn_s);
    pt_log_add(l->io->log, log_time(now_ms), "turn started (%s)", follow_up ? "follow-up" : "first turn");
    pt_io_device(l->io, PT_DEV_LISTENING, false, NULL);
    pt_session_for_turn(s, false, now_ms);
    if (!t->active) {
        return;
    }
    if (!t->ready) {
        seg_drop(l);
        give_history(l);
    } else if (l->seg_on) {
        own_segment(l);
    }
}

void pt_live_stop(pt_live_t *l, uint32_t now_ms)
{
    l->follow_up = false;
    if (!l->turn.active) {
        pt_log_add(l->io->log, log_time(now_ms), "stop with no active turn; follow-up cleared");
        pt_io_device(l->io, PT_DEV_ENDED, false, NULL);
        end_convo(l, now_ms);
        return;
    }
    seg_drop(l);
    end_turn(l, "stopped by device", PT_DEV_ENDED, NULL, false, now_ms);
}

void pt_live_mic(pt_live_t *l, const int16_t *pcm, size_t n, uint32_t now_ms)
{
    live_turn_t *t = &l->turn;
    if (!t->active || t->phase == PH_ANSWERING || l->seg_on) {
        return;
    }
    int16_t *out = pt_io_scratch(l->io, pt_mic_path_out_cap(t->mic, n));
    if (out == NULL) {
        return;
    }
    size_t produced = pt_mic_path_run(t->mic, pcm, n, out, l->io->scratch_cap);
    if (produced == 0) {
        return;
    }
    if (!t->ready) {
        pt_early_push(&t->early, out, produced);
        return;
    }
    while (produced > 0) {
        size_t k = KEEP_SAMPLES - l->tx_n;
        if (k > produced) {
            k = produced;
        }
        memcpy(l->tx + l->tx_n, out, k * sizeof(int16_t));
        l->tx_n += k;
        out += k;
        produced -= k;
        if (l->tx_n == KEEP_SAMPLES && !tx_flush(l)) {
            send_failed(l, now_ms);
            return;
        }
    }
}

void pt_live_overflow(pt_live_t *l, uint32_t now_ms)
{
    if (l->turn.active) {
        pt_log_add(l->io->log, log_time(now_ms), "answer too long — cut after %.0f s",
                   (double)l->played / PT_CONVO_OUT_RATE);
        if (l->seg_on) {
            if (!l->seg_playing) {
                release_prebuffer(l);
            }
            l->played += pt_io_flush(l->io, l->speaker);
        }
        seg_drop(l);
        end_turn(l, "answer cut", PT_DEV_ANSWERED, NULL, false, now_ms);
    } else {
        pt_log_add(l->io->log, log_time(now_ms), "messages from OpenAI did not fit in memory; the connection is closed");
        end_convo(l, now_ms);
    }
    l->follow_up = false;
    pt_session_close(session(l));
}

bool pt_live_close_idle(pt_live_t *l, uint32_t now_ms)
{
    if (l->turn.active) {
        return false;
    }
    end_convo(l, now_ms);
    forget_history(l);
    pt_session_close(session(l));
    return true;
}

static pt_slot_t *earliest(pt_live_t *l)
{
    pt_slot_t *list[] = {&l->seg_limit, &l->stall, &l->turn.arm, &l->turn.quiet, &l->turn.cap, &l->keep, &l->forget};
    pt_slot_t *best = NULL;
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        if (list[i]->on && (best == NULL || pt_time_before(list[i]->at, best->at))) {
            best = list[i];
        }
    }
    return best;
}

uint32_t pt_live_next_deadline(pt_live_t *l)
{
    const pt_slot_t *best = earliest(l);
    return best != NULL ? best->at : PT_DEADLINE_NONE;
}

void pt_live_fire(pt_live_t *l, uint32_t now_ms)
{
    pt_slot_t *slot = earliest(l);
    if (slot == NULL || !pt_time_due(slot->at, now_ms)) {
        return;
    }
    uint32_t at = slot->at;
    slot->on = false;
    live_turn_t *t = &l->turn;
    if (slot == &l->seg_limit) {
        pt_log_add(l->io->log, log_time(now_ms), "answer limit reached");
        fail(l, "timeout", now_ms);
    } else if (slot == &l->stall) {
        if (l->seg_on) {
            pt_log_add(l->io->log, log_time(now_ms), "answer audio stopped for 2 s");
            seg_end(l, now_ms);
        }
    } else if (slot == &t->cap) {
        if (t->active && t->phase != PH_ANSWERING) {
            end_turn(l, "no answer", PT_DEV_ENDED, NULL, false, now_ms);
        }
    } else if (slot == &t->quiet) {
        if (t->phase == PH_SPEAKING) {
            speech_stopped(l, now_ms);
        }
    } else if (slot == &t->arm) {
        if (t->arm_kind == ARM_LISTEN) {
            if (t->phase == PH_LISTENING) {
                end_turn(l, "no speech", PT_DEV_ENDED, NULL, false, now_ms);
            }
        } else if (t->arm_kind == ARM_SPEECH) {
            if (t->phase == PH_SPEAKING) {
                pt_log_add(l->io->log, log_time(now_ms), "speech limit reached");
                speech_stopped(l, now_ms);
            }
        } else if (t->phase == PH_THINKING) {
            end_turn(l, "no answer", PT_DEV_ENDED, NULL, false, now_ms);
        }
    } else if (slot == &l->keep) {
        keep_alive(l, at, now_ms);
    } else {
        forget_history(l);
    }
}

bool pt_live_follow_up_pending(const pt_live_t *l)
{
    return l != NULL && l->follow_up;
}

void pt_live_on_ready(pt_live_t *l, uint32_t now_ms)
{
    live_turn_t *t = &l->turn;
    l->gen = pt_session_gen(session(l));
    if (!t->active || t->ready) {
        return;
    }
    t->ready = true;
    const pt_timing_t *timing = pt_session_timing(session(l));
    t->arm_kind = ARM_LISTEN;
    pt_slot_arm(&t->arm, now_ms, t->follow_up ? timing->follow_up_s : timing->first_turn_s);
    pt_early_flush(&t->early, l->io->log, now_ms, LIVE_EARLY_CHUNK, early_send, l);
}

void pt_live_on_failed(pt_live_t *l, const char *why, uint32_t now_ms)
{
    pt_log_add(l->io->log, log_time(now_ms), "live unavailable: %s", why != NULL ? why : "unknown");
    if (l->turn.active) {
        fail(l, UNAVAILABLE, now_ms);
    }
}

void pt_live_on_event(pt_live_t *l, const pt_rt_event_t *e, uint32_t now_ms)
{
    if (!l->convo) {
        return;
    }
    switch (e->kind) {
    case PT_RT_LIVE_AUDIO:
        on_audio(l, e->pcm, e->pcm_len, now_ms);
        break;
    case PT_RT_INPUT_DELTA:
        on_heard(l, e->text, now_ms);
        break;
    case PT_RT_OUTPUT_DELTA:
        on_said(l, e->text);
        break;
    case PT_RT_TOOL_CALL:
        on_tool(l, e, now_ms);
        break;
    default:
        break;
    }
}

void pt_live_on_closed(pt_live_t *l, uint32_t now_ms)
{
    if (!l->convo) {
        return;
    }
    if (l->turn.active) {
        fail(l, UNAVAILABLE, now_ms);
        return;
    }
    seg_drop(l);
}
