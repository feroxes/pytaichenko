#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fake_port.h"
#include "pt_appstate.h"
#include "pt_dsp.h"
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
#define HAPPY L, SS, SP, AS, AN
#define CHUNK 512
#define PI 3.14159265358979323846

static uint32_t base = 1000;
static uint32_t now;
static fake_port_t F;
static pt_convo_t *C;
static pt_log_t *LOG;
static pt_settings_t *S;
static int16_t ZERO[24000];

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

static void new_convo(void)
{
    C = pt_convo_new(&F.port, LOG);
    TEST_ASSERT_NOT_NULL(C);
    F.target.t = C;
}

static void apply(void)
{
    pt_convo_settings(C, S, now);
}

void setUp(void)
{
    now = base;
    S = malloc(sizeof(*S));
    TEST_ASSERT_NOT_NULL(S);
    pt_settings_defaults(S, "uk");
    snprintf(S->openai.api_key, sizeof(S->openai.api_key), "sk-test");
    S->timing.mic_holdoff_s = 0.0f;
    S->timing.first_turn_s = 1.0f;
    S->timing.follow_up_s = 0.3f;
    S->timing.max_speech_s = 2.0f;
    S->timing.response_timeout_s = 1.0f;
    S->timing.session_idle_s = 0.5f;
    S->timing.mic_stop_grace_s = 0.05f;
    S->sound.mic_gain_db = 0.0f;
    LOG = pt_log_new(400, 200);
    fp_target_t target = {t_opened, t_failed, t_message, t_closed, NULL};
    fp_init(&F, &now, &target);
    new_convo();
    apply();
}

void tearDown(void)
{
    pt_convo_free(C);
    C = NULL;
    fp_free(&F);
    pt_log_free(LOG);
    free(S);
    base = 1000;
}

static void advance(uint32_t ms)
{
    uint32_t target = now + ms;
    for (int guard = 0; guard < 10000; guard++) {
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

static void begin(void)
{
    size_t clears = fp_sends(&F, "input_audio_buffer.clear", 0);
    start();
    TEST_ASSERT_TRUE(fp_sends(&F, "input_audio_buffer.clear", 0) > clears);
}

static void stop(void)
{
    pt_convo_stop(C, now);
    fp_pump(&F);
}

static void closed(void)
{
    pt_convo_rt_closed(C, now);
    fp_pump(&F);
}

static void mic(const int16_t *pcm, size_t n)
{
    pt_convo_mic(C, pcm, n, now);
    fp_pump(&F);
}

static void push(const char *json)
{
    fp_push(&F, json);
}

static void speech_started(void)
{
    push("{\"type\":\"input_audio_buffer.speech_started\"}");
}

static void speech(void)
{
    speech_started();
    push("{\"type\":\"input_audio_buffer.speech_stopped\"}");
}

static void created(const char *rid)
{
    char json[128];
    snprintf(json, sizeof(json), "{\"type\":\"response.created\",\"response\":{\"id\":\"%s\"}}", rid);
    push(json);
}

static void delta(const char *rid, const int16_t *pcm, size_t n)
{
    char *json = fp_json_delta(rid, pcm, n);
    push(json);
    free(json);
}

static void transcript(const char *rid, const char *text)
{
    char json[256];
    snprintf(json, sizeof(json), "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"%s\",\"transcript\":\"%s\"}",
             rid, text);
    push(json);
}

static void done_with(const char *rid, const char *status, const char *error, const char *calls)
{
    char json[1024];
    char details[160] = "";
    if (error != NULL) {
        snprintf(details, sizeof(details), ",\"status_details\":{\"error\":{\"code\":\"%s\"}}", error);
    }
    snprintf(json, sizeof(json), "{\"type\":\"response.done\",\"response\":{\"id\":\"%s\",\"status\":\"%s\"%s,\"output\":[%s]}}",
             rid, status, details, calls != NULL ? calls : "");
    push(json);
}

static void done(const char *rid)
{
    done_with(rid, "completed", NULL, NULL);
}

static void answer(const char *rid, const char *text)
{
    created(rid);
    delta(rid, ZERO, 2400);
    transcript(rid, text);
    done(rid);
}

#define CALL(id, name, args) "{\"type\":\"function_call\",\"call_id\":\"" id "\",\"name\":\"" name "\",\"arguments\":" args "}"
#define END_CALL CALL("e1", "end_conversation", "\"{}\"")
#define DOWN_CALL CALL("c1", "set_volume", "\"{\\\"action\\\":\\\"down\\\"}\"")

static void answered_turn(const char *rid)
{
    begin();
    speech();
    answer(rid, "Так.");
    advance(50);
}

static size_t devices(pt_dev_event_t *out, size_t cap)
{
    return fp_devices(&F, out, cap);
}

static void check_devices(size_t from, const pt_dev_event_t *expected, size_t n, int line)
{
    pt_dev_event_t got[256];
    size_t count = devices(got, 256);
    char msg[512];
    int used = snprintf(msg, sizeof(msg), "line %d, got:", line);
    for (size_t i = 0; i < count && used < (int)sizeof(msg) - 4; i++) {
        used += snprintf(msg + used, sizeof(msg) - (size_t)used, " %d", (int)got[i]);
    }
    UNITY_TEST_ASSERT_EQUAL_UINT((UNITY_UINT)(from + n), (UNITY_UINT)count, line, msg);
    for (size_t i = 0; i < n; i++) {
        UNITY_TEST_ASSERT_EQUAL_INT((int)expected[i], (int)got[from + i], line, msg);
    }
}

#define EXPECT_DEVICES_FROM(from, ...)                                                                                   \
    do {                                                                                                                 \
        pt_dev_event_t expected_[] = {__VA_ARGS__};                                                                      \
        check_devices((from), expected_, sizeof(expected_) / sizeof(expected_[0]), __LINE__);                            \
    } while (0)
#define EXPECT_DEVICES(...) EXPECT_DEVICES_FROM(0, __VA_ARGS__)

static size_t device_total(void)
{
    return fp_count(&F, FP_DEVICE);
}

static size_t endings(void)
{
    return fp_device_count(&F, AN) + fp_device_count(&F, EN) + fp_device_count(&F, FA);
}

static const fp_call_t *last_device(void)
{
    return fp_last(&F, FP_DEVICE);
}

static void calls_of(int conn, char *out, size_t len)
{
    fp_calls(&F, conn, out, len);
}

#define EXPECT_CALLS(conn, text)                                                                                         \
    do {                                                                                                                 \
        char calls_[256];                                                                                                \
        calls_of((conn), calls_, sizeof(calls_));                                                                        \
        TEST_ASSERT_EQUAL_STRING((text), calls_);                                                                        \
    } while (0)

static int log_count_prefix(const char *prefix, char *first, size_t first_len)
{
    char *json = pt_log_json(LOG, 0);
    cJSON *root = cJSON_Parse(json);
    free(json);
    int count = 0;
    const cJSON *line;
    cJSON_ArrayForEach(line, cJSON_GetObjectItemCaseSensitive(root, "lines"))
    {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(line, "text");
        if (cJSON_IsString(t) && strncmp(t->valuestring, prefix, strlen(prefix)) == 0) {
            if (count == 0 && first != NULL) {
                snprintf(first, first_len, "%s", t->valuestring);
            }
            count++;
        }
    }
    cJSON_Delete(root);
    return count;
}

static bool log_has(const char *text)
{
    char line[256];
    return log_count_prefix(text, line, sizeof(line)) > 0 && strcmp(line, text) == 0;
}

static bool log_mentions(const char *text)
{
    char *json = pt_log_json(LOG, 0);
    bool found = strstr(json, text) != NULL;
    free(json);
    return found;
}

static int max_abs(const int16_t *x, size_t n)
{
    int m = 0;
    for (size_t i = 0; i < n; i++) {
        int v = abs((int)x[i]);
        if (v > m) {
            m = v;
        }
    }
    return m;
}

static int16_t *appended(int conn, size_t *n)
{
    *n = fp_appended_len(&F, conn);
    int16_t *out = malloc((*n + 1) * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(out);
    fp_appended(&F, conn, out, *n);
    return out;
}

static int16_t *expected_mic(const int16_t *pcm, size_t total, size_t chunk, size_t *n)
{
    pt_mic_path_t *path = pt_mic_path_new(16000, 24000, S);
    TEST_ASSERT_NOT_NULL(path);
    size_t cap = total * 2 + 64;
    int16_t *out = malloc(cap * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(out);
    *n = 0;
    for (size_t at = 0; at < total; at += chunk) {
        size_t k = total - at < chunk ? total - at : chunk;
        *n += pt_mic_path_run(path, pcm + at, k, out + *n, cap - *n);
    }
    pt_mic_path_free(path);
    return out;
}

static const cJSON *tool_item(cJSON *msg)
{
    return cJSON_GetObjectItemCaseSensitive(msg, "item");
}

static void tool_outputs(char *out, size_t len)
{
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < F.n; i++) {
        const fp_call_t *c = &F.calls[i];
        if (c->kind != FP_RT_SEND) {
            continue;
        }
        if (strcmp(c->type, "conversation.item.create") == 0) {
            cJSON *msg = cJSON_Parse(c->json);
            const cJSON *item = tool_item(msg);
            const char *id = cJSON_GetObjectItemCaseSensitive(item, "call_id")->valuestring;
            const char *output = cJSON_GetObjectItemCaseSensitive(item, "output")->valuestring;
            used += (size_t)snprintf(out + used, len - used, "%s=%s;", id, output);
            cJSON_Delete(msg);
        } else if (strcmp(c->type, "response.create") == 0 && used > 0) {
            used += (size_t)snprintf(out + used, len - used, "create;");
        }
    }
}

static size_t volume_calls(float *out, size_t cap)
{
    size_t count = 0;
    for (size_t i = 0; i < F.n; i++) {
        if (F.calls[i].kind == FP_SET_VOLUME) {
            if (count < cap) {
                out[count] = F.calls[i].volume;
            }
            count++;
        }
    }
    return count;
}

static void tone(int16_t *out, size_t n, double hz, double amplitude, double rate)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)(sin(2.0 * PI * hz * (double)i / rate) * amplitude);
    }
}

