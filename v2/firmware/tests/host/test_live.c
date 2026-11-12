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

static void heard(const char *text)
{
    transcript("session.input_transcript.delta", text);
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

static void ask(const char *text)
{
    heard(text);
    advance(800);
}

static void reply(uint32_t ms)
{
    audio(3000, ms);
    audio(0, 600);
}

static void tool(const char *id, const char *name, const char *args)
{
    char json[512];
    snprintf(json, sizeof(json),
             "{\"type\":\"response.event\",\"event\":{\"type\":\"response.output_item.done\",\"item\":{\"type\":"
             "\"function_call\",\"call_id\":\"%s\",\"name\":\"%s\",\"arguments\":\"%s\"}}}",
             id, name, args);
    push(json);
}

static void closed_event(const char *reason)
{
    char json[128];
    snprintf(json, sizeof(json), "{\"type\":\"session.closed\",\"reason\":\"%s\"}", reason);
    push(json);
}

static void check_devices(size_t from, const pt_dev_event_t *expected, size_t n, int line)
{
    pt_dev_event_t got[256];
    size_t count = fp_devices(&F, got, 256);
    char msg[512];
    int used = snprintf(msg, sizeof(msg), "line %d, got:", line);
    for (size_t i = 0; i < count && i < 256 && used < (int)sizeof(msg) - 4; i++) {
        used += snprintf(msg + used, sizeof(msg) - (size_t)used, " %d", (int)got[i]);
    }
    UNITY_TEST_ASSERT_EQUAL_UINT((UNITY_UINT)(from + n), (UNITY_UINT)count, line, msg);
    for (size_t i = 0; i < n; i++) {
        UNITY_TEST_ASSERT_EQUAL_INT((int)expected[i], (int)got[from + i], line, msg);
    }
}

#define EXPECT_DEVICES(...)                                                                                              \
    do {                                                                                                                 \
        pt_dev_event_t expected_[] = {__VA_ARGS__};                                                                      \
        check_devices(0, expected_, sizeof(expected_) / sizeof(expected_[0]), __LINE__);                                 \
    } while (0)

static const fp_call_t *last_device(void)
{
    return fp_last(&F, FP_DEVICE);
}

static bool log_mentions(const char *text)
{
    char *json = pt_log_json(LOG, 0);
    bool found = strstr(json, text) != NULL;
    free(json);
    return found;
}

static size_t index_of_device(pt_dev_event_t ev, size_t nth)
{
    size_t seen = 0;
    for (size_t i = 0; i < F.n; i++) {
        if (F.calls[i].kind == FP_DEVICE && F.calls[i].ev == ev && seen++ == nth) {
            return i;
        }
    }
    return SIZE_MAX;
}

static bool all_zero(const fp_call_t *c)
{
    for (size_t k = 0; k < c->n; k++) {
        if (c->pcm[k] != 0) {
            return false;
        }
    }
    return true;
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

static void answered_first_turn(void)
{
    start();
    mic();
    ask("Яка столиця Франції?");
    reply(300);
}

void test_live_answer_flow(void)
{
    start();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 1));
    mic();
    mic();
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.input_audio.append", 1));
    mic();
    mic();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.input_audio.append", 1));
    TEST_ASSERT_EQUAL_size_t(2400, fp_appended_len(&F, 1));
    heard("Яка");
    EXPECT_DEVICES(L, SS);
    advance(799);
    EXPECT_DEVICES(L, SS);
    advance(1);
    EXPECT_DEVICES(L, SS, SP);
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played(&F, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_TRUE(log_mentions("turn ended: answered"));
    TEST_ASSERT_TRUE(log_mentions("first audio 0.00 s after speech end"));
    TEST_ASSERT_TRUE(fp_live(&F, 1));
}

void test_live_no_mic_while_answering(void)
{
    start();
    mic();
    ask("Котра година?");
    for (int i = 0; i < 3; i++) {
        live(3000, STEP);
        mic();
        advance(100);
    }
    for (int i = 0; i < 6; i++) {
        live(0, STEP);
        mic();
        advance(100);
    }
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    size_t from = index_of_device(AS, 0);
    size_t to = index_of_device(AN, 0);
    size_t loud_before = 0;
    size_t during = 0;
    for (size_t i = 0; i < F.n; i++) {
        const fp_call_t *c = &F.calls[i];
        if (c->kind != FP_RT_SEND || strcmp(c->type, "session.input_audio.append") != 0) {
            continue;
        }
        if (i < from && !all_zero(c)) {
            loud_before++;
        }
        if (i > from && i < to) {
            TEST_ASSERT_TRUE(all_zero(c));
            during++;
        }
    }
    TEST_ASSERT_TRUE(loud_before > 0);
    TEST_ASSERT_TRUE(during > 0);
}

void test_live_keepalive_zeros(void)
{
    answered_first_turn();
    size_t mark = F.n;
    advance(1000);
    size_t zeros = 0;
    for (size_t i = mark; i < F.n; i++) {
        const fp_call_t *c = &F.calls[i];
        if (c->kind == FP_RT_SEND && strcmp(c->type, "session.input_audio.append") == 0) {
            TEST_ASSERT_EQUAL_size_t(2400, c->n);
            TEST_ASSERT_TRUE(all_zero(c));
            zeros++;
        }
    }
    TEST_ASSERT_TRUE(zeros >= 9 && zeros <= 11);
}

void test_live_no_keepalive_while_listening(void)
{
    start();
    for (int i = 0; i < 10; i++) {
        mic();
        advance(100);
    }
    heard("Привіт");
    for (int i = 0; i < 5; i++) {
        mic();
        advance(100);
    }
    size_t appends = 0;
    for (size_t i = 0; i < F.n; i++) {
        const fp_call_t *c = &F.calls[i];
        if (c->kind == FP_RT_SEND && strcmp(c->type, "session.input_audio.append") == 0) {
            TEST_ASSERT_FALSE(all_zero(c));
            appends++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(4, appends);
}

void test_live_prebuffer(void)
{
    start();
    ask("Привіт");
    live(3000, 4800);
    EXPECT_DEVICES(L, SS, SP, AS);
    TEST_ASSERT_EQUAL_size_t(0, fp_played(&F, NULL, 0));
    live(3000, 1200);
    TEST_ASSERT_EQUAL_size_t(6000, fp_played(&F, NULL, 0));
    live(3000, 2400);
    TEST_ASSERT_EQUAL_size_t(8400, fp_played(&F, NULL, 0));
}

void test_live_short_pause_inside_answer_is_played(void)
{
    start();
    ask("Привіт");
    live(3000, 6000);
    live(0, 4800);
    live(3000, 2400);
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_size_t(6000 + 4800 + 2400 + 6000, fp_played(&F, NULL, 0));
}

void test_live_long_pause_plays_a_quarter_second_then_replays_the_rest(void)
{
    start();
    ask("Привіт");
    audio(3000, 300);
    audio(0, 300);
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played(&F, NULL, 0));
    audio(3000, 300);
    TEST_ASSERT_EQUAL_size_t(7200 + 7200 + 7200, fp_played(&F, NULL, 0));
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_size_t(7200 + 7200 + 7200 + 6000, fp_played(&F, NULL, 0));
}

void test_live_turn_adopts_a_prebuffering_segment(void)
{
    answered_first_turn();
    live(3000, 2400);
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played(&F, NULL, 0));
    start();
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SP, AS);
    audio(3000, 300);
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_size_t(2400 + 7200 + 6000, fp_played_before_end(&F, 1));
    advance(3000);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SP, AS, AN);
}

