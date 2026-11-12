#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fake_port.h"
#include "pt_log.h"
#include "pt_session.h"
#include "pt_turn.h"

#define L PT_DEV_LISTENING
#define SS PT_DEV_SPEECH_STARTED
#define SP PT_DEV_SPEECH_STOPPED
#define AS PT_DEV_ANSWER_STREAMING
#define AN PT_DEV_ANSWERED
#define EN PT_DEV_ENDED
#define FA PT_DEV_FAILED
#define CHUNK 512
#define STEP 2400
#define PI 3.14159265358979323846

static uint32_t now;
static fake_port_t F;
static pt_convo_t *C;
static pt_log_t *LOG;
static pt_settings_t *S;
static int16_t TONE[CHUNK];

static void t_opened(void *t, uint32_t at)
{
    pt_convo_rt_opened(t, at);
}

static void t_failed(void *t, const char *why, uint32_t at)
{
    pt_convo_rt_failed(t, why, at);
}

static void t_message(void *t, const char *json, size_t len, uint32_t at)
{
    pt_convo_rt_message(t, json, len, at);
}

static void t_closed(void *t, uint32_t at)
{
    pt_convo_rt_closed(t, at);
}

static void apply(void)
{
    pt_convo_settings(C, S, now);
    fp_pump(&F);
}

static void use_model(const char *model)
{
    snprintf(S->openai.model, sizeof(S->openai.model), "%s", model);
    apply();
}

void setUp(void)
{
    now = 1000;
    S = malloc(sizeof(*S));
    TEST_ASSERT_NOT_NULL(S);
    pt_settings_defaults(S, "uk");
    snprintf(S->openai.api_key, sizeof(S->openai.api_key), "sk-test");
    snprintf(S->openai.model, sizeof(S->openai.model), "gpt-live-1");
    snprintf(S->openai.voice, sizeof(S->openai.voice), "marin");
    S->timing.mic_holdoff_s = 0.0f;
    S->timing.first_turn_s = 5.0f;
    S->timing.follow_up_s = 3.0f;
    S->timing.max_speech_s = 10.0f;
    S->timing.response_timeout_s = 5.0f;
    S->timing.answer_max_s = 20.0f;
    S->timing.session_idle_s = 30.0f;
    S->timing.ready_check_s = 1.0f;
    S->timing.mic_stop_grace_s = 0.05f;
    S->sound.mic_gain_db = 0.0f;
    S->sound.low_cut_hz = 0.0f;
    S->sound.loudness_gain_db = 0.0f;
    for (size_t i = 0; i < CHUNK; i++) {
        TONE[i] = (int16_t)(sin(2.0 * PI * 440.0 * (double)i / 16000.0) * 8000.0);
    }
    LOG = pt_log_new(400, 200);
    fp_target_t target = {t_opened, t_failed, t_message, t_closed, NULL};
    fp_init(&F, &now, &target);
    C = pt_convo_new(&F.port, LOG);
    TEST_ASSERT_NOT_NULL(C);
    F.target.t = C;
    apply();
}

void tearDown(void)
{
    pt_convo_free(C);
    C = NULL;
    fp_free(&F);
    pt_log_free(LOG);
    free(S);
}

static void advance(uint32_t ms)
{
    uint32_t target = now + ms;
    for (int guard = 0; guard < 100000; guard++) {
        uint32_t d = pt_convo_next_deadline(C);
        if (d == PT_DEADLINE_NONE || !pt_time_due(d, target)) {
            break;
        }
        if (pt_time_before(now, d)) {
            now = d;
        }
        pt_convo_tick(C, now);
        fp_pump(&F);
    }
    now = target;
}

static void allow(bool on)
{
    pt_convo_warm(C, on, now);
    fp_pump(&F);
    advance(0);
}

static void warm(void)
{
    allow(true);
}

static void start(void)
{
    pt_convo_start(C, now);
    fp_pump(&F);
}

static void stop(void)
{
    pt_convo_stop(C, now);
    fp_pump(&F);
}

static void mic(void)
{
    pt_convo_mic(C, TONE, CHUNK, now);
    fp_pump(&F);
}

static void push(const char *json)
{
    fp_push(&F, json);
}

static void transcript(const char *type, const char *text)
{
    size_t len = strlen(text) + 128;
    char *json = malloc(len);
    TEST_ASSERT_NOT_NULL(json);
    snprintf(json, len, "{\"type\":\"%s\",\"delta\":\"%s\",\"start_ms\":0,\"end_ms\":200}", type, text);
    push(json);
    free(json);
}