void test_happy_turn_event_order(void)
{
    pt_convo_start(C, now);
    fp_pump(&F);
    mic(ZERO, CHUNK);
    mic(ZERO, CHUNK);
    TEST_ASSERT_TRUE(fp_appended_len(&F, 1) > 0);
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    transcript("r1", "П'ятдесят шість.");
    done("r1");
    advance(50);
    EXPECT_DEVICES(HAPPY);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_FALSE(fp_device_at(&F, i)->has_code);
    }
    TEST_ASSERT_TRUE(last_device()->follow_up);
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
    char calls[256];
    calls_of(1, calls, sizeof(calls));
    TEST_ASSERT_EQUAL_STRING_LEN("clear", calls, 5);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(2400, fp_played_before_end(&F, 0));
    TEST_ASSERT_TRUE(log_has("turn ended: answered"));
    TEST_ASSERT_FALSE(log_mentions("sk-test"));
}

void test_answer_audio_is_boosted_for_the_speaker(void)
{
    begin();
    int16_t wave[2400];
    tone(wave, 2400, 1000.0, 8000.0, 24000.0);
    speech();
    created("r1");
    delta("r1", wave, 2400);
    done("r1");
    advance(50);
    TEST_ASSERT_EQUAL_size_t(1, fp_device_count(&F, AN));
    pt_speaker_path_t *path = pt_speaker_path_new(24000, 24000, S);
    int16_t expected[4096];
    size_t n = pt_speaker_path_run(path, wave, 2400, expected, 4096);
    n += pt_speaker_path_flush(path, expected + n, 4096 - n);
    pt_speaker_path_free(path);
    int16_t got[4096];
    size_t m = fp_played(&F, got, 4096);
    TEST_ASSERT_EQUAL_size_t(2400, n);
    TEST_ASSERT_EQUAL_size_t(n, m);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, got, n);
    TEST_ASSERT_TRUE(max_abs(got, m) > 8000);
}

void test_events_wait_for_mic_stop_grace(void)
{
    begin();
    speech();
    answer("r1", "Так.");
    EXPECT_DEVICES(L, SS, SP);
    advance(49);
    EXPECT_DEVICES(L, SS, SP);
    advance(1);
    EXPECT_DEVICES(HAPPY);
    uint32_t vad_end = fp_time_of(&F, SP, 0);
    TEST_ASSERT_EQUAL_UINT32(vad_end + 50, fp_time_of(&F, AS, 0));

    begin();
    speech();
    created("r2");
    transcript("r2", "Так.");
    done("r2");
    advance(50);
    EXPECT_DEVICES_FROM(5, L, SS, SP, AN);
    vad_end = fp_time_of(&F, SP, 1);
    TEST_ASSERT_EQUAL_UINT32(vad_end + 50, fp_time_of(&F, AN, 1));
}

void test_no_tts_end_after_streaming(void)
{
    begin();
    speech();
    answer("r1", "Так.");
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_EQUAL_size_t(1, endings());
}

void test_tts_end_when_nothing_streamed(void)
{
    begin();
    speech();
    created("r1");
    transcript("r1", "Так.");
    done("r1");
    advance(50);
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, AS));
    EXPECT_DEVICES(L, SS, SP, AN);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_PLAY));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
}

void test_audio_buffered_until_session_ready(void)
{
    F.conn[1].manual = true;
    int16_t wave[1536];
    tone(wave, 1536, 440.0, 8000.0, 16000.0);
    pt_convo_start(C, now);
    for (size_t at = 0; at < 1536; at += CHUNK) {
        mic(wave + at, CHUNK);
    }
    advance(50);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "input_audio_buffer.append", 0));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_SEND));
    advance(150);
    fp_connect(&F);
    size_t n_expected = 0;
    int16_t *expected = expected_mic(wave, 1536, CHUNK, &n_expected);
    size_t n = 0;
    int16_t *sent = appended(1, &n);
    TEST_ASSERT_TRUE(n_expected > 0);
    TEST_ASSERT_EQUAL_size_t(n_expected, n);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, sent, n);
    free(expected);
    free(sent);
    TEST_ASSERT_EQUAL_STRING("session.update", fp_nth(&F, FP_RT_SEND, 0)->type);
    TEST_ASSERT_EQUAL_STRING("input_audio_buffer.clear", fp_nth(&F, FP_RT_SEND, 1)->type);
    TEST_ASSERT_EQUAL_STRING("input_audio_buffer.append", fp_nth(&F, FP_RT_SEND, 2)->type);
}

