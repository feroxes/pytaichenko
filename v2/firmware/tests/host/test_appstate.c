#include "unity.h"

#include <stdlib.h>
#include <string.h>

#include "pt_appstate.h"

static pt_appstate_t *A;

void setUp(void)
{
    A = pt_appstate_new();
    TEST_ASSERT_NOT_NULL(A);
}

void tearDown(void)
{
    pt_appstate_free(A);
    A = NULL;
}

static pt_app_out_t on(pt_app_ev_kind_t kind)
{
    pt_app_ev_t ev = {.kind = kind};
    return pt_appstate_on(A, &ev);
}

static pt_app_out_t dev(pt_dev_event_t d, bool follow_up, const char *code)
{
    pt_app_ev_t ev = {.kind = PT_APP_EV_DEV, .dev = d, .follow_up = follow_up, .code = code};
    return pt_appstate_on(A, &ev);
}

static pt_app_out_t settings(bool has_key)
{
    pt_appstate_settings(A, has_key);
    return on(PT_APP_EV_SETTINGS);
}

static void assert_quiet(pt_app_out_t o)
{
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_FALSE(o.play_wake_sound);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.reset_password);
}

static void make_ready(void)
{
    on(PT_APP_EV_WIFI_UP);
    pt_app_out_t o = settings(true);
    TEST_ASSERT_TRUE(pt_appstate_ready(A));
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    assert_quiet(o);
}

static void start_by_wake(void)
{
    pt_app_out_t o = on(PT_APP_EV_WAKE);
    TEST_ASSERT_TRUE(o.start_convo);
    TEST_ASSERT_TRUE(o.play_wake_sound);
    TEST_ASSERT_EQUAL_INT(PT_RING_LISTENING, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_TRUE(pt_appstate_in_convo(A));
    o = dev(PT_DEV_LISTENING, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_LISTENING, o.ring);
    assert_quiet(o);
}

static void assert_back_to_wake(pt_app_out_t o)
{
    TEST_ASSERT_TRUE(o.play_end_sound);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_FALSE(o.play_wake_sound);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));
}

void test_app_without_key_stays_idle_not_ready(void)
{
    pt_app_out_t o = on(PT_APP_EV_SETTINGS);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    o = on(PT_APP_EV_WIFI_UP);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_FALSE(pt_appstate_ready(A));
    o = on(PT_APP_EV_WAKE);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    o = on(PT_APP_EV_BUTTON);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));

    o = settings(true);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    o = on(PT_APP_EV_WIFI_DOWN);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    assert_quiet(o);
    o = on(PT_APP_EV_BUTTON);
    assert_quiet(o);

    on(PT_APP_EV_WIFI_UP);
    start_by_wake();
    o = settings(false);
    TEST_ASSERT_TRUE(o.stop_convo);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));
}

void test_app_wake_starts_listening_with_sound(void)
{
    make_ready();
    start_by_wake();
    pt_app_out_t o = on(PT_APP_EV_WAKE);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_LISTENING, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
}

void test_app_button_toggles(void)
{
    make_ready();
    pt_app_out_t o = on(PT_APP_EV_BUTTON);
    TEST_ASSERT_TRUE(o.start_convo);
    TEST_ASSERT_TRUE(o.play_wake_sound);
    TEST_ASSERT_EQUAL_INT(PT_RING_LISTENING, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    dev(PT_DEV_LISTENING, false, NULL);
    o = dev(PT_DEV_SPEECH_STARTED, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_SPEECH, o.ring);

    o = on(PT_APP_EV_BUTTON);
    TEST_ASSERT_TRUE(o.stop_convo);
    TEST_ASSERT_TRUE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_FALSE(o.play_wake_sound);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));

    o = dev(PT_DEV_ENDED, false, NULL);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);

    o = on(PT_APP_EV_BUTTON);
    TEST_ASSERT_TRUE(o.start_convo);
    TEST_ASSERT_TRUE(o.play_wake_sound);
    dev(PT_DEV_LISTENING, false, NULL);
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    dev(PT_DEV_SPEECH_STOPPED, false, NULL);
    o = dev(PT_DEV_ANSWERED, true, NULL);
    TEST_ASSERT_TRUE(o.start_convo);
    o = on(PT_APP_EV_BUTTON);
    TEST_ASSERT_TRUE(o.stop_convo);
    TEST_ASSERT_TRUE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_TRUE(o.wake_listening);
}

