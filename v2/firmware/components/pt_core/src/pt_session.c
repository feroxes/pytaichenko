#include "pt_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DURATION_MS 0x3FFFFFFFu

typedef enum { ST_NONE, ST_OPENING, ST_WAIT_CREATED, ST_WAIT_UPDATED, ST_READY } state_t;
typedef enum { WAIT_NONE, WAIT_OPEN, WAIT_CHECK } waiting_t;

typedef struct {
    bool on;
    uint32_t at;
} deadline_t;

typedef struct deferred {
    struct deferred *next;
    bool closed;
    pt_rt_event_t ev;
} deferred_t;

struct pt_session {
    pt_port_t port;
    pt_log_t *log;
    pt_session_hooks_t hooks;
    pt_settings_t fresh;
    pt_settings_t used;
    bool have_settings;
    state_t state;
    waiting_t waiting;
    bool lost;
    uint32_t gen;
    char model[64];
    deadline_t open;
    deadline_t check;
    deadline_t idle;
    deadline_t hold;
    deferred_t *head;
    deferred_t *tail;
};

bool pt_time_due(uint32_t at, uint32_t now_ms)
{
    return (int32_t)(now_ms - at) >= 0;
}

bool pt_time_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

uint32_t pt_time_ms(float seconds)
{
    if (!(seconds > 0.0f)) {
        return 0;
    }
    double ms = (double)seconds * 1000.0 + 0.5;
    if (ms >= (double)MAX_DURATION_MS) {
        return MAX_DURATION_MS;
    }
    return (uint32_t)ms;
}

uint32_t pt_time_after(uint32_t now_ms, uint32_t ms)
{
    uint32_t at = now_ms + ms;
    return at == PT_DEADLINE_NONE ? 0 : at;
}

static uint32_t log_time(uint32_t now_ms)
{
    return now_ms / 1000u;
}

static void set_deadline(deadline_t *d, uint32_t at)
{
    d->on = true;
    d->at = at == PT_DEADLINE_NONE ? 0 : at;
}

static void drop_deferred(pt_session_t *s)
{
    deferred_t *d = s->head;
    while (d != NULL) {
        deferred_t *next = d->next;
        pt_rt_event_free(&d->ev);
        free(d);
        d = next;
    }
    s->head = s->tail = NULL;
    s->hold.on = false;
}

pt_session_t *pt_session_new(const pt_port_t *port, pt_log_t *log, const pt_session_hooks_t *hooks)
{
    if (port == NULL || hooks == NULL) {
        return NULL;
    }
    pt_session_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->port = *port;
    s->log = log;
    s->hooks = *hooks;
    return s;
}

static void close_connection(pt_session_t *s)
{
    s->state = ST_NONE;
    s->waiting = WAIT_NONE;
    s->lost = false;
    s->open.on = false;
    s->check.on = false;
    drop_deferred(s);
    if (s->port.rt_close != NULL) {
        s->port.rt_close(s->port.ctx);
    }
}

void pt_session_free(pt_session_t *s)
{
    if (s == NULL) {
        return;
    }
    if (s->state != ST_NONE) {
        s->idle.on = false;
        close_connection(s);
    }
    drop_deferred(s);
    free(s);
}

void pt_session_settings(pt_session_t *s, const pt_settings_t *settings)
{
    if (s == NULL || settings == NULL) {
        return;
    }
    s->fresh = *settings;
    s->have_settings = true;
}

bool pt_session_has_settings(const pt_session_t *s)
{
    return s != NULL && s->have_settings;
}

const pt_settings_t *pt_session_fresh(const pt_session_t *s)
{
    return &s->fresh;
}

const pt_timing_t *pt_session_timing(const pt_session_t *s)
{
    return s->state == ST_READY ? &s->used.timing : &s->fresh.timing;
}

const pt_timing_t *pt_session_fresh_timing(const pt_session_t *s)
{
    return &s->fresh.timing;
}

uint32_t pt_session_gen(const pt_session_t *s)
{
    return s->state == ST_READY ? s->gen : 0;
}

static void send_raw(pt_session_t *s, const char *json)
{
    if (s->port.rt_send != NULL) {
        s->port.rt_send(s->port.ctx, json);
    }
}