void test_audio_after_speech_stopped_not_sent(void)
{
    begin();
    for (int i = 0; i < 3; i++) {
        mic(ZERO, CHUNK);
    }
    TEST_ASSERT_TRUE(fp_appended_len(&F, 1) > 0);
    speech();
    EXPECT_DEVICES(L, SS, SP);
    size_t sent = fp_appended_len(&F, 1);
    for (int i = 0; i < 5; i++) {
        mic(ZERO, CHUNK);
    }
    advance(50);
    TEST_ASSERT_EQUAL_size_t(sent, fp_appended_len(&F, 1));
}

void test_first_turn_no_speech_timeout(void)
{
    S->timing.first_turn_s = 0.2f;
    apply();
    uint32_t started = now;
    begin();
    advance(199);
    EXPECT_DEVICES(L);
    advance(1);
    EXPECT_DEVICES(L, EN);
    TEST_ASSERT_EQUAL_UINT32(started + 200, last_device()->at);
    TEST_ASSERT_EQUAL_size_t(2, fp_sends(&F, "input_audio_buffer.clear", 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
}

void test_heard_speech_is_logged(void)
{
    begin();
    push("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"transcript\":\"Яка столиця Франції?\"}");
    TEST_ASSERT_TRUE(log_has("heard: Яка столиця Франції?"));
}

void test_turn_end_logs_device_audio_level(void)
{
    S->timing.first_turn_s = 0.2f;
    apply();
    begin();
    size_t total = 512 * 31;
    int16_t *wave = malloc(total * sizeof(int16_t));
    tone(wave, total, 1000.0, 2000.0, 16000.0);
    for (size_t at = 0; at < total; at += CHUNK) {
        mic(wave + at, CHUNK);
    }
    free(wave);
    advance(200);
    EXPECT_DEVICES(L, EN);
    char line[256];
    TEST_ASSERT_EQUAL_INT(1, log_count_prefix("device audio: ", line, sizeof(line)));
    double seconds = 0.0;
    int peak = 0;
    double dbfs = 0.0;
    TEST_ASSERT_EQUAL_INT(3, sscanf(line, "device audio: %lf s, peak %d (%lf dBFS)", &seconds, &peak, &dbfs));
    TEST_ASSERT_NOT_NULL(strstr(line, ": 0.99 s, peak "));
    TEST_ASSERT_TRUE(peak >= 1900 && peak <= 2200);
    TEST_ASSERT_TRUE(dbfs >= -25.0 && dbfs <= -23.4);
}

void test_device_audio_in_holdoff_is_not_sent(void)
{
    S->timing.mic_holdoff_s = 0.064f;
    apply();
    begin();
    int16_t loud[CHUNK];
    int16_t quiet[CHUNK];
    for (int i = 0; i < CHUNK; i++) {
        loud[i] = 30000;
        quiet[i] = 100;
    }
    mic(loud, CHUNK);
    mic(loud, CHUNK);
    for (int i = 0; i < 4; i++) {
        mic(quiet, CHUNK);
    }
    size_t n = 0;
    int16_t *sent = appended(1, &n);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(max_abs(sent, n) < 1000);
    free(sent);
}

void test_a_new_holdoff_applies_from_the_next_question(void)
{
    answered_turn("r1");
    EXPECT_DEVICES(HAPPY);
    S->timing.mic_holdoff_s = 0.064f;
    apply();
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    int16_t loud[CHUNK];
    int16_t quiet[CHUNK];
    for (int i = 0; i < CHUNK; i++) {
        loud[i] = 30000;
        quiet[i] = 100;
    }
    mic(loud, CHUNK);
    mic(loud, CHUNK);
    for (int i = 0; i < 4; i++) {
        mic(quiet, CHUNK);
    }
    size_t n = 0;
    int16_t *sent = appended(2, &n);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(max_abs(sent, n) < 1000);
    free(sent);
}

void test_device_audio_is_high_passed_before_sending(void)
{
    begin();
    int16_t *drift = malloc(16000 * sizeof(int16_t));
    tone(drift, 16000, 3.0, 20000.0, 16000.0);
    for (size_t at = 0; at < 16000; at += CHUNK) {
        mic(drift + at, 16000 - at < CHUNK ? 16000 - at : CHUNK);
    }
    free(drift);
    size_t n = 0;
    int16_t *sent = appended(1, &n);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(max_abs(sent + n / 2, n - n / 2) < 200);
    free(sent);
}

void test_events_before_turn_ready_are_ignored(void)
{
    answered_turn("r1");
    pt_convo_start(C, now);
    const char *started_json = "{\"type\":\"input_audio_buffer.speech_started\"}";
    pt_convo_rt_message(C, started_json, strlen(started_json), now);
    fp_pump(&F);
    advance(300);
    EXPECT_DEVICES_FROM(5, L, EN);
}

void test_follow_up_uses_short_timeout(void)
{
    answered_turn("r1");
    uint32_t started = now;
    begin();
    advance(299);
    EXPECT_DEVICES_FROM(5, L);
    advance(1);
    EXPECT_DEVICES_FROM(5, L, EN);
    TEST_ASSERT_EQUAL_UINT32(started + 300, last_device()->at);
}

void test_follow_ups_are_capped(void)
{
    S->timing.max_follow_ups = 2;
    apply();
    for (int count = 1; count <= 3; count++) {
        char rid[8];
        snprintf(rid, sizeof(rid), "r%d", count);
        answered_turn(rid);
        TEST_ASSERT_EQUAL_size_t((size_t)count, endings());
    }
    TEST_ASSERT_TRUE(fp_nth(&F, FP_DEVICE, 4)->follow_up);
    TEST_ASSERT_TRUE(fp_nth(&F, FP_DEVICE, 9)->follow_up);
    TEST_ASSERT_FALSE(fp_nth(&F, FP_DEVICE, 14)->follow_up);
    TEST_ASSERT_EQUAL_INT(AN, fp_nth(&F, FP_DEVICE, 14)->ev);
    uint32_t started = now;
    begin();
    advance(999);
    TEST_ASSERT_EQUAL_size_t(3, endings());
    advance(1);
    TEST_ASSERT_EQUAL_size_t(4, endings());
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
    EXPECT_DEVICES_FROM(15, L, EN);
}

void test_max_speech_forces_answer(void)
{
    S->timing.max_speech_s = 0.2f;
    apply();
    begin();
    speech_started();
    advance(199);
    EXPECT_DEVICES(L, SS);
    advance(1);
    EXPECT_DEVICES(L, SS, SP);
    TEST_ASSERT_EQUAL_UINT32(fp_time_of(&F, SS, 0) + 200, fp_time_of(&F, SP, 0));
    EXPECT_CALLS(1, "clear,commit,create");
    answer("r1", "Так.");
    advance(50);
    EXPECT_DEVICES(HAPPY);
}

void test_volume_tool_call(void)
{
    begin();
    speech();
    created("r1");
    done_with("r1", "completed", NULL, DOWN_CALL);
    float volumes[4];
    TEST_ASSERT_EQUAL_size_t(1, volume_calls(volumes, 4));
    TEST_ASSERT_EQUAL_FLOAT(0.55f, volumes[0]);
    char outputs[256];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("c1={\"volume\": 55};create;", outputs);
    TEST_ASSERT_EQUAL_size_t(0, endings());
    answer("r2", "Зробив тихіше.");
    advance(50);
    EXPECT_DEVICES(HAPPY);
}

void test_bad_tool_calls_return_errors(void)
{
    begin();
    speech();
    created("r1");
    done_with("r1", "completed", NULL,
              CALL("c1", "set_volume", "\"{\\\"action\\\":\\\"sideways\\\"}\"") "," CALL("c2", "open_door", "\"{}\"") "," CALL(
                  "c3", "set_volume", "\"not json\""));
    TEST_ASSERT_EQUAL_size_t(3, fp_sends(&F, "conversation.item.create", 1));
    char outputs[512];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("c1={\"error\": \"unknown set_volume action: 'sideways'\"};"
                             "c2={\"error\": \"unknown tool open_door\"};"
                             "c3={\"error\": \"set_volume arguments are not valid JSON\"};create;",
                             outputs);
    for (size_t i = 0; i < F.n; i++) {
        if (strcmp(F.calls[i].type, "conversation.item.create") == 0) {
            cJSON *msg = cJSON_Parse(F.calls[i].json);
            cJSON *output = cJSON_Parse(cJSON_GetObjectItemCaseSensitive(tool_item(msg), "output")->valuestring);
            TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(output, "error")));
            cJSON_Delete(output);
            cJSON_Delete(msg);
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_SET_VOLUME));
}