void test_live_two_part_answer(void)
{
    answered_first_turn();
    start();
    audio(3000, 300);
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SP, AS, AN);
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played_before_end(&F, 1));
    TEST_ASSERT_EQUAL_size_t(2, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_TRUE(last_device()->follow_up);
}

void test_live_answer_between_turns_is_played(void)
{
    answered_first_turn();
    audio(3000, 300);
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_size_t(2 * (7200 + 6000), fp_played(&F, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(2, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
}

void test_live_follow_up_no_speech_ends_and_closes(void)
{
    answered_first_turn();
    start();
    advance(2900);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L);
    advance(100);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, EN);
    TEST_ASSERT_TRUE(log_mentions("turn ended: no speech"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
}

void test_live_follow_up_limit_ends(void)
{
    S->timing.max_follow_ups = 1;
    apply();
    answered_first_turn();
    TEST_ASSERT_TRUE(last_device()->follow_up);
    start();
    ask("А Німеччини?");
    reply(300);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: answered, follow-up limit reached"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
}

void test_live_set_volume_tool(void)
{
    start();
    ask("Тихіше");
    tool("c1", "set_volume", "{\\\"action\\\":\\\"down\\\"}");
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_SET_VOLUME));
    TEST_ASSERT_TRUE(fp_last(&F, FP_SET_VOLUME)->volume < 0.7f);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.item.create", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.create", 1));
    size_t item = SIZE_MAX;
    size_t create = SIZE_MAX;
    for (size_t i = 0; i < F.n; i++) {
        if (F.calls[i].kind == FP_RT_SEND && strcmp(F.calls[i].type, "response.item.create") == 0) {
            item = i;
        }
        if (F.calls[i].kind == FP_RT_SEND && strcmp(F.calls[i].type, "response.create") == 0) {
            create = i;
        }
    }
    TEST_ASSERT_TRUE(item < create);
    TEST_ASSERT_NOT_NULL(strstr(sent_json("response.item.create", 1, 0), "volume"));
    TEST_ASSERT_NOT_NULL(strstr(sent_json("response.item.create", 1, 0), "c1"));
    TEST_ASSERT_TRUE(log_mentions("volume set to"));
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
}

