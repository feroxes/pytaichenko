#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fake_port.h"
#include "pt_log.h"
#include "pt_session.h"

static uint32_t now;
static fake_port_t F;
static pt_session_t *K;
static pt_log_t *LOG;
static pt_settings_t *OLD;
static pt_settings_t *NEW;
static int readies;
static int failures;
static int closes;
static char last_failure[128];
static char forgotten[64];

static void t_opened(void *t, uint32_t at)
{
    pt_session_rt_opened(t, at);
}

static void t_failed(void *t, const char *why, uint32_t at)
{
    pt_session_rt_failed(t, why, at);
}

static void t_message(void *t, const char *json, size_t len, uint32_t at)
{
    pt_session_rt_message(t, json, len, at);
}

static void t_closed(void *t, uint32_t at)
{
    pt_session_rt_closed(t, at);
}

static void h_ready(void *ctx, uint32_t at)
{
    (void)ctx;
    (void)at;
    readies++;
}

static void h_failed(void *ctx, const char *why, uint32_t at)
{
    (void)ctx;
    (void)at;
    failures++;
    snprintf(last_failure, sizeof(last_failure), "%s", why);
}

static void h_closed(void *ctx, uint32_t at)
{
    (void)ctx;
    (void)at;
    closes++;
}

static void h_forget(void *ctx)
{
    (void)ctx;
    strncat(forgotten, "idle,", sizeof(forgotten) - strlen(forgotten) - 1);
}

static void make(const pt_settings_t *settings)
{
    pt_session_hooks_t hooks = {0};
    hooks.ready = h_ready;
    hooks.failed = h_failed;
    hooks.forget = h_forget;
    hooks.closed = h_closed;
    K = pt_session_new(&F.port, LOG, &hooks);
    TEST_ASSERT_NOT_NULL(K);
    F.target.t = K;
    pt_session_settings(K, settings);
}

void setUp(void)
{
    now = 5000;
    readies = failures = closes = 0;
    last_failure[0] = '\0';
    forgotten[0] = '\0';
    OLD = malloc(sizeof(*OLD));
    NEW = malloc(sizeof(*NEW));
    TEST_ASSERT_NOT_NULL(OLD);
    TEST_ASSERT_NOT_NULL(NEW);
    pt_settings_defaults(OLD, "uk");
    snprintf(OLD->openai.api_key, sizeof(OLD->openai.api_key), "sk-test");
    OLD->timing.first_turn_s = 5.0f;
    OLD->timing.ready_check_s = 1.0f;
    *NEW = *OLD;
    NEW->timing.first_turn_s = 0.2f;
    LOG = pt_log_new(200, 200);
    fp_target_t target = {t_opened, t_failed, t_message, t_closed, NULL};
    fp_init(&F, &now, &target);
    K = NULL;
}

void tearDown(void)
{
    pt_session_free(K);
    K = NULL;
    fp_free(&F);
    pt_log_free(LOG);
    free(OLD);
    free(NEW);
}