void test_end_conversation_without_audio(void)
{
    S->timing.response_timeout_s = 0.3f;
    S->timing.session_idle_s = 5.0f;
    apply();
    begin();
    speech();
    created("r1");
    done_with("r1", "completed", NULL, END_CALL);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_TRUE(fp_time_of(&F, AN, 0) - fp_time_of(&F, SP, 0) >= 50);
    char outputs[128];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("e1={\"ok\": true};", outputs);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    advance(400);
    TEST_ASSERT_EQUAL_size_t(1, endings());
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    uint32_t started = now;
    begin();
    advance(1000);
    TEST_ASSERT_EQUAL_size_t(2, endings());
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
    EXPECT_DEVICES_FROM(4, L, EN);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_end_conversation_after_goodbye_audio(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    transcript("r1", "До зустрічі!");
    done_with("r1", "completed", NULL, END_CALL);
    advance(50);
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    char outputs[128];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("e1={\"ok\": true};", outputs);
    TEST_ASSERT_EQUAL_size_t(2400, fp_played_before_end(&F, 0));
}

void test_end_conversation_with_volume(void)
{
    S->timing.response_timeout_s = 0.3f;
    apply();
    begin();
    speech();
    created("r1");
    done_with("r1", "completed", NULL, DOWN_CALL "," END_CALL);
    advance(50);
    float volumes[4];
    TEST_ASSERT_EQUAL_size_t(1, volume_calls(volumes, 4));
    TEST_ASSERT_EQUAL_FLOAT(0.55f, volumes[0]);
    char outputs[128];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("c1={\"volume\": 55};e1={\"ok\": true};", outputs);
    EXPECT_DEVICES(L, SS, SP, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    advance(400);
    TEST_ASSERT_EQUAL_size_t(1, endings());
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
}

void test_end_conversation_resets_follow_up_count(void)
{
    S->timing.max_follow_ups = 2;
    apply();
    for (int number = 1; number <= 5; number++) {
        char rid[8];
        snprintf(rid, sizeof(rid), "r%d", number);
        begin();
        speech();
        if (number == 2) {
            created(rid);
            done_with(rid, "completed", NULL, END_CALL);
        } else {
            answer(rid, "Так.");
        }
        advance(50);
        TEST_ASSERT_EQUAL_size_t((size_t)number, endings());
    }
    bool flags[5];
    size_t k = 0;
    for (size_t i = 0; i < device_total(); i++) {
        if (fp_device_at(&F, i)->ev == AN) {
            flags[k++] = fp_device_at(&F, i)->follow_up;
        }
    }
    TEST_ASSERT_EQUAL_size_t(5, k);
    TEST_ASSERT_TRUE(flags[0]);
    TEST_ASSERT_FALSE(flags[1]);
    TEST_ASSERT_TRUE(flags[2]);
    TEST_ASSERT_TRUE(flags[3]);
    TEST_ASSERT_FALSE(flags[4]);
}

void test_connect_failure_reports_error(void)
{
    F.conn[1].fail_connect = true;
    start();
    EXPECT_DEVICES(L, FA);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_slow_connect_failure_reports_error(void)
{
    S->timing.first_turn_s = 0.1f;
    apply();
    F.conn[1].manual = true;
    start();
    advance(300);
    EXPECT_DEVICES(L);
    pt_convo_rt_failed(C, "connect failed", now);
    EXPECT_DEVICES(L, FA);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
}

void test_no_speech_timer_starts_when_ready(void)
{
    S->timing.first_turn_s = 0.2f;
    apply();
    F.conn[1].manual = true;
    start();
    advance(250);
    EXPECT_DEVICES(L);
    fp_connect(&F);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "input_audio_buffer.clear", 1));
    uint32_t ready = now;
    EXPECT_DEVICES(L);
    advance(199);
    EXPECT_DEVICES(L);
    advance(1);
    EXPECT_DEVICES(L, EN);
    TEST_ASSERT_EQUAL_UINT32(ready + 200, last_device()->at);
    EXPECT_CALLS(1, "clear,clear");
}

void test_failed_response_reports_error(void)
{
    begin();
    speech();
    created("r1");
    done_with("r1", "failed", "insufficient_quota", NULL);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, FA);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_STRING("openai-error", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

void test_session_closed_mid_turn(void)
{
    begin();
    speech();
    closed();
    advance(50);
    EXPECT_DEVICES(L, SS, SP, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_TRUE(fp_time_of(&F, FA, 0) - fp_time_of(&F, SP, 0) >= 50);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_response_timeout(void)
{
    S->timing.response_timeout_s = 0.2f;
    apply();
    begin();
    speech();
    advance(199);
    EXPECT_DEVICES(L, SS, SP);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, FA);
    TEST_ASSERT_EQUAL_STRING("timeout", last_device()->code);
    TEST_ASSERT_EQUAL_UINT32(fp_time_of(&F, SP, 0) + 200, fp_time_of(&F, FA, 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
}

static void push_deltas(const char *rid, uint32_t ms, uint32_t every)
{
    for (uint32_t t = 0; t < ms; t += every) {
        delta(rid, ZERO, 240);
        advance(every);
    }
}

void test_long_answer_not_cut_while_audio_flows(void)
{
    S->timing.response_timeout_s = 0.2f;
    apply();
    begin();
    speech();
    created("r1");
    push_deltas("r1", 600, 100);
    transcript("r1", "Довга відповідь.");
    done("r1");
    advance(50);
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
}

void test_silence_while_answering_times_out(void)
{
    S->timing.response_timeout_s = 0.6f;
    apply();
    begin();
    speech();
    created("r1");
    advance(300);
    uint32_t delta_at = now;
    delta("r1", ZERO, 240);
    advance(599);
    EXPECT_DEVICES(L, SS, SP, AS);
    advance(1);
    EXPECT_DEVICES(L, SS, SP, AS, FA);
    TEST_ASSERT_EQUAL_STRING("timeout", last_device()->code);
    TEST_ASSERT_EQUAL_UINT32(delta_at + 600, fp_time_of(&F, FA, 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
}

void test_answer_hard_cap(void)
{
    S->timing.answer_max_s = 0.3f;
    apply();
    begin();
    speech();
    created("r1");
    push_deltas("r1", 600, 100);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.cancel", 1));
    TEST_ASSERT_EQUAL_INT(FA, last_device()->ev);
    TEST_ASSERT_EQUAL_STRING("timeout", last_device()->code);
    TEST_ASSERT_EQUAL_UINT32(fp_time_of(&F, SP, 0) + 300, fp_time_of(&F, FA, 0));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_tool_round_trip_rearms_answer_timer(void)
{
    S->timing.response_timeout_s = 0.5f;
    apply();
    begin();
    speech();
    created("r1");
    advance(300);
    done_with("r1", "completed", NULL, DOWN_CALL);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "conversation.item.create", 1));
    advance(300);
    answer("r2", "Зробив тихіше.");
    advance(50);
    EXPECT_DEVICES(HAPPY);
}

void test_send_failure_mid_turn_reports_error(void)
{
    start();
    mic(ZERO, CHUNK);
    mic(ZERO, CHUNK);
    TEST_ASSERT_TRUE(fp_appended_len(&F, 1) > 0);
    closed();
    EXPECT_DEVICES(L, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
}

void test_stop_cancels_response_and_ignores_stale_events(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS);
    stop();
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.cancel", 1));
    EXPECT_DEVICES(L, SS, SP, AS, EN);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));

    begin();
    delta("r1", ZERO, 2400);
    done_with("r1", "cancelled", NULL, NULL);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(5, HAPPY);
    TEST_ASSERT_EQUAL_size_t(2, fp_count(&F, FP_PLAY_END));
}

void test_stop_before_announce_still_pairs_run_start(void)
{
    pt_convo_start(C, now);
    stop();
    advance(50);
    EXPECT_DEVICES(L, EN);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(fp_count(&F, FP_RT_OPEN), fp_count(&F, FP_RT_CLOSE));
}

void test_stop_while_speaking_cancels_response(void)
{
    begin();
    speech_started();
    EXPECT_DEVICES(L, SS);
    stop();
    EXPECT_CALLS(1, "clear,cancel,clear");
    EXPECT_DEVICES(L, SS, EN);

    begin();
    created("r1");
    delta("r1", ZERO, 2400);
    done_with("r1", "cancelled", NULL, NULL);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(3, HAPPY);
}

void test_new_start_while_speaking_cancels_response(void)
{
    begin();
    speech_started();
    EXPECT_DEVICES(L, SS);
    begin();
    EXPECT_CALLS(1, "clear,cancel,clear");
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    created("r1");
    done_with("r1", "cancelled", NULL, NULL);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(2, HAPPY);
}

void test_cancel_failure_after_stop_drops_session(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    closed();
    stop();
    advance(50);
    TEST_ASSERT_EQUAL_INT(EN, last_device()->ev);
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.cancel", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

void test_stop_without_turn_sends_run_end(void)
{
    stop();
    stop();
    EXPECT_DEVICES(EN, EN);
    TEST_ASSERT_EQUAL_INT(0, F.opened);
    answered_turn("r1");
    TEST_ASSERT_EQUAL_size_t(3, endings());
    stop();
    EXPECT_DEVICES(EN, EN, HAPPY, EN);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "response.cancel", 0));
}

void test_stop_during_answer_grace_ends_conversation(void)
{
    S->timing.mic_stop_grace_s = 0.5f;
    apply();
    begin();
    speech();
    created("r1");
    transcript("r1", "Так.");
    done("r1");
    advance(150);
    stop();
    advance(350);
    TEST_ASSERT_EQUAL_size_t(1, fp_device_count(&F, AN));
    TEST_ASSERT_FALSE(fp_last(&F, FP_DEVICE)->follow_up);
    TEST_ASSERT_EQUAL_size_t(1, endings());
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    uint32_t started = now;
    begin();
    advance(1000);
    TEST_ASSERT_EQUAL_size_t(2, endings());
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
}

void test_stop_while_turn_is_ending_adds_no_run_end(void)
{
    S->timing.mic_stop_grace_s = 0.3f;
    apply();
    begin();
    speech();
    created("r1");
    transcript("r1", "Так.");
    done("r1");
    advance(100);
    stop();
    advance(250);
    TEST_ASSERT_EQUAL_size_t(1, endings());
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, EN));
}

void test_stop_after_answer_clears_follow_up(void)
{
    answered_turn("r1");
    size_t count = device_total();
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
    stop();
    EXPECT_DEVICES_FROM(count, EN);
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    uint32_t started = now;
    begin();
    advance(1000);
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
    EXPECT_DEVICES_FROM(count + 1, L, EN);
}

void test_session_dropped_after_the_answer_makes_the_next_start_a_first_turn(void)
{
    answered_turn("r1");
    TEST_ASSERT_TRUE(fp_device_at(&F, 4)->follow_up);
    closed();
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    uint32_t started = now;
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    advance(1000);
    TEST_ASSERT_EQUAL_size_t(2, endings());
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
    TEST_ASSERT_EQUAL_INT(2, log_count_prefix("turn started (first turn)", NULL, 0));
    TEST_ASSERT_EQUAL_INT(2, log_count_prefix("turn started", NULL, 0));
}

void test_new_start_replaces_running_turn(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS);
    begin();
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    EXPECT_CALLS(1, "clear,cancel,clear");
    EXPECT_DEVICES(L, SS, SP, AS, L);
    TEST_ASSERT_TRUE(log_has("turn ended: replaced by a new start"));
    done_with("r1", "cancelled", NULL, NULL);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(4, HAPPY);
    TEST_ASSERT_EQUAL_size_t(2, fp_count(&F, FP_PLAY_END));
}