static void ask(const char *text)
{
    transcript("session.input_transcript.delta", text);
    advance(800);
}

static void said(const char *text)
{
    transcript("session.output_transcript.delta", text);
}

static void live(int16_t level, size_t n)
{
    int16_t *pcm = malloc(n * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(pcm);
    for (size_t i = 0; i < n; i++) {
        pcm[i] = (i % 2 == 0) ? level : (int16_t)-level;
    }
    char *json = fp_json_live_delta(pcm, n);
    push(json);
    free(json);
    free(pcm);
}

static void audio(int16_t level, uint32_t ms)
{
    for (uint32_t at = 0; at < ms; at += 100) {
        live(level, STEP);
        advance(100);
    }
}

static void reply(uint32_t ms)
{
    audio(3000, ms);
    audio(0, 600);
}

static void check_devices(const pt_dev_event_t *expected, size_t n, int line)
{
    pt_dev_event_t got[256];
    size_t count = fp_devices(&F, got, 256);
    char msg[512];
    int used = snprintf(msg, sizeof(msg), "line %d, got:", line);
    for (size_t i = 0; i < count && i < 256 && used < (int)sizeof(msg) - 4; i++) {
        used += snprintf(msg + used, sizeof(msg) - (size_t)used, " %d", (int)got[i]);
    }
    UNITY_TEST_ASSERT_EQUAL_UINT((UNITY_UINT)n, (UNITY_UINT)count, line, msg);
    for (size_t i = 0; i < n; i++) {
        UNITY_TEST_ASSERT_EQUAL_INT((int)expected[i], (int)got[i], line, msg);
    }
}

#define EXPECT_DEVICES(...)                                                                                              \
    do {                                                                                                                 \
        pt_dev_event_t expected_[] = {__VA_ARGS__};                                                                      \
        check_devices(expected_, sizeof(expected_) / sizeof(expected_[0]), __LINE__);                                   \
    } while (0)

static size_t log_count(const char *text)
{
    char *json = pt_log_json(LOG, 0);
    size_t n = 0;
    for (const char *p = strstr(json, text); p != NULL; p = strstr(p + 1, text)) {
        n++;
    }
    free(json);
    return n;
}

static bool log_mentions(const char *text)
{
    return log_count(text) > 0;
}

static size_t sends_on(int conn)
{
    return fp_count_conn(&F, FP_RT_SEND, conn);
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

static uint32_t open_time(size_t nth)
{
    const fp_call_t *c = fp_nth(&F, FP_RT_OPEN, nth);
    TEST_ASSERT_NOT_NULL(c);
    return c->at;
}

static void closed_socket(void)
{
    pt_convo_rt_closed(C, now);
    fp_pump(&F);
}

void test_warm_is_off_until_allowed(void)
{
    advance(60000);
    TEST_ASSERT_EQUAL_INT(0, F.opened);
}

void test_warm_connection_opens_without_a_session(void)
{
    warm();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_UINT32(1000, open_time(0));
    TEST_ASSERT_TRUE(fp_live(&F, 1));
    TEST_ASSERT_EQUAL_STRING("gpt-live-1", fp_last(&F, FP_RT_OPEN)->model);
    TEST_ASSERT_EQUAL_size_t(1, log_count("live connection ready (warm)"));
    for (int i = 0; i < 10; i++) {
        mic();
        advance(100);
    }
    advance(300000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, sends_on(1));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_DEVICE));
    TEST_ASSERT_EQUAL_size_t(1, log_count("live connection ready (warm)"));
}

void test_conversation_starts_on_the_warm_connection(void)
{
    warm();
    start();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
    TEST_ASSERT_EQUAL_STRING("marin", fp_last(&F, FP_RT_SEND)->voice);
    TEST_ASSERT_TRUE(log_mentions("live session ready"));
    for (int i = 0; i < 4; i++) {
        mic();
    }
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.input_audio.append", 1));
    ask("Яка столиця Франції?");
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(fp_last(&F, FP_DEVICE)->follow_up);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_no_new_warm_connection_while_the_conversation_runs(void)
{
    warm();
    start();
    ask("Яка столиця Франції?");
    reply(300);
    advance(2000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    start();
    advance(2900);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(100);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, EN);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    advance(1999);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(1);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, sends_on(2));
}