void test_app_follow_up_restarts_without_wake(void)
{
    make_ready();
    start_by_wake();
    pt_app_out_t o = dev(PT_DEV_SPEECH_STARTED, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_SPEECH, o.ring);
    o = dev(PT_DEV_SPEECH_STOPPED, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_THINKING, o.ring);
    o = dev(PT_DEV_ANSWER_STREAMING, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_ANSWERING, o.ring);
    assert_quiet(o);
    TEST_ASSERT_FALSE(o.wake_listening);

    o = dev(PT_DEV_ANSWERED, true, NULL);
    TEST_ASSERT_TRUE(o.start_convo);
    TEST_ASSERT_FALSE(o.play_wake_sound);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_EQUAL_INT(PT_RING_FOLLOW_UP, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_TRUE(pt_appstate_in_convo(A));

    o = dev(PT_DEV_LISTENING, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_FOLLOW_UP, o.ring);
    assert_quiet(o);
    o = dev(PT_DEV_SPEECH_STARTED, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_SPEECH, o.ring);
    dev(PT_DEV_SPEECH_STOPPED, false, NULL);
    dev(PT_DEV_ANSWER_STREAMING, false, NULL);
    o = dev(PT_DEV_ANSWERED, true, NULL);
    TEST_ASSERT_TRUE(o.start_convo);
    TEST_ASSERT_FALSE(o.play_wake_sound);
    o = dev(PT_DEV_LISTENING, false, NULL);
    TEST_ASSERT_EQUAL_INT(PT_RING_FOLLOW_UP, o.ring);
    o = dev(PT_DEV_ENDED, false, NULL);
    assert_back_to_wake(o);

    start_by_wake();
    TEST_ASSERT_EQUAL_INT(PT_RING_LISTENING, pt_appstate_ring(A));
}

void test_app_end_plays_end_sound_and_rearms_wake(void)
{
    make_ready();
    start_by_wake();
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    dev(PT_DEV_SPEECH_STOPPED, false, NULL);
    dev(PT_DEV_ANSWER_STREAMING, false, NULL);
    pt_app_out_t o = dev(PT_DEV_ANSWERED, false, NULL);
    assert_back_to_wake(o);

    o = dev(PT_DEV_ANSWERED, false, NULL);
    assert_quiet(o);
    TEST_ASSERT_TRUE(o.wake_listening);

    start_by_wake();
    o = dev(PT_DEV_ENDED, false, NULL);
    assert_back_to_wake(o);
}

void test_app_failed_shows_error_then_listens(void)
{
    make_ready();
    start_by_wake();
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    dev(PT_DEV_SPEECH_STOPPED, false, NULL);
    pt_app_out_t o = dev(PT_DEV_FAILED, false, "timeout");
    TEST_ASSERT_EQUAL_INT(PT_RING_ERROR, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, pt_appstate_ring(A));

    o = on(PT_APP_EV_SETTINGS);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    assert_quiet(o);

    start_by_wake();
    o = dev(PT_DEV_FAILED, false, "openai-unavailable");
    TEST_ASSERT_EQUAL_INT(PT_RING_ERROR, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
}

void test_app_wifi_lost_mid_turn_recovers(void)
{
    make_ready();
    start_by_wake();
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    pt_app_out_t o = on(PT_APP_EV_WIFI_DOWN);
    TEST_ASSERT_TRUE(o.stop_convo);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_EQUAL_INT(PT_RING_ERROR, o.ring);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, pt_appstate_ring(A));
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_FALSE(pt_appstate_in_convo(A));

    o = dev(PT_DEV_FAILED, false, "openai-unavailable");
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    TEST_ASSERT_FALSE(o.wake_listening);
    o = dev(PT_DEV_ENDED, false, NULL);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);
    o = on(PT_APP_EV_WAKE);
    assert_quiet(o);
    o = on(PT_APP_EV_WIFI_DOWN);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);

    o = on(PT_APP_EV_WIFI_UP);
    assert_quiet(o);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    start_by_wake();
    o = dev(PT_DEV_ANSWERED, false, NULL);
    assert_back_to_wake(o);
}