void test_session_closed_after_idle(void)
{
    S->timing.session_idle_s = 0.2f;
    apply();
    answered_turn("r1");
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(299);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(1);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    speech();
    answer("r2", "Так.");
    advance(50);
    TEST_ASSERT_EQUAL_size_t(2, fp_device_count(&F, AN));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 2));
}

void test_idle_close_waits_for_playback(void)
{
    S->timing.session_idle_s = 0.2f;
    apply();
    begin();
    speech();
    created("r1");
    static int16_t pcm[12000];
    delta("r1", pcm, 12000);
    transcript("r1", "Так.");
    done("r1");
    advance(50);
    TEST_ASSERT_EQUAL_size_t(1, fp_device_count(&F, AN));
    TEST_ASSERT_EQUAL_size_t(12000, fp_played_before_end(&F, 0));
    uint32_t ended = fp_time_of(&F, AN, 0);
    advance(ended + 400 - now);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(299);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    advance(1);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_UINT32(ended + 700, now);
}

void test_session_reused_within_idle(void)
{
    answered_turn("r1");
    answered_turn("r2");
    TEST_ASSERT_EQUAL_size_t(2, fp_device_count(&F, AN));
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 0));
}

void test_dead_reused_session_is_replaced(void)
{
    S->timing.ready_check_s = 0.2f;
    apply();
    answered_turn("r1");
    F.conn[1].silent = true;
    size_t sent = fp_appended_len(&F, 1);
    pt_convo_start(C, now);
    fp_pump(&F);
    for (int i = 0; i < 3; i++) {
        mic(ZERO, CHUNK);
    }
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(199);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    advance(1);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "input_audio_buffer.clear", 2));
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(2, fp_sends(&F, "input_audio_buffer.clear", 1));
    TEST_ASSERT_EQUAL_size_t(sent, fp_appended_len(&F, 1));
    TEST_ASSERT_TRUE(fp_appended_len(&F, 2) > 0);
    TEST_ASSERT_TRUE(log_has("realtime session did not answer; reconnecting"));
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(5, HAPPY);
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "session.update", 2));
}