void test_warm_again_two_seconds_after_the_conversation(void)
{
    warm();
    start();
    ask("Яка столиця Франції?");
    reply(300);
    stop();
    uint32_t ended = now;
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    advance(1999);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(1);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_UINT32(ended + 2000, open_time(1));
    TEST_ASSERT_EQUAL_size_t(0, sends_on(2));
    TEST_ASSERT_EQUAL_size_t(2, log_count("live connection ready (warm)"));
    start();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 2));
}

void test_start_while_the_warm_connection_is_connecting_uses_it(void)
{
    F.conn[1].manual = true;
    warm();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    start();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, sends_on(1));
    EXPECT_DEVICES(L);
    fp_connect(&F);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
    TEST_ASSERT_FALSE(log_mentions("live connection ready (warm)"));
    ask("Привіт");
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_stop_while_the_adopted_warm_connection_is_connecting(void)
{
    F.conn[1].manual = true;
    warm();
    start();
    stop();
    EXPECT_DEVICES(L, EN);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_size_t(0, sends_on(1));
    advance(1999);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(1);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 2));
    TEST_ASSERT_EQUAL_size_t(0, sends_on(2));
}

void test_warm_start_carries_the_history(void)
{
    warm();
    start();
    ask("Яка столиця Франції?");
    said("Париж.");
    reply(300);
    stop();
    advance(2000);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    start();
    cJSON *root = cJSON_Parse(sent_json("session.start", 2, 0));
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(input));
    char *text = cJSON_PrintUnformatted(input);
    TEST_ASSERT_NOT_NULL(strstr(text, "Яка столиця Франції?"));
    TEST_ASSERT_NOT_NULL(strstr(text, "Париж."));
    free(text);
    cJSON_Delete(root);
}

void test_warm_failures_back_off(void)
{
    for (int i = 1; i <= 6; i++) {
        F.conn[i].fail_connect = true;
    }
    warm();
    advance(221999);
    TEST_ASSERT_EQUAL_INT(6, F.opened);
    const uint32_t at[] = {1000, 3000, 13000, 43000, 103000, 163000};
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(at[i], open_time(i));
    }
    TEST_ASSERT_EQUAL_size_t(1, log_count("warm live connection failed"));
    TEST_ASSERT_FALSE(log_mentions("live unavailable"));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_DEVICE));
    advance(1);
    TEST_ASSERT_EQUAL_INT(7, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 7));
    TEST_ASSERT_EQUAL_size_t(1, log_count("live connection ready (warm)"));
    advance(30000);
    closed_socket();
    advance(2000);
    TEST_ASSERT_EQUAL_INT(8, F.opened);
}

void test_warm_connection_closed_while_idle_reconnects(void)
{
    warm();
    advance(60000);
    closed_socket();
    TEST_ASSERT_TRUE(log_mentions("warm live connection closed after 60 s"));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_DEVICE));
    advance(1999);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(1);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 2));
    advance(30000);
    closed_socket();
    advance(2000);
    TEST_ASSERT_EQUAL_INT(3, F.opened);
}

void test_warm_connection_dropped_soon_after_opening_keeps_backing_off(void)
{
    warm();
    const uint32_t gaps[] = {2000, 10000, 30000, 60000, 60000};
    for (int i = 0; i < 5; i++) {
        advance(1000);
        closed_socket();
        advance(gaps[i] - 1);
        TEST_ASSERT_EQUAL_INT(i + 1, F.opened);
        advance(1);
        TEST_ASSERT_EQUAL_INT(i + 2, F.opened);
        TEST_ASSERT_TRUE(fp_live(&F, i + 2));
    }
    TEST_ASSERT_EQUAL_size_t(6, log_count("live connection ready (warm)"));
    advance(30000);
    closed_socket();
    advance(2000);
    TEST_ASSERT_EQUAL_INT(7, F.opened);
}

void test_warm_connect_timeout_counts_as_a_failure(void)
{
    F.conn[1].manual = true;
    warm();
    advance(9999);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(1);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_TRUE(log_mentions("warm live connection failed"));
    advance(2000);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 2));
}

void test_messages_on_the_warm_connection_are_ignored(void)
{
    warm();
    push("{\"type\":\"info\",\"code\":\"data_channel_permissions\",\"message\":\"x\"}");
    push("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":\"x\",\"message\":\"y\"}}");
    TEST_ASSERT_TRUE(fp_live(&F, 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_DEVICE));
    start();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
}