void test_live_unknown_tool_reports_error(void)
{
    start();
    ask("Щось");
    tool("x1", "fly", "{}");
    TEST_ASSERT_NOT_NULL(strstr(sent_json("response.item.create", 1, 0), "unknown tool fly"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.create", 1));
}

void test_live_end_tool_while_speaking(void)
{
    start();
    ask("Дякую, все");
    audio(3000, 300);
    EXPECT_DEVICES(L, SS, SP, AS);
    tool("e1", "end_conversation", "{}");
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.item.create", 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.create", 1));
    TEST_ASSERT_NOT_NULL(strstr(sent_json("response.item.create", 1, 0), "ok"));
    EXPECT_DEVICES(L, SS, SP, AS);
    audio(0, 600);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: ended by model"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played(&F, NULL, 0));
}

void test_live_end_tool_silent(void)
{
    start();
    ask("Дякую, все");
    tool("e1", "end_conversation", "{}");
    EXPECT_DEVICES(L, SS, SP, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: ended by model"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.item.create", 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.create", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
}

void test_live_end_tool_in_follow_up_ends_at_once(void)
{
    answered_first_turn();
    start();
    advance(500);
    tool("e1", "end_conversation", "{}");
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: ended by model"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
}

void test_live_end_tool_between_turns(void)
{
    answered_first_turn();
    tool("e1", "end_conversation", "{}");
    EXPECT_DEVICES(L, SS, SP, AS, AN, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
}

void test_live_session_closed_mid_answer_fails(void)
{
    start();
    ask("Розкажи казку");
    audio(3000, 300);
    closed_event("content");
    EXPECT_DEVICES(L, SS, SP, AS, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_TRUE(log_mentions("live session closed: content"));
}

void test_live_socket_closed_mid_turn_fails(void)
{
    start();
    heard("Привіт");
    pt_convo_rt_closed(C, now);
    fp_pump(&F);
    EXPECT_DEVICES(L, SS, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
}

static void assert_no_answer_end(void)
{
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: no answer"));
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
}

static void delegation(void)
{
    push("{\"type\":\"session.delegation.created\",\"offset_ms\":14800,\"delegation\":{\"id\":\"item_1\",\"type\":"
         "\"delegation\",\"response_id\":\"resp_1\",\"target\":\"responses\"},\"event_id\":\"event_1\"}");
}

void test_live_silent_model_ends_with_no_answer(void)
{
    start();
    ask("Привіт");
    advance(2900);
    EXPECT_DEVICES(L, SS, SP);
    advance(100);
    EXPECT_DEVICES(L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(1000 + 800 + 3000, fp_time_of(&F, EN, 0));
    assert_no_answer_end();
}

void test_live_answer_just_before_three_seconds_plays(void)
{
    start();
    ask("Привіт");
    uint32_t stopped = now;
    advance(2999);
    EXPECT_DEVICES(L, SS, SP);
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_UINT32(stopped + 2999, fp_time_of(&F, AS, 0));
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_TRUE(fp_live(&F, 1));
    TEST_ASSERT_EQUAL_size_t(7200 + 6000, fp_played(&F, NULL, 0));
    TEST_ASSERT_FALSE(log_mentions("turn ended: no answer"));
    start();
    ask("А Німеччини?");
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_live_no_answer_at_exactly_three_seconds_ends(void)
{
    start();
    ask("Привіт");
    uint32_t stopped = now;
    advance(2999);
    EXPECT_DEVICES(L, SS, SP);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(stopped + 3000, fp_time_of(&F, EN, 0));
    assert_no_answer_end();
}

void test_live_silent_model_ends_at_three_seconds_even_with_a_long_timeout(void)
{
    S->timing.response_timeout_s = 30.0f;
    apply();
    start();
    ask("Дякую, все");
    advance(2999);
    EXPECT_DEVICES(L, SS, SP);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, EN);
    assert_no_answer_end();
}

void test_live_silent_model_with_a_short_timeout_ends_sooner(void)
{
    S->timing.response_timeout_s = 2.0f;
    apply();
    start();
    ask("Привіт");
    advance(1999);
    EXPECT_DEVICES(L, SS, SP);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, EN);
    assert_no_answer_end();
}

void test_live_delegation_waits_at_most_eight_seconds(void)
{
    S->timing.response_timeout_s = 30.0f;
    apply();
    start();
    ask("Тихіше");
    advance(1000);
    delegation();
    advance(6900);
    EXPECT_DEVICES(L, SS, SP);
    advance(1100);
    EXPECT_DEVICES(L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(1000 + 800 + 1000 + 8000, fp_time_of(&F, EN, 0));
    assert_no_answer_end();
}

void test_live_delegation_before_speech_stopped_waits_eight_seconds(void)
{
    S->timing.response_timeout_s = 30.0f;
    apply();
    start();
    heard("Тихіше");
    delegation();
    advance(800);
    EXPECT_DEVICES(L, SS, SP);
    advance(7900);
    EXPECT_DEVICES(L, SS, SP);
    advance(100);
    EXPECT_DEVICES(L, SS, SP, EN);
    assert_no_answer_end();
}

void test_live_delegation_then_tool_then_answer(void)
{
    S->timing.response_timeout_s = 30.0f;
    apply();
    start();
    ask("Тихіше");
    delegation();
    advance(4000);
    EXPECT_DEVICES(L, SS, SP);
    tool("c1", "set_volume", "{\\\"action\\\":\\\"down\\\"}");
    advance(7900);
    EXPECT_DEVICES(L, SS, SP);
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: answered"));
}

void test_live_tool_call_without_delegation_event_waits_eight_seconds(void)
{
    S->timing.response_timeout_s = 30.0f;
    apply();
    start();
    ask("Тихіше");
    advance(2000);
    tool("c1", "set_volume", "{\\\"action\\\":\\\"down\\\"}");
    advance(7900);
    EXPECT_DEVICES(L, SS, SP);
    advance(100);
    EXPECT_DEVICES(L, SS, SP, EN);
}

void test_live_delegation_counts_only_for_its_turn(void)
{
    start();
    ask("Тихіше");
    delegation();
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    start();
    ask("Дякую, все");
    uint32_t stopped = now;
    advance(3000);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(stopped + 3000, fp_time_of(&F, EN, 0));
}

void test_live_delegation_between_turns_is_ignored(void)
{
    answered_first_turn();
    delegation();
    start();
    ask("Дякую, все");
    advance(3000);
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SS, SP, EN);
}

void test_live_background_speech_past_the_cap_ends(void)
{
    start();
    heard("а");
    for (int i = 0; i < 80; i++) {
        advance(200);
        heard("а");
    }
    EXPECT_DEVICES(L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(1000 + 10000 + 5000, fp_time_of(&F, EN, 0));
    assert_no_answer_end();
}

void test_live_resumed_speech_goes_back_to_speaking_under_the_cap(void)
{
    start();
    heard("Яка");
    advance(800);
    EXPECT_DEVICES(L, SS, SP);
    for (int i = 0; i < 11; i++) {
        advance(500);
        heard("ще");
    }
    EXPECT_DEVICES(L, SS, SP);
    TEST_ASSERT_TRUE(log_mentions("speech resumed"));
    for (int i = 0; i < 20; i++) {
        advance(500);
        heard("ще");
    }
    EXPECT_DEVICES(L, SS, SP, EN);
    TEST_ASSERT_EQUAL_UINT32(1000 + 10000 + 5000, fp_time_of(&F, EN, 0));
    assert_no_answer_end();
}

void test_live_resumed_speech_then_answer(void)
{
    start();
    ask("Яка");
    heard(" столиця?");
    advance(799);
    TEST_ASSERT_EQUAL_size_t(1, fp_device_count(&F, SP));
    advance(1);
    reply(300);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("first audio 0.00 s after speech end"));
}

void test_live_answer_audio_stall_ends_the_segment(void)
{
    start();
    ask("Привіт");
    audio(3000, 300);
    EXPECT_DEVICES(L, SS, SP, AS);
    advance(1899);
    EXPECT_DEVICES(L, SS, SP, AS);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_size_t(7200, fp_played(&F, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_TRUE(log_mentions("answer audio stopped for 2 s"));
    TEST_ASSERT_TRUE(log_mentions("turn ended: answered"));
}

void test_live_max_speech_counts_as_speech_stopped(void)
{
    start();
    heard("а");
    for (int i = 0; i < 50; i++) {
        advance(200);
        heard("а");
    }
    EXPECT_DEVICES(L, SS, SP);
    TEST_ASSERT_TRUE(log_mentions("speech limit reached"));
}

void test_live_answer_limit_fails(void)
{
    S->timing.answer_max_s = 1.0f;
    apply();
    start();
    ask("Розкажи казку");
    audio(3000, 1000);
    EXPECT_DEVICES(L, SS, SP, AS, FA);
    TEST_ASSERT_EQUAL_STRING("timeout", last_device()->code);
    TEST_ASSERT_TRUE(log_mentions("answer limit reached"));
}

void test_live_heard_logged_when_enabled(void)
{
    S->openai.log_heard = true;
    apply();
    start();
    heard("Яка ");
    advance(200);
    heard("столиця?");
    advance(800);
    TEST_ASSERT_TRUE(log_mentions("heard: Яка столиця?"));
}

void test_live_history_seeded_next_conversation(void)
{
    start();
    heard("Яка столиця ");
    advance(200);
    heard("Франції?");
    advance(800);
    said("Париж.");
    reply(300);
    stop();
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    start();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    cJSON *root = cJSON_Parse(sent_json("session.start", 2, 0));
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    TEST_ASSERT_TRUE(cJSON_IsArray(input));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(input));
    char *text = cJSON_PrintUnformatted(input);
    TEST_ASSERT_NOT_NULL(strstr(text, "\"role\":\"user\""));
    TEST_ASSERT_NOT_NULL(strstr(text, "Яка столиця Франції?"));
    TEST_ASSERT_NOT_NULL(strstr(text, "\"role\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(text, "Париж."));
    free(text);
    cJSON_Delete(root);
    stop();
    advance(30100);
    start();
    TEST_ASSERT_EQUAL_INT(3, F.opened);
    root = cJSON_Parse(sent_json("session.start", 3, 0));
    TEST_ASSERT_NOT_NULL(root);
    input = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    TEST_ASSERT_NULL(input);
    cJSON_Delete(root);
}

static cJSON *start_input(int conn)
{
    cJSON *root = cJSON_Parse(sent_json("session.start", conn, 0));
    TEST_ASSERT_NOT_NULL(root);
    cJSON *input = cJSON_DetachItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    cJSON_Delete(root);
    return input;
}

static const char *item_text(const cJSON *item)
{
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
    TEST_ASSERT_TRUE(cJSON_IsString(text));
    return text->valuestring;
}

static const char *item_role(const cJSON *item)
{
    return cJSON_GetObjectItemCaseSensitive(item, "role")->valuestring;
}

static size_t utf8_chars(const char *s)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0) {
        size_t more = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : 9;
        TEST_ASSERT_TRUE(more < 9);
        p++;
        for (size_t k = 0; k < more; k++, p++) {
            TEST_ASSERT_EQUAL_HEX8(0x80, *p & 0xC0);
        }
        n++;
    }
    return n;
}

static char *cyrillic(const char *prefix, size_t count)
{
    size_t len = strlen(prefix) + count * 2 + 1;
    char *out = malloc(len);
    TEST_ASSERT_NOT_NULL(out);
    strcpy(out, prefix);
    for (size_t i = 0; i < count; i++) {
        strcat(out, "ж");
    }
    return out;
}

void test_live_history_keeps_the_newest_24_messages_and_4000_characters(void)
{
    start();
    for (int i = 0; i < 13; i++) {
        if (i > 0) {
            start();
        }
        char tag[16];
        snprintf(tag, sizeof(tag), "q%02d ", i);
        char *q = cyrillic(tag, 196);
        heard(q);
        free(q);
        advance(800);
        snprintf(tag, sizeof(tag), "a%02d ", i);
        char *a = cyrillic(tag, 196);
        said(a);
        free(a);
        reply(300);
    }
    stop();
    start();
    cJSON *input = start_input(2);
    TEST_ASSERT_TRUE(cJSON_IsArray(input));
    int n = cJSON_GetArraySize(input);
    TEST_ASSERT_EQUAL_INT(20, n);
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        const cJSON *item = cJSON_GetArrayItem(input, i);
        total += utf8_chars(item_text(item));
    }
    TEST_ASSERT_TRUE(total <= 4000);
    TEST_ASSERT_EQUAL_STRING_LEN("q03 ", item_text(cJSON_GetArrayItem(input, 0)), 4);
    TEST_ASSERT_EQUAL_STRING("user", item_role(cJSON_GetArrayItem(input, 0)));
    TEST_ASSERT_EQUAL_STRING_LEN("a12 ", item_text(cJSON_GetArrayItem(input, n - 1)), 4);
    TEST_ASSERT_EQUAL_STRING("assistant", item_role(cJSON_GetArrayItem(input, n - 1)));
    cJSON_Delete(input);
}

void test_live_history_count_limit_is_24(void)
{
    start();
    for (int i = 0; i < 13; i++) {
        if (i > 0) {
            start();
        }
        char text[16];
        snprintf(text, sizeof(text), "q%02d", i);
        heard(text);
        advance(800);
        snprintf(text, sizeof(text), "a%02d", i);
        said(text);
        reply(300);
    }
    stop();
    start();
    cJSON *input = start_input(2);
    TEST_ASSERT_EQUAL_INT(24, cJSON_GetArraySize(input));
    TEST_ASSERT_EQUAL_STRING("q01", item_text(cJSON_GetArrayItem(input, 0)));
    TEST_ASSERT_EQUAL_STRING("a12", item_text(cJSON_GetArrayItem(input, 23)));
    cJSON_Delete(input);
}

void test_live_long_text_is_cut_on_a_character_boundary(void)
{
    start();
    char *q = cyrillic("a", 2100);
    heard(q);
    free(q);
    advance(800);
    stop();
    start();
    cJSON *input = start_input(2);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(input));
    const char *text = item_text(cJSON_GetArrayItem(input, 0));
    TEST_ASSERT_EQUAL_size_t(3999, strlen(text));
    TEST_ASSERT_EQUAL_size_t(2000, utf8_chars(text));
    cJSON_Delete(input);
}

void test_live_late_answer_text_stays_with_its_answer(void)
{
    start();
    ask("Яка столиця Франції?");
    live(3000, 2400);
    said("Париж");
    audio(3000, 200);
    audio(0, 600);
    said(", це Франція.");
    start();
    ask("А Німеччини?");
    said("Берлін.");
    reply(300);
    start();
    ask("А Італії?");
    said("Рим");
    audio(3000, 300);
    stop();
    start();
    cJSON *input = start_input(2);
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(input));
    TEST_ASSERT_EQUAL_STRING("Яка столиця Франції?", item_text(cJSON_GetArrayItem(input, 0)));
    TEST_ASSERT_EQUAL_STRING("Париж, це Франція.", item_text(cJSON_GetArrayItem(input, 1)));
    TEST_ASSERT_EQUAL_STRING("А Німеччини?", item_text(cJSON_GetArrayItem(input, 2)));
    TEST_ASSERT_EQUAL_STRING("Берлін.", item_text(cJSON_GetArrayItem(input, 3)));
    TEST_ASSERT_EQUAL_STRING("А Італії?", item_text(cJSON_GetArrayItem(input, 4)));
    cJSON_Delete(input);
}

void test_live_stop_button(void)
{
    start();
    heard("Привіт");
    stop();
    EXPECT_DEVICES(L, SS, EN);
    TEST_ASSERT_TRUE(log_mentions("turn ended: stopped by device"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    size_t sends = fp_count(&F, FP_RT_SEND);
    advance(1000);
    TEST_ASSERT_EQUAL_size_t(sends, fp_count(&F, FP_RT_SEND));
}

void test_live_stop_between_turns_closes(void)
{
    answered_first_turn();
    stop();
    EXPECT_DEVICES(L, SS, SP, AS, AN, EN);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
}

void test_live_overflow_cuts_and_closes(void)
{
    start();
    ask("Розкажи казку");
    audio(3000, 300);
    pt_convo_rt_overflow(C, now);
    fp_pump(&F);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(log_mentions("turn ended: answer cut"));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
}

void test_live_never_sends_realtime_messages(void)
{
    start();
    mic();
    ask("Тихіше");
    tool("c1", "set_volume", "{\\\"action\\\":\\\"down\\\"}");
    reply(300);
    start();
    mic();
    ask("Дякую, все");
    tool("e1", "end_conversation", "{}");
    EXPECT_DEVICES(L, SS, SP, AS, AN, L, SS, SP, AN);
    start();
    stop();
    const char *banned[] = {"session.update",           "input_audio_buffer.append", "input_audio_buffer.clear",
                            "input_audio_buffer.commit", "response.cancel",           "conversation.item.create"};
    for (size_t i = 0; i < sizeof(banned) / sizeof(banned[0]); i++) {
        TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, banned[i], 0));
    }
}

void test_switch_engines_between_conversations(void)
{
    use_model("gpt-realtime-2.1-mini");
    start();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 1));
    push("{\"type\":\"input_audio_buffer.speech_started\"}");
    push("{\"type\":\"input_audio_buffer.speech_stopped\"}");
    advance(100);
    stop();
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.start", 0));
    use_model("gpt-live-1");
    start();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.start", 2));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.update", 2));
    for (int i = 0; i < 4; i++) {
        mic();
    }
    TEST_ASSERT_TRUE(fp_sends(&F, "session.input_audio.append", 2) > 0);
    stop();
    use_model("gpt-realtime-2.1-mini");
    start();
    TEST_ASSERT_EQUAL_INT(3, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 3));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.start", 3));
    mic();
    TEST_ASSERT_TRUE(fp_sends(&F, "input_audio_buffer.append", 3) > 0);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.input_audio.append", 3));
}