void test_dead_reused_session_and_failed_reconnect_report_error(void)
{
    S->timing.ready_check_s = 0.2f;
    apply();
    answered_turn("r1");
    F.conn[1].silent = true;
    F.conn[2].fail_connect = true;
    start();
    advance(200);
    EXPECT_DEVICES_FROM(5, L, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

void test_fresh_session_is_not_checked(void)
{
    S->timing.ready_check_s = 5.0f;
    apply();
    F.conn[1].silent = true;
    begin();
    for (int i = 0; i < 3; i++) {
        mic(ZERO, CHUNK);
    }
    TEST_ASSERT_TRUE(fp_appended_len(&F, 1) > 0);
    speech();
    answer("r1", "Так.");
    advance(50);
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
}

void test_dead_session_close_does_not_delay_replacement(void)
{
    S->timing.ready_check_s = 0.2f;
    apply();
    answered_turn("r1");
    F.conn[1].silent = true;
    uint32_t started = now;
    start();
    for (int i = 0; i < 3; i++) {
        mic(ZERO, CHUNK);
    }
    advance(200);
    TEST_ASSERT_TRUE(fp_appended_len(&F, 2) > 0);
    TEST_ASSERT_TRUE(now - started < 600);
    const fp_call_t *first_append = NULL;
    for (size_t i = 0; i < F.n && first_append == NULL; i++) {
        if (F.calls[i].conn == 2 && strcmp(F.calls[i].type, "input_audio_buffer.append") == 0) {
            first_append = &F.calls[i];
        }
    }
    TEST_ASSERT_EQUAL_UINT32(started + 200, first_append->at);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
}

void test_closed_during_ready_check_reconnects(void)
{
    S->timing.ready_check_s = 1.0f;
    apply();
    answered_turn("r1");
    F.conn[1].silent = true;
    uint32_t started = now;
    start();
    TEST_ASSERT_EQUAL_size_t(2, fp_sends(&F, "input_audio_buffer.clear", 1));
    closed();
    for (int i = 0; i < 3; i++) {
        mic(ZERO, CHUNK);
    }
    TEST_ASSERT_TRUE(fp_appended_len(&F, 2) > 0);
    TEST_ASSERT_EQUAL_UINT32(started, now);
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_count_conn(&F, FP_RT_CLOSE, 1));
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(5, HAPPY);
}

void test_closed_on_fresh_session_before_ready_reports_error(void)
{
    F.conn[1].manual = true;
    start();
    pt_convo_rt_opened(C, now);
    closed();
    EXPECT_DEVICES(L, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_debug_input_replaces_mic_first_turn_only(void)
{
    int16_t loud[CHUNK];
    for (int i = 0; i < CHUNK; i++) {
        loud[i] = 10000;
    }
    int16_t four[4 * CHUNK];
    for (int i = 0; i < 4; i++) {
        memcpy(four + i * CHUNK, loud, sizeof(loud));
    }
    size_t n_expected = 0;
    int16_t *expected = expected_mic(four, 4 * CHUNK, CHUNK, &n_expected);

    begin();
    for (int i = 0; i < 4; i++) {
        mic(loud, CHUNK);
    }
    size_t n = 0;
    int16_t *sent = appended(1, &n);
    TEST_ASSERT_EQUAL_size_t(n_expected, n);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, sent, n);
    free(sent);
    speech();
    answer("r1", "Так.");
    advance(50);
    size_t first_turn = fp_appended_len(&F, 1);

    begin();
    for (int i = 0; i < 4; i++) {
        mic(loud, CHUNK);
    }
    advance(300);
    sent = appended(1, &n);
    TEST_ASSERT_EQUAL_size_t(first_turn + n_expected, n);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, sent + first_turn, n_expected);
    free(sent);
    free(expected);
    EXPECT_DEVICES_FROM(5, L, EN);
}

void test_close_finishes_stream_and_closes_session(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS);
    pt_convo_free(C);
    C = NULL;
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    size_t count = device_total();
    TEST_ASSERT_TRUE(log_has("turn ended: conversation closed"));
    new_convo();
    apply();
    advance(100);
    TEST_ASSERT_EQUAL_size_t(count, device_total());
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
}

void test_close_during_error_grace_closes_dropped_session(void)
{
    S->timing.mic_stop_grace_s = 0.5f;
    apply();
    begin();
    speech();
    created("r1");
    done_with("r1", "failed", "server_error", NULL);
    EXPECT_DEVICES(L, SS, SP);
    advance(100);
    pt_convo_free(C);
    C = NULL;
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_close_while_connecting(void)
{
    F.conn[1].manual = true;
    start();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    pt_convo_free(C);
    C = NULL;
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    EXPECT_DEVICES(L);
}

void test_a_start_that_fails_after_making_a_stream_leaves_no_stream_behind(void)
{
    pt_convo_free(C);
    new_convo();
    start();
    TEST_ASSERT_EQUAL_size_t(1, F.n);
    EXPECT_DEVICES(FA);
    TEST_ASSERT_EQUAL_UINT32(PT_DEADLINE_NONE, pt_convo_next_deadline(C));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    stop();
    EXPECT_DEVICES(FA, EN);
    apply();
    begin();
    TEST_ASSERT_EQUAL_INT(1, F.opened);
    EXPECT_DEVICES(FA, EN, L);
}

void test_handle_start_returns_none_on_internal_error(void)
{
    pt_convo_free(C);
    new_convo();
    start();
    advance(100);
    EXPECT_DEVICES(FA);
    TEST_ASSERT_TRUE(last_device()->has_code);
    TEST_ASSERT_EQUAL_STRING("start-failed", last_device()->code);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_EQUAL_INT(0, F.opened);
    TEST_ASSERT_TRUE(log_has("could not start a turn: no settings yet"));
}

void test_settings_change_mid_turn_applies_next_turn(void)
{
    begin();
    speech();
    created("r1");
    S->timing.response_timeout_s = 0.5f;
    snprintf(S->openai.voice, sizeof(S->openai.voice), "cedar");
    apply();
    advance(900);
    answer("r1", "Так.");
    advance(50);
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    const fp_call_t *close = fp_nth(&F, FP_RT_CLOSE, 0);
    TEST_ASSERT_NOT_NULL(close);
    TEST_ASSERT_TRUE(close < fp_nth(&F, FP_RT_OPEN, 1));
    TEST_ASSERT_TRUE(log_has("settings changed; opening a fresh realtime session"));
    const fp_call_t *update = NULL;
    for (size_t i = 0; i < F.n; i++) {
        if (F.calls[i].conn == 2 && strcmp(F.calls[i].type, "session.update") == 0) {
            update = &F.calls[i];
        }
    }
    TEST_ASSERT_NOT_NULL(update);
    TEST_ASSERT_EQUAL_STRING("cedar", update->voice);
    speech();
    advance(499);
    EXPECT_DEVICES_FROM(5, L, SS, SP);
    advance(1);
    EXPECT_DEVICES_FROM(5, L, SS, SP, FA);
    TEST_ASSERT_EQUAL_STRING("timeout", last_device()->code);
}

void test_connection_lost_mid_answer_fails_turn_and_next_turn_reopens(void)
{
    begin();
    speech();
    created("r1");
    delta("r1", ZERO, 2400);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS);
    closed();
    EXPECT_DEVICES(L, SS, SP, AS, FA);
    TEST_ASSERT_EQUAL_STRING("openai-unavailable", last_device()->code);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(5, HAPPY);
}