static uint32_t turn(void)
{
    int before = readies;
    pt_session_for_turn(K, false, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(before + 1, readies);
    return pt_session_gen(K);
}

static void advance(uint32_t ms)
{
    uint32_t target = now + ms;
    for (int guard = 0; guard < 1000; guard++) {
        uint32_t d = pt_session_next_deadline(K);
        if (d == PT_DEADLINE_NONE || !pt_time_due(d, target)) {
            break;
        }
        if (pt_time_before(now, d)) {
            now = d;
        }
        TEST_ASSERT_TRUE(pt_session_tick_one(K, now));
        fp_pump(&F);
    }
    now = target;
}

static void assert_timing(const pt_settings_t *expected, const pt_timing_t *actual)
{
    TEST_ASSERT_EQUAL_MEMORY(&expected->timing, actual, sizeof(pt_timing_t));
}

static bool log_has(const char *text)
{
    char *json = pt_log_json(LOG, 0);
    cJSON *root = cJSON_Parse(json);
    free(json);
    bool found = false;
    const cJSON *line;
    cJSON_ArrayForEach(line, cJSON_GetObjectItemCaseSensitive(root, "lines"))
    {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(line, "text");
        if (cJSON_IsString(t) && strcmp(t->valuestring, text) == 0) {
            found = true;
        }
    }
    cJSON_Delete(root);
    return found;
}

static bool log_mentions(const char *text)
{
    char *json = pt_log_json(LOG, 0);
    bool found = strstr(json, text) != NULL;
    free(json);
    return found;
}

void test_fixed_timing_is_used_as_is(void)
{
    make(OLD);
    assert_timing(OLD, pt_session_timing(K));
    turn();
    assert_timing(OLD, pt_session_timing(K));
}

void test_timing_is_read_fresh_while_no_session_is_open(void)
{
    make(OLD);
    assert_timing(OLD, pt_session_timing(K));
    pt_session_settings(K, NEW);
    assert_timing(NEW, pt_session_timing(K));
}

void test_changed_timing_opens_a_fresh_session_on_the_next_turn(void)
{
    make(OLD);
    uint32_t first = turn();
    TEST_ASSERT_EQUAL_UINT32(first, turn());
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    pt_session_settings(K, NEW);
    assert_timing(OLD, pt_session_timing(K));
    uint32_t second = turn();
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, fp_nth(&F, FP_RT_OPEN, 0)->first_turn_s);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, fp_nth(&F, FP_RT_OPEN, 1)->first_turn_s);
    assert_timing(NEW, pt_session_timing(K));
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
}