void test_app_wake_word_change_applies_immediately(void)
{
    make_ready();
    pt_app_out_t o = settings(true);
    assert_quiet(o);
    TEST_ASSERT_TRUE(o.wake_listening);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);

    start_by_wake();
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    o = settings(true);
    assert_quiet(o);
    TEST_ASSERT_FALSE(o.wake_listening);
    TEST_ASSERT_EQUAL_INT(PT_RING_SPEECH, o.ring);
    TEST_ASSERT_TRUE(pt_appstate_in_convo(A));
    o = dev(PT_DEV_ENDED, false, NULL);
    assert_back_to_wake(o);
    o = settings(true);
    TEST_ASSERT_TRUE(o.wake_listening);
}

void test_app_long_press_resets_password(void)
{
    pt_app_out_t o = on(PT_APP_EV_BUTTON_LONG);
    TEST_ASSERT_TRUE(o.reset_password);
    TEST_ASSERT_EQUAL_INT(PT_RING_RESET, o.ring);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, pt_appstate_ring(A));
    o = on(PT_APP_EV_SETTINGS);
    TEST_ASSERT_FALSE(o.reset_password);
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, o.ring);

    make_ready();
    o = on(PT_APP_EV_BUTTON_LONG);
    TEST_ASSERT_TRUE(o.reset_password);
    TEST_ASSERT_EQUAL_INT(PT_RING_RESET, o.ring);
    TEST_ASSERT_TRUE(o.wake_listening);
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, pt_appstate_ring(A));

    start_by_wake();
    dev(PT_DEV_SPEECH_STARTED, false, NULL);
    o = on(PT_APP_EV_BUTTON_LONG);
    TEST_ASSERT_TRUE(o.reset_password);
    TEST_ASSERT_FALSE(o.stop_convo);
    TEST_ASSERT_FALSE(o.play_end_sound);
    TEST_ASSERT_TRUE(pt_appstate_in_convo(A));
    TEST_ASSERT_EQUAL_INT(PT_RING_SPEECH, pt_appstate_ring(A));
}

#define SPEAKER_CAP 144000u
#define DRAIN_PER_WAIT 240u
#define DELTA 4800u
#define ANSWER_SAMPLES (24000u * 120u)

typedef struct {
    int16_t *buf;
    size_t head, len, max_len;
    size_t consumed;
    int16_t expect;
    bool in_order;
    size_t waits;
    size_t abort_after;
} sim_t;

static size_t sim_push(void *ctx, const int16_t *pcm, size_t n)
{
    sim_t *s = ctx;
    size_t room = SPEAKER_CAP - s->len;
    size_t k = n < room ? n : room;
    for (size_t i = 0; i < k; i++) {
        s->buf[(s->head + s->len + i) % SPEAKER_CAP] = pcm[i];
    }
    s->len += k;
    if (s->len > s->max_len) {
        s->max_len = s->len;
    }
    return k;
}

static void sim_drain(sim_t *s, size_t n)
{
    for (size_t i = 0; i < n && s->len > 0; i++) {
        if (s->buf[s->head] != s->expect) {
            s->in_order = false;
        }
        s->expect = (int16_t)(s->expect + 1);
        s->head = (s->head + 1) % SPEAKER_CAP;
        s->len--;
        s->consumed++;
    }
}

