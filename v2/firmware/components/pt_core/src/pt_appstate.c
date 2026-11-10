#include "pt_appstate.h"

#include <stdlib.h>

struct pt_appstate {
    bool wifi;
    bool has_key;
    bool in_convo;
    bool follow_up;
    pt_ring_state_t ring;
};

pt_appstate_t *pt_appstate_new(void)
{
    pt_appstate_t *a = calloc(1, sizeof(*a));
    if (a != NULL) {
        a->ring = PT_RING_NOT_READY;
    }
    return a;
}

void pt_appstate_free(pt_appstate_t *a)
{
    free(a);
}

void pt_appstate_settings(pt_appstate_t *a, bool has_key)
{
    if (a != NULL) {
        a->has_key = has_key;
    }
}

bool pt_appstate_ready(const pt_appstate_t *a)
{
    return a != NULL && a->wifi && a->has_key;
}

bool pt_appstate_in_convo(const pt_appstate_t *a)
{
    return a != NULL && a->in_convo;
}

pt_ring_state_t pt_appstate_ring(const pt_appstate_t *a)
{
    return a != NULL ? a->ring : PT_RING_NOT_READY;
}

static void settle(pt_appstate_t *a, pt_app_out_t *o)
{
    if (!pt_appstate_ready(a)) {
        if (a->in_convo) {
            o->stop_convo = true;
            a->in_convo = false;
            a->follow_up = false;
        }
        a->ring = PT_RING_NOT_READY;
    } else if (!a->in_convo) {
        a->ring = PT_RING_OFF;
    }
}

static void begin(pt_appstate_t *a, pt_app_out_t *o)
{
    a->in_convo = true;
    a->follow_up = false;
    a->ring = PT_RING_LISTENING;
    o->play_wake_sound = true;
    o->start_convo = true;
}

static void finish(pt_appstate_t *a, pt_app_out_t *o)
{
    a->in_convo = false;
    a->follow_up = false;
    a->ring = PT_RING_OFF;
    o->play_end_sound = true;
}

static pt_ring_state_t on_dev(pt_appstate_t *a, const pt_app_ev_t *ev, pt_app_out_t *o)
{
    if (!a->in_convo) {
        return a->ring;
    }
    switch (ev->dev) {
    case PT_DEV_LISTENING:
        a->ring = a->follow_up ? PT_RING_FOLLOW_UP : PT_RING_LISTENING;
        break;
    case PT_DEV_SPEECH_STARTED:
        a->ring = PT_RING_SPEECH;
        break;
    case PT_DEV_SPEECH_STOPPED:
        a->ring = PT_RING_THINKING;
        break;
    case PT_DEV_ANSWER_STREAMING:
        a->ring = PT_RING_ANSWERING;
        break;
    case PT_DEV_ANSWERED:
        if (ev->follow_up) {
            a->follow_up = true;
            a->ring = PT_RING_FOLLOW_UP;
            o->start_convo = true;
        } else {
            finish(a, o);
        }
        break;
    case PT_DEV_ENDED:
        finish(a, o);
        break;
    case PT_DEV_FAILED:
        a->in_convo = false;
        a->follow_up = false;
        a->ring = PT_RING_OFF;
        return PT_RING_ERROR;
    }
    return a->ring;
}

pt_app_out_t pt_appstate_on(pt_appstate_t *a, const pt_app_ev_t *ev)
{
    pt_app_out_t o = {.ring = PT_RING_NOT_READY};
    if (a == NULL || ev == NULL) {
        return o;
    }
    pt_ring_state_t shown = PT_RING_OFF;
    bool one_shot = false;
    switch (ev->kind) {
    case PT_APP_EV_WIFI_UP:
        a->wifi = true;
        settle(a, &o);
        break;
    case PT_APP_EV_WIFI_DOWN:
        a->wifi = false;
        settle(a, &o);
        if (o.stop_convo) {
            shown = PT_RING_ERROR;
            one_shot = true;
        }
        break;
    case PT_APP_EV_SETTINGS:
        settle(a, &o);
        break;
    case PT_APP_EV_WAKE:
        if (pt_appstate_ready(a) && !a->in_convo) {
            begin(a, &o);
        }
        break;
    case PT_APP_EV_BUTTON:
        if (a->in_convo) {
            o.stop_convo = true;
            finish(a, &o);
        } else if (pt_appstate_ready(a)) {
            begin(a, &o);
        }
        break;
    case PT_APP_EV_BUTTON_LONG:
        o.reset_password = true;
        shown = PT_RING_RESET;
        one_shot = true;
        break;
    case PT_APP_EV_DEV:
        shown = on_dev(a, ev, &o);
        one_shot = shown != a->ring;
        break;
    }
    o.ring = one_shot ? shown : a->ring;
    o.wake_listening = pt_appstate_ready(a) && !a->in_convo;
    return o;
}

size_t pt_app_feed(const pt_app_sink_t *sink, const int16_t *pcm, size_t n)
{
    if (sink == NULL || sink->push == NULL || pcm == NULL) {
        return 0;
    }
    size_t done = 0;
    while (done < n) {
        done += sink->push(sink->ctx, pcm + done, n - done);
        if (done >= n || sink->wait == NULL || !sink->wait(sink->ctx)) {
            break;
        }
    }
    return done;
}