void test_changed_openai_settings_open_a_fresh_session_on_the_next_turn(void)
{
    make(OLD);
    uint32_t first = turn();
    snprintf(NEW->openai.voice, sizeof(NEW->openai.voice), "marin");
    *NEW = *OLD;
    pt_session_settings(K, NEW);
    TEST_ASSERT_EQUAL_UINT32(first, turn());
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    snprintf(NEW->openai.voice, sizeof(NEW->openai.voice), "cedar");
    pt_session_settings(K, NEW);
    uint32_t second = turn();
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_STRING("cedar", fp_last(&F, FP_RT_OPEN)->voice);
    TEST_ASSERT_EQUAL_UINT32(second, turn());
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

void test_outdated_session_closes_in_the_background(void)
{
    make(OLD);
    uint32_t first = turn();
    pt_session_settings(K, NEW);
    uint32_t started = now;
    uint32_t second = turn();
    TEST_ASSERT_EQUAL_UINT32(started, now);
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    const fp_call_t *close = fp_nth(&F, FP_RT_CLOSE, 0);
    TEST_ASSERT_NOT_NULL(close);
    TEST_ASSERT_EQUAL_INT(1, close->conn);
    TEST_ASSERT_TRUE(close < fp_nth(&F, FP_RT_OPEN, 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_fresh_timing_is_what_the_next_turn_will_use(void)
{
    make(OLD);
    turn();
    pt_session_settings(K, NEW);
    assert_timing(OLD, pt_session_timing(K));
    assert_timing(NEW, pt_session_fresh_timing(K));
    turn();
    assert_timing(NEW, pt_session_timing(K));
    assert_timing(NEW, pt_session_fresh_timing(K));
}

void test_dropped_session_forgets_what_the_idle_timer_would_forget(void)
{
    make(OLD);
    turn();
    pt_session_close_when_idle(K, 5.0f, now);
    pt_session_detach(K);
    TEST_ASSERT_EQUAL_STRING("idle,", forgotten);
    pt_session_free(K);
    K = NULL;
    TEST_ASSERT_EQUAL_STRING("idle,", forgotten);
}

void test_idle_timer_without_a_session_forgets_at_once(void)
{
    make(OLD);
    pt_session_close_when_idle(K, 5.0f, now);
    TEST_ASSERT_EQUAL_STRING("idle,", forgotten);
}

void test_a_new_turn_cancels_the_idle_timer_without_forgetting(void)
{
    make(OLD);
    turn();
    pt_session_close_when_idle(K, 0.05f, now);
    pt_session_cancel_idle(K);
    turn();
    advance(100);
    TEST_ASSERT_EQUAL_STRING("", forgotten);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
}

void test_idle_timer_forgets_once_and_closes_the_session(void)
{
    make(OLD);
    turn();
    pt_session_close_when_idle(K, 0.05f, now);
    advance(49);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(1);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_STRING("idle,", forgotten);
    TEST_ASSERT_EQUAL_UINT32(0, pt_session_gen(K));
    pt_session_free(K);
    K = NULL;
    TEST_ASSERT_EQUAL_STRING("idle,", forgotten);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_failed_open_keeps_reading_fresh_timing(void)
{
    make(OLD);
    F.conn[1].fail_connect = true;
    pt_session_for_turn(K, false, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(1, failures);
    TEST_ASSERT_EQUAL_INT(0, readies);
    pt_session_settings(K, NEW);
    assert_timing(NEW, pt_session_timing(K));
}

void test_connect_fails_on_rejected_update(void)
{
    make(OLD);
    F.conn[1].reject_update = true;
    pt_session_for_turn(K, false, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(1, failures);
    TEST_ASSERT_NOT_NULL(strstr(last_failure, "bad session"));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_UINT32(0, pt_session_gen(K));
}

void test_handshake_waits_for_created_then_updated(void)
{
    make(OLD);
    F.conn[1].manual = true;
    pt_session_for_turn(K, false, now);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_SEND));
    pt_session_rt_opened(K, now);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_SEND));
    const char *updated = "{\"type\":\"session.updated\",\"session\":{\"audio\":{\"output\":{\"voice\":\"marin\"}}}}";
    pt_session_rt_message(K, updated, strlen(updated), now);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_SEND));
    const char *created = "{\"type\":\"session.created\",\"session\":{\"model\":\"gpt-realtime-2.1-mini\"}}";
    pt_session_rt_message(K, created, strlen(created), now);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 1));
    TEST_ASSERT_EQUAL_INT(0, readies);
    pt_session_rt_message(K, updated, strlen(updated), now);
    TEST_ASSERT_EQUAL_INT(1, readies);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "input_audio_buffer.clear", 1));
    TEST_ASSERT_TRUE(log_has("realtime session ready: model gpt-realtime-2.1-mini, voice marin"));
    TEST_ASSERT_FALSE(log_mentions("sk-test"));
}

void test_connect_times_out_after_ten_seconds(void)
{
    make(OLD);
    F.conn[1].manual = true;
    pt_session_for_turn(K, false, now);
    pt_session_rt_opened(K, now);
    advance(PT_SESSION_CONNECT_TIMEOUT_MS - 1);
    TEST_ASSERT_EQUAL_INT(0, failures);
    advance(1);
    TEST_ASSERT_EQUAL_INT(1, failures);
    TEST_ASSERT_EQUAL_STRING("realtime connect timed out after 10s", last_failure);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_UINT32(PT_DEADLINE_NONE, pt_session_next_deadline(K));
}

static void make_live(void)
{
    snprintf(OLD->openai.model, sizeof(OLD->openai.model), "gpt-live-1");
    snprintf(OLD->openai.voice, sizeof(OLD->openai.voice), "marin");
    make(OLD);
}

static const char *sent_json(const char *type, int conn, size_t nth)
{
    size_t seen = 0;
    for (size_t i = 0; i < F.n; i++) {
        const fp_call_t *c = &F.calls[i];
        if (c->kind == FP_RT_SEND && c->conn == conn && strcmp(c->type, type) == 0 && seen++ == nth) {
            return c->json;
        }
    }
    return NULL;
}

static int start_input_items(size_t nth)
{
    cJSON *root = cJSON_Parse(sent_json("session.start", 0 + (int)(nth + 1), 0));
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    int n = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
    cJSON_Delete(root);
    return n;
}