void test_app_answer_overflow_ends_cleanly(void)
{
    static int16_t second[24000];
    begin();
    speech();
    created("r1");
    delta("r1", second, 24000);
    delta("r1", second, 24000);
    advance(50);
    EXPECT_DEVICES(L, SS, SP, AS);
    pt_convo_rt_overflow(C, now);
    fp_pump(&F);
    EXPECT_DEVICES(L, SS, SP, AS, AN);
    TEST_ASSERT_FALSE(last_device()->follow_up);
    TEST_ASSERT_FALSE(last_device()->has_code);
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_EQUAL_size_t(1, fp_sends(&F, "response.cancel", 1));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_PLAY_END));
    TEST_ASSERT_EQUAL_size_t(48000, fp_played_before_end(&F, 0));
    TEST_ASSERT_TRUE(log_has("answer too long — cut after 2 s"));
    TEST_ASSERT_TRUE(log_has("turn ended: answer cut"));
    TEST_ASSERT_EQUAL_INT(0, log_count_prefix("realtime unavailable", NULL, 0));
    done("r1");
    advance(2000);
    TEST_ASSERT_EQUAL_size_t(1, fp_device_count(&F, AN));
    TEST_ASSERT_EQUAL_size_t(0, fp_device_count(&F, FA));

    pt_appstate_t *a = pt_appstate_new();
    TEST_ASSERT_NOT_NULL(a);
    pt_app_ev_t ev = {.kind = PT_APP_EV_WIFI_UP};
    pt_appstate_on(a, &ev);
    pt_appstate_settings(a, true);
    ev.kind = PT_APP_EV_WAKE;
    pt_appstate_on(a, &ev);
    pt_app_out_t o = {0};
    for (size_t i = 0; i < fp_count(&F, FP_DEVICE); i++) {
        const fp_call_t *d = fp_device_at(&F, i);
        pt_app_ev_t dev = {.kind = PT_APP_EV_DEV, .dev = d->ev, .follow_up = d->follow_up,
                           .code = d->has_code ? d->code : NULL};
        o = pt_appstate_on(a, &dev);
    }
    TEST_ASSERT_EQUAL_INT(PT_RING_OFF, o.ring);
    TEST_ASSERT_TRUE(o.play_end_sound);
    TEST_ASSERT_FALSE(o.start_convo);
    TEST_ASSERT_TRUE(o.wake_listening);
    pt_appstate_free(a);

    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    speech();
    answer("r2", "Так.");
    advance(50);
    EXPECT_DEVICES_FROM(5, HAPPY);
}

void test_overflow_without_a_turn_closes_the_kept_session(void)
{
    answered_turn("r1");
    EXPECT_DEVICES(HAPPY);
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    pt_convo_start(C, now);
    fp_pump(&F);
    pt_convo_stop(C, now);
    fp_pump(&F);
    size_t before = device_total();
    pt_convo_rt_overflow(C, now);
    fp_pump(&F);
    TEST_ASSERT_EQUAL_size_t(before, device_total());
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_TRUE(log_has("messages from OpenAI did not fit in memory; the connection is closed"));
    pt_convo_rt_overflow(C, now);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
}

void test_close_idle_drops_the_kept_session_but_not_a_turn(void)
{
    answered_turn("r1");
    TEST_ASSERT_TRUE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    begin();
    TEST_ASSERT_FALSE(pt_convo_close_idle(C, now));
    TEST_ASSERT_EQUAL_size_t(0, fp_count(&F, FP_RT_CLOSE));
    stop();
    size_t before = device_total();
    TEST_ASSERT_TRUE(pt_convo_close_idle(C, now));
    fp_pump(&F);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    TEST_ASSERT_EQUAL_size_t(before, device_total());
    TEST_ASSERT_FALSE(pt_convo_follow_up_pending(C));
    TEST_ASSERT_TRUE(pt_convo_close_idle(C, now));
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    advance(5000);
    TEST_ASSERT_EQUAL_size_t(1, fp_count(&F, FP_RT_CLOSE));
    begin();
    TEST_ASSERT_EQUAL_INT(2, F.opened);
    TEST_ASSERT_FALSE(pt_convo_close_idle(NULL, now));
}

void test_early_audio_buffer_is_capped_at_30_seconds(void)
{
    S->timing.first_turn_s = 100.0f;
    apply();
    F.conn[1].manual = true;
    size_t total = 4096;
    int16_t *wave = malloc(total * sizeof(int16_t));
    tone(wave, total, 440.0, 8000.0, 16000.0);
    start();
    for (int i = 0; i < 160; i++) {
        mic(wave, total);
    }
    fp_connect(&F);
    TEST_ASSERT_EQUAL_size_t(30u * PT_CONVO_RT_RATE, fp_appended_len(&F, 1));
    char line[256];
    TEST_ASSERT_EQUAL_INT(1, log_count_prefix("dropped ", line, sizeof(line)));
    free(wave);
}

void test_mic_audio_before_session_ready_is_buffered_not_lost(void)
{
    S->timing.first_turn_s = 0.1f;
    apply();
    F.conn[1].manual = true;
    size_t total = 10 * CHUNK;
    int16_t *wave = malloc(total * sizeof(int16_t));
    tone(wave, total, 440.0, 8000.0, 16000.0);
    start();
    for (size_t at = 0; at < total; at += CHUNK) {
        mic(wave + at, CHUNK);
    }
    advance(500);
    TEST_ASSERT_EQUAL_size_t(0, fp_sends(&F, "input_audio_buffer.append", 0));
    fp_connect(&F);
    size_t n_expected = 0;
    int16_t *expected = expected_mic(wave, total, CHUNK, &n_expected);
    size_t n = 0;
    int16_t *sent = appended(1, &n);
    TEST_ASSERT_EQUAL_size_t(2400, n);
    TEST_ASSERT_TRUE(n_expected > n);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected + (n_expected - n), sent, n);
    char line[256];
    TEST_ASSERT_EQUAL_INT(1, log_count_prefix("dropped ", line, sizeof(line)));
    free(expected);
    free(sent);
    free(wave);
    mic(ZERO, CHUNK);
    TEST_ASSERT_EQUAL_size_t(2400 + 768, fp_appended_len(&F, 1));
}