void test_settings_for_the_connection_warm_again(void)
{
    warm();
    S->timing.follow_up_s = 4.0f;
    S->sound.volume = 0.3f;
    snprintf(S->openai.voice, sizeof(S->openai.voice), "cedar");
    apply();
    advance(1000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 1));
    use_model("gpt-live-2");
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    advance(0);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_STRING("gpt-live-2", fp_last(&F, FP_RT_OPEN)->model);
    snprintf(S->openai.api_key, sizeof(S->openai.api_key), "sk-other");
    apply();
    TEST_ASSERT_FALSE(fp_live(&F, 2));
    advance(0);
    TEST_ASSERT_EQUAL_INT(3, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 3));
    start();
    TEST_ASSERT_EQUAL_STRING("cedar", fp_last(&F, FP_RT_SEND)->voice);
}

void test_realtime_model_closes_and_stops_warming(void)
{
    warm();
    use_model("gpt-realtime-2.1-mini");
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    advance(120000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    start();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 2));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.start", 0));
    stop();
    advance(120000);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    use_model("gpt-live-1");
    advance(120000);
    TEST_ASSERT_EQUAL_INT(3, F.opened);
    TEST_ASSERT_EQUAL_STRING("gpt-live-1", fp_last(&F, FP_RT_OPEN)->model);
    TEST_ASSERT_EQUAL_size_t(0, sends_on(3));
}

void test_not_allowed_closes_and_stops_warming(void)
{
    warm();
    allow(false);
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    advance(300000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    allow(true);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 2));
}

void test_allowed_again_skips_the_backoff(void)
{
    for (int i = 1; i <= 4; i++) {
        F.conn[i].fail_connect = true;
    }
    warm();
    advance(50000);
    TEST_ASSERT_EQUAL_INT(4, F.opened);
    allow(false);
    allow(true);
    TEST_ASSERT_EQUAL_INT(5, F.opened);
    TEST_ASSERT_TRUE(fp_live(&F, 5));
}

void test_not_allowed_after_the_conversation_does_not_warm(void)
{
    warm();
    start();
    ask("Привіт");
    reply(300);
    allow(false);
    stop();
    advance(60000);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_no_warm_without_a_key(void)
{
    S->openai.api_key[0] = '\0';
    apply();
    warm();
    advance(60000);
    TEST_ASSERT_EQUAL_INT(0, F.opened);
    snprintf(S->openai.api_key, sizeof(S->openai.api_key), "sk-test");
    apply();
    advance(0);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_warm_connection_lost_at_start_opens_a_new_one(void)
{
    warm();
    pt_convo_start(C, now);
    pt_convo_rt_closed(C, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 2));
    TEST_ASSERT_TRUE(log_mentions("opening a new one"));
    EXPECT_DEVICES(L);
    ask("Привіт");
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
}

void test_warm_connection_refused_at_start_fails_the_turn(void)
{
    warm();
    F.conn[1].live_reject = true;
    start();
    EXPECT_DEVICES(L, FA);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(2000);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_warm_is_off_until_allowed);
    RUN_TEST(test_warm_connection_opens_without_a_session);
    RUN_TEST(test_conversation_starts_on_the_warm_connection);
    RUN_TEST(test_no_new_warm_connection_while_the_conversation_runs);
    RUN_TEST(test_warm_again_two_seconds_after_the_conversation);
    RUN_TEST(test_start_while_the_warm_connection_is_connecting_uses_it);
    RUN_TEST(test_stop_while_the_adopted_warm_connection_is_connecting);
    RUN_TEST(test_warm_start_carries_the_history);
    RUN_TEST(test_warm_failures_back_off);
    RUN_TEST(test_warm_connection_closed_while_idle_reconnects);
    RUN_TEST(test_warm_connection_dropped_soon_after_opening_keeps_backing_off);
    RUN_TEST(test_warm_connect_timeout_counts_as_a_failure);
    RUN_TEST(test_messages_on_the_warm_connection_are_ignored);
    RUN_TEST(test_settings_for_the_connection_warm_again);
    RUN_TEST(test_realtime_model_closes_and_stops_warming);
    RUN_TEST(test_not_allowed_closes_and_stops_warming);
    RUN_TEST(test_allowed_again_skips_the_backoff);
    RUN_TEST(test_not_allowed_after_the_conversation_does_not_warm);
    RUN_TEST(test_no_warm_without_a_key);
    RUN_TEST(test_warm_connection_lost_at_start_opens_a_new_one);
    RUN_TEST(test_warm_connection_refused_at_start_fails_the_turn);
    return UNITY_END();
}