void test_live_opens_with_session_start(void)
{
    make_live();
    turn();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.update", 0));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "input_audio_buffer.clear", 0));
    TEST_ASSERT_EQUAL_INT(1, readies);
    TEST_ASSERT_TRUE(pt_session_live(K));
    TEST_ASSERT_TRUE(log_has("live session ready: model gpt-live-1, voice marin"));
    TEST_ASSERT_TRUE(log_has("live session opened"));
    TEST_ASSERT_FALSE(log_has("realtime session opened"));
}

void test_live_error_before_started_fails_fast(void)
{
    make_live();
    F.conn[1].live_reject = true;
    pt_session_for_turn(K, false, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(1, failures);
    TEST_ASSERT_EQUAL_INT(0, readies);
    TEST_ASSERT_NOT_NULL(strstr(last_failure, "live error"));
    TEST_ASSERT_NULL(strstr(last_failure, "realtime"));
    TEST_ASSERT_NOT_NULL(strstr(last_failure, "invalid_value"));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

static void message(const char *json)
{
    pt_session_rt_message(K, json, strlen(json), now);
}

void test_live_helper_error_before_started_is_not_fatal(void)
{
    make_live();
    F.conn[1].manual = true;
    pt_session_for_turn(K, false, now);
    pt_session_rt_opened(K, now);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
    message("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":"
            "\"responses_validation_failed\",\"message\":\"Unsupported value: 'minimal'\"}}");
    TEST_ASSERT_EQUAL_INT(0, failures);
    TEST_ASSERT_EQUAL_INT(0, readies);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_TRUE(log_has("live error: responses_validation_failed: Unsupported value: 'minimal'"));
    message("{\"type\":\"session.started\",\"session\":{\"model\":\"gpt-live-1\",\"audio\":{\"output\":{\"voice\":"
            "\"marin\"}}}}");
    TEST_ASSERT_EQUAL_INT(1, readies);
    TEST_ASSERT_EQUAL_INT(0, failures);
    TEST_ASSERT_TRUE(pt_session_gen(K) != 0);
}

void test_live_other_error_before_started_stays_fatal(void)
{
    make_live();
    F.conn[1].manual = true;
    pt_session_for_turn(K, false, now);
    pt_session_rt_opened(K, now);
    message("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":\"invalid_value\","
            "\"message\":\"bad voice\"}}");
    TEST_ASSERT_EQUAL_INT(1, failures);
    TEST_ASSERT_EQUAL_STRING("live error invalid_value: bad voice", last_failure);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_live_logs_use_live_wording(void)
{
    make_live();
    turn();
    fp_push(&F, "{\"type\":\"error\",\"error\":{\"code\":\"invalid_value\",\"message\":\"bad\"}}");
    TEST_ASSERT_TRUE(log_has("live error: invalid_value: bad"));
    TEST_ASSERT_FALSE(log_mentions("realtime error"));
    *NEW = *OLD;
    snprintf(NEW->openai.voice, sizeof(NEW->openai.voice), "cedar");
    pt_session_settings(K, NEW);
    turn();
    TEST_ASSERT_TRUE(log_has("settings changed; opening a fresh live session"));
    TEST_ASSERT_FALSE(log_mentions("realtime"));
}

void test_live_connect_timeout_uses_live_wording(void)
{
    make_live();
    F.conn[1].manual = true;
    pt_session_for_turn(K, false, now);
    pt_session_rt_opened(K, now);
    advance(PT_SESSION_CONNECT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_STRING("live connect timed out after 10s", last_failure);
}

void test_realtime_logs_keep_realtime_wording(void)
{
    make(OLD);
    turn();
    fp_push(&F, "{\"type\":\"error\",\"error\":{\"code\":\"invalid_value\",\"message\":\"bad\"}}");
    TEST_ASSERT_TRUE(log_has("realtime error: invalid_value: bad"));
    pt_session_settings(K, NEW);
    turn();
    TEST_ASSERT_TRUE(log_has("settings changed; opening a fresh realtime session"));
}

void test_live_second_turn_sends_no_realtime_messages(void)
{
    make_live();
    uint32_t first = turn();
    TEST_ASSERT_EQUAL_UINT32(first, turn());
    pt_session_for_turn(K, true, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(3, readies);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "input_audio_buffer.clear", 0));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.cancel", 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 0));
    pt_session_release(K, first, true, true, false, now);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.cancel", 0));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "input_audio_buffer.clear", 0));
}

void test_live_history_goes_into_next_start_once(void)
{
    make_live();
    pt_rt_msg_t history[2] = {{"user", "hello"}, {"assistant", "hi"}};
    pt_session_set_history(K, history, 2);
    turn();
    TEST_ASSERT_EQUAL_INT(2, start_input_items(0));
    pt_session_end_live(K);
    turn();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 2));
    TEST_ASSERT_EQUAL_INT(0, start_input_items(1));
}

void test_live_close_when_idle_closes_now(void)
{
    make_live();
    turn();
    pt_session_close_when_idle(K, 300.0f, now);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_UINT32(0, pt_session_gen(K));
}

void test_live_closed_event_calls_closed_hook(void)
{
    make_live();
    turn();
    fp_push(&F, "{\"type\":\"session.closed\",\"reason\":\"expired\"}");
    TEST_ASSERT_EQUAL_INT(1, closes);
    TEST_ASSERT_TRUE(log_has("live session closed: expired"));
}

void test_switch_realtime_to_live(void)
{
    make(OLD);
    turn();
    TEST_ASSERT_FALSE(pt_session_live(K));
    snprintf(NEW->openai.model, sizeof(NEW->openai.model), "gpt-live-1");
    pt_session_settings(K, NEW);
    turn();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 2));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.update", 2));
    TEST_ASSERT_TRUE(pt_session_live(K));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fixed_timing_is_used_as_is);
    RUN_TEST(test_timing_is_read_fresh_while_no_session_is_open);
    RUN_TEST(test_changed_timing_opens_a_fresh_session_on_the_next_turn);
    RUN_TEST(test_changed_openai_settings_open_a_fresh_session_on_the_next_turn);
    RUN_TEST(test_outdated_session_closes_in_the_background);
    RUN_TEST(test_fresh_timing_is_what_the_next_turn_will_use);
    RUN_TEST(test_dropped_session_forgets_what_the_idle_timer_would_forget);
    RUN_TEST(test_idle_timer_without_a_session_forgets_at_once);
    RUN_TEST(test_a_new_turn_cancels_the_idle_timer_without_forgetting);
    RUN_TEST(test_idle_timer_forgets_once_and_closes_the_session);
    RUN_TEST(test_failed_open_keeps_reading_fresh_timing);
    RUN_TEST(test_connect_fails_on_rejected_update);
    RUN_TEST(test_handshake_waits_for_created_then_updated);
    RUN_TEST(test_connect_times_out_after_ten_seconds);
    RUN_TEST(test_live_opens_with_session_start);
    RUN_TEST(test_live_error_before_started_fails_fast);
    RUN_TEST(test_live_helper_error_before_started_is_not_fatal);
    RUN_TEST(test_live_other_error_before_started_stays_fatal);
    RUN_TEST(test_live_logs_use_live_wording);
    RUN_TEST(test_live_connect_timeout_uses_live_wording);
    RUN_TEST(test_realtime_logs_keep_realtime_wording);
    RUN_TEST(test_live_second_turn_sends_no_realtime_messages);
    RUN_TEST(test_live_history_goes_into_next_start_once);
    RUN_TEST(test_live_close_when_idle_closes_now);
    RUN_TEST(test_live_closed_event_calls_closed_hook);
    RUN_TEST(test_switch_realtime_to_live);
    return UNITY_END();
}