bool pt_session_send(pt_session_t *s, uint32_t gen, const char *json)
{
    if (s->state != ST_READY || s->lost || gen == 0 || gen != s->gen || json == NULL) {
        return false;
    }
    send_raw(s, json);
    return true;
}

void pt_session_detach(pt_session_t *s)
{
    if (s->state != ST_READY) {
        return;
    }
    bool forget = s->idle.on;
    s->idle.on = false;
    close_connection(s);
    if (forget && s->hooks.forget != NULL) {
        s->hooks.forget(s->hooks.ctx);
    }
}

static void open_failed(pt_session_t *s, const char *why, uint32_t now_ms)
{
    close_connection(s);
    if (s->hooks.failed != NULL) {
        s->hooks.failed(s->hooks.ctx, why, now_ms);
    }
}

static void open_session(pt_session_t *s, uint32_t now_ms)
{
    if (s->fresh.openai.api_key[0] == '\0') {
        if (s->hooks.failed != NULL) {
            s->hooks.failed(s->hooks.ctx, "the OpenAI API key is not set", now_ms);
        }
        return;
    }
    s->used = s->fresh;
    s->gen++;
    if (s->gen == 0) {
        s->gen = 1;
    }
    s->state = ST_OPENING;
    s->waiting = WAIT_OPEN;
    s->lost = false;
    s->model[0] = '\0';
    set_deadline(&s->open, pt_time_after(now_ms, PT_SESSION_CONNECT_TIMEOUT_MS));
    if (s->port.rt_open != NULL) {
        s->port.rt_open(s->port.ctx, &s->used);
    }
}

static void reconnect(pt_session_t *s, uint32_t now_ms)
{
    pt_log_add(s->log, log_time(now_ms), "realtime session did not answer; reconnecting");
    s->waiting = WAIT_NONE;
    s->check.on = false;
    pt_session_detach(s);
    open_session(s, now_ms);
}

void pt_session_for_turn(pt_session_t *s, bool cancel_previous, uint32_t now_ms)
{
    if (s->state != ST_NONE && s->state != ST_READY) {
        pt_session_abort(s);
    }
    if (s->state == ST_READY && pt_settings_session_changed(&s->used, &s->fresh)) {
        pt_log_add(s->log, log_time(now_ms), "settings changed; opening a fresh realtime session");
        pt_session_detach(s);
    }
    if (s->state == ST_READY && cancel_previous) {
        if (s->lost) {
            pt_log_add(s->log, log_time(now_ms), "realtime cancel failed: realtime session is not open");
            pt_session_detach(s);
        } else {
            send_raw(s, pt_rt_cancel());
        }
    }
    if (s->state == ST_READY && s->lost) {
        pt_session_detach(s);
    }
    if (s->state == ST_READY) {
        s->waiting = WAIT_CHECK;
        set_deadline(&s->check, pt_time_after(now_ms, pt_time_ms(s->used.timing.ready_check_s)));
        send_raw(s, pt_rt_clear());
        return;
    }
    open_session(s, now_ms);
}

void pt_session_abort(pt_session_t *s)
{
    if (s->waiting == WAIT_OPEN) {
        close_connection(s);
    } else if (s->waiting == WAIT_CHECK) {
        s->waiting = WAIT_NONE;
        s->check.on = false;
    }
}

void pt_session_release(pt_session_t *s, uint32_t gen, bool cancel, bool clear, bool drop, uint32_t now_ms)
{
    if (s->state != ST_READY || gen == 0 || gen != s->gen) {
        return;
    }
    if (s->lost) {
        if (cancel || clear) {
            pt_log_add(s->log, log_time(now_ms), "realtime send failed: realtime session is not open");
        }
        drop = true;
    } else {
        if (cancel) {
            send_raw(s, pt_rt_cancel());
        }
        if (clear) {
            send_raw(s, pt_rt_clear());
        }
    }
    if (drop) {
        pt_session_detach(s);
    }
}

void pt_session_close(pt_session_t *s)
{
    if (s->state == ST_NONE) {
        return;
    }
    s->idle.on = false;
    close_connection(s);
    if (s->hooks.forget != NULL) {
        s->hooks.forget(s->hooks.ctx);
    }
}