void test_switch_engines_mid_conversation_starts_the_new_engine(void)
{
    answered_first_turn();
    use_model("gpt-realtime-2.1-mini");
    start();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.close", 1));
    TEST_ASSERT_FALSE(fp_live(&F, 1));
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 2));
    mic();
    TEST_ASSERT_TRUE(fp_sends(&F, "input_audio_buffer.append", 2) > 0);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "session.input_audio.append", 2));
}

void test_live_settings_change_mid_conversation_keeps_history(void)
{
    answered_first_turn();
    S->timing.follow_up_s = 4.0f;
    apply();
    start();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    cJSON *root = cJSON_Parse(sent_json("session.start", 2, 0));
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "session"), "input");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(input));
    cJSON_Delete(root);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_live_answer_flow);
    RUN_TEST(test_live_no_mic_while_answering);
    RUN_TEST(test_live_keepalive_zeros);
    RUN_TEST(test_live_no_keepalive_while_listening);
    RUN_TEST(test_live_prebuffer);
    RUN_TEST(test_live_short_pause_inside_answer_is_played);
    RUN_TEST(test_live_long_pause_plays_a_quarter_second_then_replays_the_rest);
    RUN_TEST(test_live_turn_adopts_a_prebuffering_segment);
    RUN_TEST(test_live_two_part_answer);
    RUN_TEST(test_live_answer_between_turns_is_played);
    RUN_TEST(test_live_follow_up_no_speech_ends_and_closes);
    RUN_TEST(test_live_follow_up_limit_ends);
    RUN_TEST(test_live_set_volume_tool);
    RUN_TEST(test_live_unknown_tool_reports_error);
    RUN_TEST(test_live_end_tool_while_speaking);
    RUN_TEST(test_live_end_tool_silent);
    RUN_TEST(test_live_end_tool_in_follow_up_ends_at_once);
    RUN_TEST(test_live_end_tool_between_turns);
    RUN_TEST(test_live_session_closed_mid_answer_fails);
    RUN_TEST(test_live_socket_closed_mid_turn_fails);
    RUN_TEST(test_live_silent_model_ends_with_no_answer);
    RUN_TEST(test_live_answer_just_before_three_seconds_plays);
    RUN_TEST(test_live_no_answer_at_exactly_three_seconds_ends);
    RUN_TEST(test_live_silent_model_ends_at_three_seconds_even_with_a_long_timeout);
    RUN_TEST(test_live_silent_model_with_a_short_timeout_ends_sooner);
    RUN_TEST(test_live_delegation_waits_at_most_eight_seconds);
    RUN_TEST(test_live_delegation_before_speech_stopped_waits_eight_seconds);
    RUN_TEST(test_live_delegation_then_tool_then_answer);
    RUN_TEST(test_live_tool_call_without_delegation_event_waits_eight_seconds);
    RUN_TEST(test_live_delegation_counts_only_for_its_turn);
    RUN_TEST(test_live_delegation_between_turns_is_ignored);
    RUN_TEST(test_live_background_speech_past_the_cap_ends);
    RUN_TEST(test_live_resumed_speech_goes_back_to_speaking_under_the_cap);
    RUN_TEST(test_live_resumed_speech_then_answer);
    RUN_TEST(test_live_answer_audio_stall_ends_the_segment);
    RUN_TEST(test_live_max_speech_counts_as_speech_stopped);
    RUN_TEST(test_live_answer_limit_fails);
    RUN_TEST(test_live_heard_logged_when_enabled);
    RUN_TEST(test_live_history_seeded_next_conversation);
    RUN_TEST(test_live_history_keeps_the_newest_24_messages_and_4000_characters);
    RUN_TEST(test_live_history_count_limit_is_24);
    RUN_TEST(test_live_long_text_is_cut_on_a_character_boundary);
    RUN_TEST(test_live_late_answer_text_stays_with_its_answer);
    RUN_TEST(test_live_stop_button);
    RUN_TEST(test_live_stop_between_turns_closes);
    RUN_TEST(test_live_overflow_cuts_and_closes);
    RUN_TEST(test_live_never_sends_realtime_messages);
    RUN_TEST(test_switch_engines_between_conversations);
    RUN_TEST(test_switch_engines_mid_conversation_starts_the_new_engine);
    RUN_TEST(test_live_settings_change_mid_conversation_keeps_history);
    return UNITY_END();
}