static bool sim_wait(void *ctx)
{
    sim_t *s = ctx;
    s->waits++;
    if (s->abort_after != 0 && s->waits >= s->abort_after) {
        return false;
    }
    sim_drain(s, DRAIN_PER_WAIT);
    return true;
}

static void sim_init(sim_t *s)
{
    memset(s, 0, sizeof(*s));
    s->buf = malloc(SPEAKER_CAP * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(s->buf);
    s->in_order = true;
}

void test_app_long_answer_keeps_memory_bounded(void)
{
    sim_t s;
    sim_init(&s);
    pt_app_sink_t sink = {.push = sim_push, .wait = sim_wait, .ctx = &s};
    int16_t delta[DELTA];
    int16_t next = 0;
    size_t produced = 0;
    while (produced < ANSWER_SAMPLES) {
        for (size_t i = 0; i < DELTA; i++) {
            delta[i] = next;
            next = (int16_t)(next + 1);
        }
        TEST_ASSERT_EQUAL_size_t(DELTA, pt_app_feed(&sink, delta, DELTA));
        produced += DELTA;
        TEST_ASSERT_TRUE(s.len <= SPEAKER_CAP);
    }
    TEST_ASSERT_TRUE(s.max_len <= SPEAKER_CAP);
    TEST_ASSERT_EQUAL_size_t(SPEAKER_CAP, s.max_len);
    TEST_ASSERT_TRUE(s.waits > 0);
    sim_drain(&s, s.len);
    TEST_ASSERT_EQUAL_size_t(ANSWER_SAMPLES, s.consumed);
    TEST_ASSERT_TRUE(s.in_order);
    free(s.buf);
}

void test_app_feed_gives_up_when_wait_aborts(void)
{
    sim_t s;
    sim_init(&s);
    s.abort_after = 3;
    pt_app_sink_t sink = {.push = sim_push, .wait = sim_wait, .ctx = &s};
    int16_t *big = calloc(SPEAKER_CAP + 10000, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(big);
    size_t done = pt_app_feed(&sink, big, SPEAKER_CAP + 10000);
    TEST_ASSERT_EQUAL_size_t(SPEAKER_CAP + 2 * DRAIN_PER_WAIT, done);
    TEST_ASSERT_EQUAL_size_t(3, s.waits);
    TEST_ASSERT_EQUAL_size_t(0, pt_app_feed(&sink, big, 0));
    TEST_ASSERT_EQUAL_size_t(0, pt_app_feed(NULL, big, 10));
    free(big);
    free(s.buf);
}

void test_app_null_state_is_safe(void)
{
    pt_app_ev_t ev = {.kind = PT_APP_EV_WAKE};
    pt_app_out_t o = pt_appstate_on(NULL, &ev);
    assert_quiet(o);
    o = pt_appstate_on(A, NULL);
    assert_quiet(o);
    pt_appstate_settings(NULL, true);
    pt_appstate_free(NULL);
    TEST_ASSERT_FALSE(pt_appstate_ready(NULL));
    TEST_ASSERT_FALSE(pt_appstate_in_convo(NULL));
    TEST_ASSERT_EQUAL_INT(PT_RING_NOT_READY, pt_appstate_ring(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_without_key_stays_idle_not_ready);
    RUN_TEST(test_app_wake_starts_listening_with_sound);
    RUN_TEST(test_app_button_toggles);
    RUN_TEST(test_app_follow_up_restarts_without_wake);
    RUN_TEST(test_app_end_plays_end_sound_and_rearms_wake);
    RUN_TEST(test_app_failed_shows_error_then_listens);
    RUN_TEST(test_app_wifi_lost_mid_turn_recovers);
    RUN_TEST(test_app_wake_word_change_applies_immediately);
    RUN_TEST(test_app_long_press_resets_password);
    RUN_TEST(test_app_long_answer_keeps_memory_bounded);
    RUN_TEST(test_app_feed_gives_up_when_wait_aborts);
    RUN_TEST(test_app_null_state_is_safe);
    return UNITY_END();
}