void pt_session_cancel_idle(pt_session_t *s)
{
    s->idle.on = false;
}

void pt_session_close_when_idle(pt_session_t *s, float delay_s, uint32_t now_ms)
{
    pt_session_cancel_idle(s);
    if (s->state != ST_READY) {
        if (s->hooks.forget != NULL) {
            s->hooks.forget(s->hooks.ctx);
        }
        return;
    }
    set_deadline(&s->idle, pt_time_after(now_ms, pt_time_ms(delay_s)));
}

bool pt_session_hold(pt_session_t *s, uint32_t until_ms)
{
    if (s->state != ST_READY) {
        return false;
    }
    if (!s->hold.on || pt_time_before(s->hold.at, until_ms)) {
        set_deadline(&s->hold, until_ms);
    }
    return true;
}

void pt_session_rt_opened(pt_session_t *s, uint32_t now_ms)
{
    (void)now_ms;
    if (s->state == ST_OPENING) {
        s->state = ST_WAIT_CREATED;
    }
}

void pt_session_rt_failed(pt_session_t *s, const char *why, uint32_t now_ms)
{
    if (s->state == ST_OPENING || s->state == ST_WAIT_CREATED || s->state == ST_WAIT_UPDATED) {
        open_failed(s, why != NULL && why[0] != '\0' ? why : "realtime connection failed", now_ms);
    }
}

static void handshake(pt_session_t *s, const pt_rt_event_t *e, uint32_t now_ms)
{
    if (e->kind == PT_RT_ERROR) {
        char why[192];
        snprintf(why, sizeof(why), "realtime error %s", e->text != NULL ? e->text : "unknown");
        open_failed(s, why, now_ms);
        return;
    }
    if (e->kind != PT_RT_SESSION_READY) {
        return;
    }
    if (s->state == ST_WAIT_CREATED && strcmp(e->status, "created") == 0) {
        snprintf(s->model, sizeof(s->model), "%s", e->model);
        char *update = pt_rt_session_update(&s->used);
        if (update == NULL) {
            open_failed(s, "could not build the session update", now_ms);
            return;
        }
        s->state = ST_WAIT_UPDATED;
        send_raw(s, update);
        free(update);
        return;
    }
    if (s->state == ST_WAIT_UPDATED && strcmp(e->status, "updated") == 0) {
        s->state = ST_READY;
        s->waiting = WAIT_NONE;
        s->open.on = false;
        pt_log_add(s->log, log_time(now_ms), "realtime session ready: model %s, voice %s",
                   s->model[0] != '\0' ? s->model : "?", e->voice[0] != '\0' ? e->voice : "?");
        pt_log_add(s->log, log_time(now_ms), "realtime session opened");
        send_raw(s, pt_rt_clear());
        if (s->hooks.ready != NULL) {
            s->hooks.ready(s->hooks.ctx, now_ms);
        }
    }
}

static void handle_closed(pt_session_t *s, uint32_t now_ms)
{
    pt_log_add(s->log, log_time(now_ms), "realtime session closed");
    s->lost = true;
    if (s->waiting == WAIT_CHECK) {
        reconnect(s, now_ms);
        return;
    }
    uint32_t gen = s->gen;
    if (s->hooks.closed != NULL) {
        s->hooks.closed(s->hooks.ctx, now_ms);
    }
    if (s->state == ST_READY && s->gen == gen) {
        pt_session_detach(s);
    }
}

static void dispatch(pt_session_t *s, const pt_rt_event_t *e, uint32_t now_ms)
{
    switch (e->kind) {
    case PT_RT_IGNORED:
    case PT_RT_SESSION_READY:
        return;
    case PT_RT_ERROR:
        pt_log_add(s->log, log_time(now_ms), "realtime error: %s", e->text != NULL ? e->text : "unknown");
        return;
    case PT_RT_INPUT_CLEARED:
        if (s->waiting == WAIT_CHECK) {
            s->waiting = WAIT_NONE;
            s->check.on = false;
            if (s->hooks.ready != NULL) {
                s->hooks.ready(s->hooks.ctx, now_ms);
            }
        }
        return;
    case PT_RT_RESPONSE_DONE:
        pt_log_add(s->log, log_time(now_ms), "response %s: %s", e->response_id, e->status);
        break;
    default:
        break;
    }
    if (s->hooks.event != NULL) {
        s->hooks.event(s->hooks.ctx, e, now_ms);
    }
}

