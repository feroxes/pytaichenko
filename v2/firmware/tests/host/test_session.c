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
    K = pt_session_new(&F.port, LOG, &hooks);
    TEST_ASSERT_NOT_NULL(K);
    F.target.t = K;
    pt_session_settings(K, settings);
}

void setUp(void)
{
    now = 5000;
    readies = failures = 0;
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
    return UNITY_END();
}