void test_set_volume_tool_calls_port(void)
{
    begin();
    speech();
    created("r1");
    done_with("r1", "completed", NULL,
              CALL("c1", "set_volume", "\"{\\\"action\\\":\\\"up\\\"}\"") "," CALL(
                  "c2", "set_volume", "\"{\\\"action\\\":\\\"set\\\",\\\"level\\\":30}\""));
    float volumes[4];
    TEST_ASSERT_EQUAL_size_t(2, volume_calls(volumes, 4));
    TEST_ASSERT_EQUAL_FLOAT(0.85f, volumes[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, volumes[1]);
    char outputs[256];
    tool_outputs(outputs, sizeof(outputs));
    TEST_ASSERT_EQUAL_STRING("c1={\"volume\": 85};c2={\"volume\": 30};create;", outputs);
    created("r2");
    done_with("r2", "completed", NULL, DOWN_CALL);
    TEST_ASSERT_EQUAL_size_t(3, volume_calls(volumes, 4));
    TEST_ASSERT_EQUAL_FLOAT(0.15f, volumes[2]);
    S->sound.volume = 0.5f;
    apply();
    created("r3");
    done_with("r3", "completed", NULL, CALL("c4", "set_volume", "\"{\\\"action\\\":\\\"up\\\"}\""));
    TEST_ASSERT_EQUAL_size_t(4, volume_calls(volumes, 4));
    TEST_ASSERT_EQUAL_FLOAT(0.65f, volumes[3]);
    TEST_ASSERT_TRUE(log_has("volume set to 0.65"));
}

void test_deadlines_survive_clock_wrap(void)
{
    pt_convo_free(C);
    now = UINT32_MAX - 300;
    new_convo();
    apply();
    uint32_t started = now;
    begin();
    advance(999);
    EXPECT_DEVICES(L);
    advance(1);
    EXPECT_DEVICES(L, EN);
    TEST_ASSERT_EQUAL_UINT32(started + 1000, last_device()->at);
    TEST_ASSERT_TRUE(last_device()->at < started);
    answered_turn("r1");
    EXPECT_DEVICES_FROM(2, HAPPY);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_turn_event_order);
    RUN_TEST(test_answer_audio_is_boosted_for_the_speaker);
    RUN_TEST(test_events_wait_for_mic_stop_grace);
    RUN_TEST(test_no_tts_end_after_streaming);
    RUN_TEST(test_tts_end_when_nothing_streamed);
    RUN_TEST(test_audio_buffered_until_session_ready);
    RUN_TEST(test_audio_after_speech_stopped_not_sent);
    RUN_TEST(test_first_turn_no_speech_timeout);
    RUN_TEST(test_heard_speech_is_logged);
    RUN_TEST(test_turn_end_logs_device_audio_level);
    RUN_TEST(test_device_audio_in_holdoff_is_not_sent);
    RUN_TEST(test_a_new_holdoff_applies_from_the_next_question);
    RUN_TEST(test_device_audio_is_high_passed_before_sending);
    RUN_TEST(test_events_before_turn_ready_are_ignored);
    RUN_TEST(test_follow_up_uses_short_timeout);
    RUN_TEST(test_follow_ups_are_capped);
    RUN_TEST(test_max_speech_forces_answer);
    RUN_TEST(test_volume_tool_call);
    RUN_TEST(test_bad_tool_calls_return_errors);
    RUN_TEST(test_end_conversation_without_audio);
    RUN_TEST(test_end_conversation_after_goodbye_audio);
    RUN_TEST(test_end_conversation_with_volume);
    RUN_TEST(test_end_conversation_resets_follow_up_count);
    RUN_TEST(test_connect_failure_reports_error);
    RUN_TEST(test_slow_connect_failure_reports_error);
    RUN_TEST(test_no_speech_timer_starts_when_ready);
    RUN_TEST(test_failed_response_reports_error);
    RUN_TEST(test_session_closed_mid_turn);
    RUN_TEST(test_response_timeout);
    RUN_TEST(test_long_answer_not_cut_while_audio_flows);
    RUN_TEST(test_silence_while_answering_times_out);
    RUN_TEST(test_answer_hard_cap);
    RUN_TEST(test_tool_round_trip_rearms_answer_timer);
    RUN_TEST(test_send_failure_mid_turn_reports_error);
    RUN_TEST(test_stop_cancels_response_and_ignores_stale_events);
    RUN_TEST(test_stop_before_announce_still_pairs_run_start);
    RUN_TEST(test_stop_while_speaking_cancels_response);
    RUN_TEST(test_new_start_while_speaking_cancels_response);
    RUN_TEST(test_cancel_failure_after_stop_drops_session);
    RUN_TEST(test_stop_without_turn_sends_run_end);
    RUN_TEST(test_stop_during_answer_grace_ends_conversation);
    RUN_TEST(test_stop_while_turn_is_ending_adds_no_run_end);
    RUN_TEST(test_stop_after_answer_clears_follow_up);
    RUN_TEST(test_session_dropped_after_the_answer_makes_the_next_start_a_first_turn);
    RUN_TEST(test_new_start_replaces_running_turn);
    RUN_TEST(test_session_closed_after_idle);
    RUN_TEST(test_idle_close_waits_for_playback);
    RUN_TEST(test_session_reused_within_idle);
    RUN_TEST(test_dead_reused_session_is_replaced);
    RUN_TEST(test_dead_reused_session_and_failed_reconnect_report_error);
    RUN_TEST(test_fresh_session_is_not_checked);
    RUN_TEST(test_dead_session_close_does_not_delay_replacement);
    RUN_TEST(test_closed_during_ready_check_reconnects);
    RUN_TEST(test_closed_on_fresh_session_before_ready_reports_error);
    RUN_TEST(test_debug_input_replaces_mic_first_turn_only);
    RUN_TEST(test_close_finishes_stream_and_closes_session);
    RUN_TEST(test_close_during_error_grace_closes_dropped_session);
    RUN_TEST(test_close_while_connecting);
    RUN_TEST(test_a_start_that_fails_after_making_a_stream_leaves_no_stream_behind);
    RUN_TEST(test_handle_start_returns_none_on_internal_error);
    RUN_TEST(test_settings_change_mid_turn_applies_next_turn);
    RUN_TEST(test_connection_lost_mid_answer_fails_turn_and_next_turn_reopens);
    RUN_TEST(test_mic_audio_before_session_ready_is_buffered_not_lost);
    RUN_TEST(test_app_answer_overflow_ends_cleanly);
    RUN_TEST(test_overflow_without_a_turn_closes_the_kept_session);
    RUN_TEST(test_close_idle_drops_the_kept_session_but_not_a_turn);
    RUN_TEST(test_early_audio_buffer_is_capped_at_30_seconds);
    RUN_TEST(test_set_volume_tool_calls_port);
    RUN_TEST(test_deadlines_survive_clock_wrap);
    return UNITY_END();
}