static void defer(pt_session_t *s, pt_rt_event_t *e, bool closed)
{
    deferred_t *d = calloc(1, sizeof(*d));
    if (d == NULL) {
        pt_rt_event_free(e);
        return;
    }
    d->closed = closed;
    if (e != NULL) {
        d->ev = *e;
    }
    if (s->tail != NULL) {
        s->tail->next = d;
    } else {
        s->head = d;
    }
    s->tail = d;
}

void pt_session_rt_message(pt_session_t *s, const char *json, size_t len, uint32_t now_ms)
{
    if (s->state == ST_NONE || s->state == ST_OPENING) {
        return;
    }
    pt_rt_event_t e;
    pt_rt_parse(json, len, &e);
    if (e.kind == PT_RT_IGNORED) {
        pt_rt_event_free(&e);
        return;
    }
    if (s->state != ST_READY) {
        handshake(s, &e, now_ms);
        pt_rt_event_free(&e);
        return;
    }
    if (s->hold.on) {
        defer(s, &e, false);
        return;
    }
    dispatch(s, &e, now_ms);
    pt_rt_event_free(&e);
}

void pt_session_rt_closed(pt_session_t *s, uint32_t now_ms)
{
    switch (s->state) {
    case ST_NONE:
        return;
    case ST_OPENING:
        open_failed(s, "realtime connection failed", now_ms);
        return;
    case ST_WAIT_CREATED:
        open_failed(s, "realtime socket closed before session.created", now_ms);
        return;
    case ST_WAIT_UPDATED:
        open_failed(s, "realtime socket closed before session.updated", now_ms);
        return;
    case ST_READY:
        break;
    }
    if (s->lost) {
        return;
    }
    if (s->hold.on) {
        s->lost = true;
        defer(s, NULL, true);
        return;
    }
    handle_closed(s, now_ms);
}

static void release_hold(pt_session_t *s, uint32_t now_ms)
{
    s->hold.on = false;
    uint32_t gen = s->gen;
    if (s->hooks.resume != NULL) {
        s->hooks.resume(s->hooks.ctx, now_ms);
    }
    while (!s->hold.on && s->head != NULL && s->state == ST_READY && s->gen == gen) {
        deferred_t *d = s->head;
        s->head = d->next;
        if (s->head == NULL) {
            s->tail = NULL;
        }
        if (d->closed) {
            handle_closed(s, now_ms);
        } else {
            dispatch(s, &d->ev, now_ms);
        }
        pt_rt_event_free(&d->ev);
        free(d);
    }
}

static void idle_close(pt_session_t *s, uint32_t now_ms)
{
    s->idle.on = false;
    pt_log_add(s->log, log_time(now_ms), "closing idle realtime session");
    if (s->hooks.forget != NULL) {
        s->hooks.forget(s->hooks.ctx);
    }
    pt_session_detach(s);
}

static const deadline_t *earliest(const pt_session_t *s)
{
    const deadline_t *order[] = {&s->hold, &s->open, &s->check, &s->idle};
    const deadline_t *best = NULL;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (order[i]->on && (best == NULL || pt_time_before(order[i]->at, best->at))) {
            best = order[i];
        }
    }
    return best;
}

bool pt_session_tick_one(pt_session_t *s, uint32_t now_ms)
{
    const deadline_t *d = earliest(s);
    if (d == NULL || !pt_time_due(d->at, now_ms)) {
        return false;
    }
    if (d == &s->hold) {
        release_hold(s, now_ms);
    } else if (d == &s->open) {
        s->open.on = false;
        char why[64];
        snprintf(why, sizeof(why), "realtime connect timed out after %us", PT_SESSION_CONNECT_TIMEOUT_MS / 1000u);
        open_failed(s, why, now_ms);
    } else if (d == &s->check) {
        reconnect(s, now_ms);
    } else {
        idle_close(s, now_ms);
    }
    return true;
}

uint32_t pt_session_next_deadline(const pt_session_t *s)
{
    const deadline_t *d = earliest(s);
    return d != NULL ? d->at : PT_DEADLINE_NONE;
}
