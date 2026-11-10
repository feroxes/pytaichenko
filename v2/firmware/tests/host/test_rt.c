#include "unity.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "mbedtls/base64.h"
#include "pt_rt.h"
#include "pt_settings.h"

static pt_settings_t *S;

void setUp(void)
{
    S = malloc(sizeof(*S));
    TEST_ASSERT_NOT_NULL(S);
    pt_settings_defaults(S, "uk");
    snprintf(S->openai.api_key, sizeof(S->openai.api_key), "sk-test-secret");
}

void tearDown(void)
{
    free(S);
}

static cJSON *parsed(const char *text)
{
    TEST_ASSERT_NOT_NULL(text);
    cJSON *json = cJSON_Parse(text);
    TEST_ASSERT_NOT_NULL_MESSAGE(json, text);
    return json;
}

static cJSON *parsed_owned(char *text)
{
    cJSON *json = parsed(text);
    free(text);
    return json;
}

static void assert_same_json(cJSON *expected, char *actual_text)
{
    cJSON *actual = parsed(actual_text);
    if (!cJSON_Compare(expected, actual, 1)) {
        char msg[256];
        snprintf(msg, sizeof msg, "got: %.200s", actual_text);
        TEST_FAIL_MESSAGE(msg);
    }
    cJSON_Delete(actual);
    cJSON_Delete(expected);
    free(actual_text);
}

static cJSON *expected_session(const char *session_body, const char *instructions, const char *language)
{
    char *text = NULL;
    size_t cap = strlen(session_body) + 64;
    text = malloc(cap);
    TEST_ASSERT_NOT_NULL(text);
    snprintf(text, cap, "{\"type\":\"session.update\",\"session\":%s}", session_body);
    cJSON *root = parsed(text);
    free(text);
    cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
    cJSON_ReplaceItemInObject(session, "instructions", cJSON_CreateString(instructions));
    char *tools = pt_tools_json(language);
    cJSON_ReplaceItemInObject(session, "tools", parsed(tools));
    free(tools);
    return root;
}

void test_session_update_shape(void)
{
    cJSON *expected = expected_session(
        "{\"type\":\"realtime\",\"output_modalities\":[\"audio\"],\"instructions\":\"x\","
        "\"reasoning\":{\"effort\":\"low\"},"
        "\"audio\":{\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},"
        "\"noise_reduction\":{\"type\":\"far_field\"},"
        "\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.5,\"prefix_padding_ms\":300,"
        "\"silence_duration_ms\":600,\"create_response\":true,\"interrupt_response\":false}},"
        "\"output\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},\"voice\":\"marin\"}},"
        "\"tools\":[],\"tool_choice\":\"auto\"}",
        pt_default_instructions("uk"), "uk");
    assert_same_json(expected, pt_rt_session_update(S));
}

void test_session_update_takes_values_from_config(void)
{
    snprintf(S->openai.voice, sizeof(S->openai.voice), "cedar");
    snprintf(S->assistant.instructions, sizeof(S->assistant.instructions), "Hi");
    snprintf(S->openai.reasoning_effort, sizeof(S->openai.reasoning_effort), "high");
    S->openai.vad_threshold = 0.8f;
    S->openai.silence_ms = 900;
    cJSON *msg = parsed_owned(pt_rt_session_update(S));
    cJSON *sess = cJSON_GetObjectItemCaseSensitive(msg, "session");
    TEST_ASSERT_EQUAL_STRING("Hi", cJSON_GetObjectItemCaseSensitive(sess, "instructions")->valuestring);
    cJSON *effort = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(sess, "reasoning"), "effort");
    TEST_ASSERT_EQUAL_STRING("high", effort->valuestring);
    cJSON *audio = cJSON_GetObjectItemCaseSensitive(sess, "audio");
    cJSON *voice = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(audio, "output"), "voice");
    TEST_ASSERT_EQUAL_STRING("cedar", voice->valuestring);
    cJSON *td = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(audio, "input"), "turn_detection");
    cJSON *expected_td = parsed("{\"threshold\":0.8,\"silence_duration_ms\":900}");
    TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(td, "threshold"),
                                   cJSON_GetObjectItemCaseSensitive(expected_td, "threshold"), 1));
    TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(td, "silence_duration_ms"),
                                   cJSON_GetObjectItemCaseSensitive(expected_td, "silence_duration_ms"), 1));
    cJSON_Delete(expected_td);
    cJSON_Delete(msg);
}

static const cJSON *input_of(const cJSON *msg)
{
    const cJSON *sess = cJSON_GetObjectItemCaseSensitive(msg, "session");
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive(sess, "audio");
    return cJSON_GetObjectItemCaseSensitive(audio, "input");
}

void test_session_update_with_transcription(void)
{
    S->openai.log_heard = true;
    cJSON *msg = parsed_owned(pt_rt_session_update(S));
    cJSON *expected = parsed("{\"model\":\"gpt-4o-mini-transcribe\",\"language\":\"uk\"}");
    TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(input_of(msg), "transcription"), expected, 1));
    cJSON_Delete(expected);
    cJSON_Delete(msg);
}

void test_session_update_without_transcription_by_default(void)
{
    cJSON *msg = parsed_owned(pt_rt_session_update(S));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(input_of(msg), "transcription"));
    cJSON_Delete(msg);
}

void test_session_update_follows_the_language(void)
{
    pt_settings_defaults(S, "en");
    S->openai.log_heard = true;
    char *text = pt_rt_session_update(S);
    cJSON *msg = parsed(text);
    free(text);
    const cJSON *sess = cJSON_GetObjectItemCaseSensitive(msg, "session");
    const cJSON *transcription = cJSON_GetObjectItemCaseSensitive(input_of(msg), "transcription");
    TEST_ASSERT_EQUAL_STRING("en", cJSON_GetObjectItemCaseSensitive(transcription, "language")->valuestring);
    char *tools = pt_tools_json("en");
    cJSON *expected_tools = parsed(tools);
    free(tools);
    TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(sess, "tools"), expected_tools, 1));
    cJSON_Delete(expected_tools);
    TEST_ASSERT_EQUAL_STRING(pt_default_instructions("en"),
                             cJSON_GetObjectItemCaseSensitive(sess, "instructions")->valuestring);
    cJSON_Delete(msg);
}

void test_session_update_without_reasoning(void)
{
    S->openai.reasoning_effort[0] = '\0';
    cJSON *msg = parsed_owned(pt_rt_session_update(S));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(msg, "session"), "reasoning"));
    cJSON_Delete(msg);
}

void test_session_update_never_contains_the_api_key(void)
{
    S->openai.log_heard = true;
    char *text = pt_rt_session_update(S);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_NULL(strstr(text, "sk-test-secret"));
    TEST_ASSERT_NULL(strstr(text, "api_key"));
    free(text);
}

void test_session_update_rejects_unknown_language(void)
{
    snprintf(S->assistant.language, sizeof(S->assistant.language), "ru");
    TEST_ASSERT_NULL(pt_rt_session_update(S));
    TEST_ASSERT_NULL(pt_rt_session_update(NULL));
}

void test_append_audio_base64(void)
{
    int16_t pcm[] = {1, 2};
    char *text = pt_rt_append_audio(pcm, 2);
    cJSON *expected = parsed("{\"type\":\"input_audio_buffer.append\",\"audio\":\"AQACAA==\"}");
    assert_same_json(expected, text);
}

void test_append_audio_is_little_endian_for_negative_samples(void)
{
    int16_t pcm[] = {-2, 0x1234, INT16_MIN};
    char *text = pt_rt_append_audio(pcm, 3);
    cJSON *msg = parsed(text);
    free(text);
    const char *audio = cJSON_GetObjectItemCaseSensitive(msg, "audio")->valuestring;
    unsigned char raw[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, mbedtls_base64_decode(raw, sizeof raw, &olen, (const unsigned char *)audio, strlen(audio)));
    static const unsigned char want[] = {0xfe, 0xff, 0x34, 0x12, 0x00, 0x80};
    TEST_ASSERT_EQUAL_UINT(sizeof want, olen);
    TEST_ASSERT_EQUAL_MEMORY(want, raw, sizeof want);
    cJSON_Delete(msg);
}

void test_append_audio_empty_and_invalid(void)
{
    char *text = pt_rt_append_audio(NULL, 0);
    cJSON *expected = parsed("{\"type\":\"input_audio_buffer.append\",\"audio\":\"\"}");
    assert_same_json(expected, text);
    TEST_ASSERT_NULL(pt_rt_append_audio(NULL, 4));
}

void test_append_audio_large_chunk(void)
{
    enum { N = 48000 };
    int16_t *pcm = malloc(N * sizeof(*pcm));
    TEST_ASSERT_NOT_NULL(pcm);
    for (int i = 0; i < N; i++) {
        pcm[i] = (int16_t)(i * 7);
    }
    char *text = pt_rt_append_audio(pcm, N);
    cJSON *msg = parsed(text);
    free(text);
    const char *audio = cJSON_GetObjectItemCaseSensitive(msg, "audio")->valuestring;
    size_t olen = 0;
    unsigned char *raw = malloc(N * 2);
    TEST_ASSERT_NOT_NULL(raw);
    TEST_ASSERT_EQUAL_INT(0, mbedtls_base64_decode(raw, N * 2, &olen, (const unsigned char *)audio, strlen(audio)));
    TEST_ASSERT_EQUAL_UINT(N * 2, olen);
    TEST_ASSERT_EQUAL_MEMORY(pcm, raw, N * 2);
    free(raw);
    free(pcm);
    cJSON_Delete(msg);
}

typedef struct {
    char *sent[8];
    int count;
} sink_t;

static void sink_send(void *ctx, const char *json)
{
    sink_t *sink = ctx;
    TEST_ASSERT_TRUE(sink->count < 8);
    sink->sent[sink->count] = strdup(json);
    sink->count++;
}

static void sink_free(sink_t *sink)
{
    for (int i = 0; i < sink->count; i++) {
        free(sink->sent[i]);
    }
    sink->count = 0;
}

static pt_tool_call_t make_call(const char *id, const char *name)
{
    pt_tool_call_t c;
    memset(&c, 0, sizeof c);
    snprintf(c.call_id, sizeof c.call_id, "%s", id);
    snprintf(c.name, sizeof c.name, "%s", name);
    return c;
}

static void assert_output_item(const char *text, const char *call_id, const char *output)
{
    cJSON *msg = parsed(text);
    TEST_ASSERT_EQUAL_STRING("conversation.item.create", cJSON_GetObjectItemCaseSensitive(msg, "type")->valuestring);
    cJSON *item = cJSON_GetObjectItemCaseSensitive(msg, "item");
    cJSON *expected = cJSON_CreateObject();
    cJSON_AddStringToObject(expected, "type", "function_call_output");
    cJSON_AddStringToObject(expected, "call_id", call_id);
    cJSON_AddStringToObject(expected, "output", output);
    TEST_ASSERT_TRUE(cJSON_Compare(item, expected, 1));
    cJSON_Delete(expected);
    cJSON_Delete(msg);
}

static void assert_type(const char *text, const char *type)
{
    cJSON *msg = parsed(text);
    TEST_ASSERT_EQUAL_STRING(type, cJSON_GetObjectItemCaseSensitive(msg, "type")->valuestring);
    cJSON_Delete(msg);
}

void test_tool_outputs_then_single_response_create(void)
{
    pt_tool_call_t calls[] = {make_call("c1", "set_volume"), make_call("c2", "end_conversation")};
    const char *outputs[] = {"{\"volume\": 55}", "{}"};
    sink_t sink = {0};
    pt_rt_tool_outputs(calls, outputs, 2, true, sink_send, &sink);
    TEST_ASSERT_EQUAL_INT(3, sink.count);
    assert_output_item(sink.sent[0], "c1", "{\"volume\": 55}");
    assert_output_item(sink.sent[1], "c2", "{}");
    assert_type(sink.sent[2], "response.create");
    sink_free(&sink);
}

void test_tool_outputs_without_response(void)
{
    pt_tool_call_t first = make_call("c1", "end_conversation");
    pt_tool_call_t second = make_call("c2", "set_volume");
    const char *out1[] = {"{\"ok\": true}"};
    const char *out2[] = {"{\"volume\": 40}"};
    sink_t sink = {0};
    pt_rt_tool_outputs(&first, out1, 1, false, sink_send, &sink);
    TEST_ASSERT_EQUAL_INT(1, sink.count);
    pt_rt_tool_outputs(&second, out2, 1, true, sink_send, &sink);
    TEST_ASSERT_EQUAL_INT(3, sink.count);
    assert_output_item(sink.sent[0], "c1", "{\"ok\": true}");
    assert_output_item(sink.sent[1], "c2", "{\"volume\": 40}");
    assert_type(sink.sent[2], "response.create");
    sink_free(&sink);
}

void test_empty_tool_outputs_send_nothing(void)
{
    sink_t sink = {0};
    pt_rt_tool_outputs(NULL, NULL, 0, true, sink_send, &sink);
    pt_rt_tool_outputs(NULL, NULL, 0, false, sink_send, &sink);
    TEST_ASSERT_EQUAL_INT(0, sink.count);
    assert_type(pt_rt_clear(), "input_audio_buffer.clear");
}

void test_tool_outputs_escape_strings(void)
{
    pt_tool_call_t call = make_call("c\"1", "set_volume");
    const char *outputs[] = {"{\"error\": \"bad \\\"x\\\"\"}"};
    sink_t sink = {0};
    pt_rt_tool_outputs(&call, outputs, 1, false, sink_send, &sink);
    TEST_ASSERT_EQUAL_INT(1, sink.count);
    assert_output_item(sink.sent[0], "c\"1", outputs[0]);
    sink_free(&sink);
}

static int alloc_budget;
static int alloc_denied;

static void *budget_malloc(size_t n)
{
    if (alloc_budget <= 0) {
        alloc_denied++;
        return NULL;
    }
    alloc_budget--;
    return malloc(n);
}

void test_tool_outputs_failure_skips_response_create(void)
{
    pt_tool_call_t calls[] = {make_call("c1", "set_volume"), make_call("c2", "set_volume")};
    const char *outputs[] = {"{\"volume\": 55}", "{\"volume\": 60}"};
    cJSON_Hooks hooks = {budget_malloc, free};
    bool saw_partial = false;
    bool saw_complete = false;
    for (int budget = 0; budget < 80; budget++) {
        sink_t sink = {0};
        alloc_budget = budget;
        alloc_denied = 0;
        cJSON_InitHooks(&hooks);
        pt_rt_tool_outputs(calls, outputs, 2, true, sink_send, &sink);
        cJSON_InitHooks(NULL);
        bool has_create = false;
        int items = 0;
        for (int i = 0; i < sink.count; i++) {
            cJSON *msg = parsed(sink.sent[i]);
            const char *type = cJSON_GetObjectItemCaseSensitive(msg, "type")->valuestring;
            has_create = has_create || strcmp(type, "response.create") == 0;
            items += strcmp(type, "conversation.item.create") == 0;
            cJSON_Delete(msg);
        }
        if (alloc_denied > 0) {
            TEST_ASSERT_FALSE(has_create);
            saw_partial = saw_partial || items == 1;
        } else {
            TEST_ASSERT_TRUE(has_create);
            TEST_ASSERT_EQUAL_INT(2, items);
            saw_complete = true;
        }
        sink_free(&sink);
    }
    TEST_ASSERT_TRUE(saw_partial);
    TEST_ASSERT_TRUE(saw_complete);
}

void test_commit_cancel_clear_messages(void)
{
    assert_type(pt_rt_clear(), "input_audio_buffer.clear");
    assert_type(pt_rt_commit(), "input_audio_buffer.commit");
    assert_type(pt_rt_response_create(), "response.create");
    assert_type(pt_rt_cancel(), "response.cancel");
}

static void parse_ok(const char *json, pt_rt_event_t *ev)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(PT_OK, pt_rt_parse(json, strlen(json), ev), json);
}

static void assert_ignored(const char *json)
{
    pt_rt_event_t ev;
    memset(&ev, 0xA5, sizeof ev);
    pt_err_t rc = pt_rt_parse(json, json ? strlen(json) : 0, &ev);
    (void)rc;
    TEST_ASSERT_EQUAL_INT_MESSAGE(PT_RT_IGNORED, ev.kind, json ? json : "(null)");
    TEST_ASSERT_NULL(ev.text);
    TEST_ASSERT_NULL(ev.pcm);
    TEST_ASSERT_NULL(ev.calls);
    TEST_ASSERT_EQUAL_UINT(0, ev.n_calls);
    pt_rt_event_free(&ev);
}

void test_parse_event_speech_started(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"input_audio_buffer.speech_started\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_SPEECH_STARTED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_parse_event_speech_stopped(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"input_audio_buffer.speech_stopped\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_SPEECH_STOPPED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_parse_event_input_cleared(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"input_audio_buffer.cleared\",\"event_id\":\"e1\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_INPUT_CLEARED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_created(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.created\",\"response\":{\"id\":\"r1\",\"status\":\"in_progress\"}}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_CREATED, ev.kind);
    TEST_ASSERT_EQUAL_STRING("r1", ev.response_id);
    pt_rt_event_free(&ev);
}

void test_parse_event_audio_delta(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\",\"delta\":\"AQACAA==\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_AUDIO_DELTA, ev.kind);
    TEST_ASSERT_EQUAL_STRING("r1", ev.response_id);
    TEST_ASSERT_EQUAL_UINT(2, ev.pcm_len);
    TEST_ASSERT_EQUAL_INT16(1, ev.pcm[0]);
    TEST_ASSERT_EQUAL_INT16(2, ev.pcm[1]);
    pt_rt_event_free(&ev);
}

void test_parse_event_audio_delta_large(void)
{
    enum { N = 150000 };
    int16_t *pcm = malloc(N * sizeof(*pcm));
    TEST_ASSERT_NOT_NULL(pcm);
    for (int i = 0; i < N; i++) {
        pcm[i] = (int16_t)(i * 13 - 20000);
    }
    char *chunk = pt_rt_append_audio(pcm, N);
    cJSON *wrapper = parsed(chunk);
    free(chunk);
    const char *b64 = cJSON_GetObjectItemCaseSensitive(wrapper, "audio")->valuestring;
    size_t cap = strlen(b64) + 128;
    char *json = malloc(cap);
    TEST_ASSERT_NOT_NULL(json);
    int len = snprintf(json, cap, "{\"type\":\"response.output_audio.delta\",\"response_id\":\"r9\",\"delta\":\"%s\"}", b64);
    cJSON_Delete(wrapper);
    pt_rt_event_t ev;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_rt_parse(json, (size_t)len, &ev));
    TEST_ASSERT_EQUAL_INT(PT_RT_AUDIO_DELTA, ev.kind);
    TEST_ASSERT_EQUAL_UINT(N, ev.pcm_len);
    TEST_ASSERT_EQUAL_MEMORY(pcm, ev.pcm, N * sizeof(*pcm));
    pt_rt_event_free(&ev);
    free(json);
    free(pcm);
}

void test_parse_event_transcript_done(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"r1\",\"transcript\":\"Привіт\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_TRANSCRIPT_DONE, ev.kind);
    TEST_ASSERT_EQUAL_STRING("r1", ev.response_id);
    TEST_ASSERT_EQUAL_STRING("Привіт", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_completed(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"status_details\":null,"
             "\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[]}],"
             "\"usage\":{\"total_tokens\":10,\"input_tokens\":6,\"output_tokens\":4}}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_DONE, ev.kind);
    TEST_ASSERT_EQUAL_STRING("r1", ev.response_id);
    TEST_ASSERT_EQUAL_STRING("completed", ev.status);
    TEST_ASSERT_EQUAL_UINT(0, ev.n_calls);
    TEST_ASSERT_NULL(ev.calls);
    TEST_ASSERT_NULL(ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_without_usage(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r2\",\"status\":\"cancelled\",\"output\":[]}}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_DONE, ev.kind);
    TEST_ASSERT_EQUAL_STRING("r2", ev.response_id);
    TEST_ASSERT_EQUAL_STRING("cancelled", ev.status);
    TEST_ASSERT_EQUAL_UINT(0, ev.n_calls);
    TEST_ASSERT_NULL(ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_tool_call(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r3\",\"status\":\"completed\",\"output\":["
             "{\"type\":\"function_call\",\"call_id\":\"call_1\",\"name\":\"set_volume\","
             "\"arguments\":\"{\\\"action\\\":\\\"down\\\"}\"}],"
             "\"usage\":{\"total_tokens\":10,\"input_tokens\":6,\"output_tokens\":4}}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_DONE, ev.kind);
    TEST_ASSERT_EQUAL_STRING("completed", ev.status);
    TEST_ASSERT_EQUAL_UINT(1, ev.n_calls);
    TEST_ASSERT_EQUAL_STRING("call_1", ev.calls[0].call_id);
    TEST_ASSERT_EQUAL_STRING("set_volume", ev.calls[0].name);
    TEST_ASSERT_EQUAL_STRING("{\"action\":\"down\"}", ev.calls[0].arguments);
    TEST_ASSERT_NULL(ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_several_calls_and_missing_arguments(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r5\",\"status\":\"completed\",\"output\":["
             "{\"type\":\"message\"},"
             "{\"type\":\"function_call\",\"call_id\":\"a\",\"name\":\"set_volume\",\"arguments\":\"{}\"},"
             "{\"type\":\"function_call\",\"call_id\":\"b\",\"name\":\"end_conversation\",\"arguments\":null},"
             "{\"type\":\"function_call\",\"call_id\":\"c\",\"name\":\"end_conversation\"}]}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_DONE, ev.kind);
    TEST_ASSERT_EQUAL_UINT(3, ev.n_calls);
    TEST_ASSERT_EQUAL_STRING("a", ev.calls[0].call_id);
    TEST_ASSERT_EQUAL_STRING("{}", ev.calls[0].arguments);
    TEST_ASSERT_EQUAL_STRING("b", ev.calls[1].call_id);
    TEST_ASSERT_EQUAL_STRING("", ev.calls[1].arguments);
    TEST_ASSERT_EQUAL_STRING("c", ev.calls[2].call_id);
    TEST_ASSERT_EQUAL_STRING("", ev.calls[2].arguments);
    TEST_ASSERT_TRUE(pt_tools_ends_conversation(ev.calls, ev.n_calls));
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_failed(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r4\",\"status\":\"failed\","
             "\"status_details\":{\"type\":\"failed\",\"error\":{\"type\":\"server_error\","
             "\"code\":\"insufficient_quota\",\"message\":\"no quota\"}},\"output\":[],\"usage\":null}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_RESPONSE_DONE, ev.kind);
    TEST_ASSERT_EQUAL_STRING("failed", ev.status);
    TEST_ASSERT_EQUAL_STRING("insufficient_quota", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_response_done_failed_falls_back_to_error_type(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.done\",\"response\":{\"id\":\"r6\",\"status\":\"failed\","
             "\"status_details\":{\"error\":{\"type\":\"server_error\",\"code\":null}},\"output\":[]}}",
             &ev);
    TEST_ASSERT_EQUAL_STRING("server_error", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_error_with_code(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":\"rate_limit_exceeded\","
             "\"message\":\"slow down\"}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_ERROR, ev.kind);
    TEST_ASSERT_EQUAL_STRING("rate_limit_exceeded: slow down", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_error_without_code(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":null,\"message\":\"bad\"}}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_ERROR, ev.kind);
    TEST_ASSERT_EQUAL_STRING("invalid_request_error: bad", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_error_hides_keys(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"error\",\"error\":{\"code\":\"invalid_api_key\",\"message\":\"Incorrect API key provided: "
             "sk-proj-abc123XYZ****wxyz. Also sk-1 and task-sk-9 and sk-\"}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_ERROR, ev.kind);
    TEST_ASSERT_EQUAL_STRING("invalid_api_key: Incorrect API key provided: sk-***. Also sk-* and task-sk-9 and sk-",
                             ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_error_without_details(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"error\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_ERROR, ev.kind);
    TEST_ASSERT_EQUAL_STRING("unknown", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_input_transcript(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"i1\","
             "\"content_index\":0,\"transcript\":\"Яка столиця Франції?\"}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_INPUT_TRANSCRIPT, ev.kind);
    TEST_ASSERT_EQUAL_STRING("Яка столиця Франції?", ev.text);
    pt_rt_event_free(&ev);
}

void test_parse_event_session_created_and_updated(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"session.created\",\"session\":{\"model\":\"gpt-realtime-2.1\"}}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_SESSION_READY, ev.kind);
    TEST_ASSERT_EQUAL_STRING("created", ev.status);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1", ev.model);
    TEST_ASSERT_EQUAL_STRING("", ev.voice);
    pt_rt_event_free(&ev);
    parse_ok("{\"type\":\"session.updated\",\"session\":{\"model\":\"gpt-realtime-2.1\","
             "\"audio\":{\"output\":{\"voice\":\"cedar\"}}}}",
             &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_SESSION_READY, ev.kind);
    TEST_ASSERT_EQUAL_STRING("updated", ev.status);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1", ev.model);
    TEST_ASSERT_EQUAL_STRING("cedar", ev.voice);
    pt_rt_event_free(&ev);
    parse_ok("{\"type\":\"session.updated\",\"session\":{}}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_SESSION_READY, ev.kind);
    TEST_ASSERT_EQUAL_STRING("", ev.model);
    pt_rt_event_free(&ev);
}

void test_parse_event_unknown_type(void)
{
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"rate_limits.updated\"}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_IGNORED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_parse_event_no_type(void)
{
    pt_rt_event_t ev;
    parse_ok("{}", &ev);
    TEST_ASSERT_EQUAL_INT(PT_RT_IGNORED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_parse_skips_unknown_and_malformed(void)
{
    assert_ignored("{\"type\":\"rate_limits.updated\",\"rate_limits\":[]}");
    assert_ignored("{\"type\":\"response.created\"}");
    assert_ignored("{\"type\":\"response.created\",\"response\":{}}");
    assert_ignored("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\",\"delta\":\"!!!\"}");
    assert_ignored("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\"}");
    assert_ignored("{\"type\":\"response.output_audio.delta\",\"delta\":\"AQACAA==\"}");
    assert_ignored("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\",\"delta\":\"AQAC\"}");
    assert_ignored("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\",\"delta\":42}");
    assert_ignored("{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"r1\"}");
    assert_ignored("{\"type\":\"conversation.item.input_audio_transcription.completed\"}");
    assert_ignored("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"transcript\":7}");
    assert_ignored("{\"type\":\"response.done\"}");
    assert_ignored("{\"type\":\"response.done\",\"response\":{\"id\":\"r1\"}}");
    assert_ignored("{\"type\":\"response.done\",\"response\":{\"status\":\"completed\"}}");
    assert_ignored("{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":["
                   "{\"type\":\"function_call\",\"name\":\"set_volume\"}]}}");
    assert_ignored("{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":["
                   "{\"type\":\"function_call\",\"call_id\":\"a\",\"name\":\"set_volume\",\"arguments\":\"{}\"},"
                   "{\"type\":\"function_call\",\"call_id\":\"b\"}]}}");
    assert_ignored("{\"type\":7}");
    assert_ignored("not json");
    assert_ignored("[]");
    assert_ignored("\"text\"");
    assert_ignored("{\"type\":\"input_audio_buffer.speech_started\"");
    assert_ignored("");
    assert_ignored(NULL);
}

void test_parse_oversized_ids_are_ignored(void)
{
    char json[512];
    char longid[128];
    memset(longid, 'x', 100);
    longid[100] = '\0';
    snprintf(json, sizeof json, "{\"type\":\"response.created\",\"response\":{\"id\":\"%s\"}}", longid);
    assert_ignored(json);
    snprintf(json, sizeof json,
             "{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":["
             "{\"type\":\"function_call\",\"call_id\":\"%s\",\"name\":\"set_volume\"}]}}",
             longid);
    assert_ignored(json);
}

void test_parse_truncates_long_names_but_keeps_call_id(void)
{
    char json[512];
    char longname[80];
    memset(longname, 'n', 60);
    longname[60] = '\0';
    snprintf(json, sizeof json,
             "{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":["
             "{\"type\":\"function_call\",\"call_id\":\"c1\",\"name\":\"%s\",\"arguments\":\"{}\"}]}}",
             longname);
    pt_rt_event_t ev;
    parse_ok(json, &ev);
    TEST_ASSERT_EQUAL_UINT(1, ev.n_calls);
    TEST_ASSERT_EQUAL_STRING("c1", ev.calls[0].call_id);
    TEST_ASSERT_EQUAL_UINT(31, strlen(ev.calls[0].name));
    pt_rt_event_free(&ev);
}

void test_parse_ignores_the_length_not_the_terminator(void)
{
    const char json[] = "{\"type\":\"input_audio_buffer.speech_started\"}{\"type\":\"error\"}";
    pt_rt_event_t ev;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_rt_parse(json, strlen("{\"type\":\"input_audio_buffer.speech_started\"}"), &ev));
    TEST_ASSERT_EQUAL_INT(PT_RT_SPEECH_STARTED, ev.kind);
    pt_rt_event_free(&ev);
}

void test_event_free_is_safe(void)
{
    pt_rt_event_free(NULL);
    pt_rt_event_t ev;
    parse_ok("{\"type\":\"response.output_audio.delta\",\"response_id\":\"r1\",\"delta\":\"AQACAA==\"}", &ev);
    pt_rt_event_free(&ev);
    TEST_ASSERT_NULL(ev.pcm);
    TEST_ASSERT_EQUAL_UINT(0, ev.pcm_len);
    pt_rt_event_free(&ev);
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_rt_parse("{}", 2, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_update_shape);
    RUN_TEST(test_session_update_takes_values_from_config);
    RUN_TEST(test_session_update_with_transcription);
    RUN_TEST(test_session_update_without_transcription_by_default);
    RUN_TEST(test_session_update_follows_the_language);
    RUN_TEST(test_session_update_without_reasoning);
    RUN_TEST(test_session_update_never_contains_the_api_key);
    RUN_TEST(test_session_update_rejects_unknown_language);
    RUN_TEST(test_append_audio_base64);
    RUN_TEST(test_append_audio_is_little_endian_for_negative_samples);
    RUN_TEST(test_append_audio_empty_and_invalid);
    RUN_TEST(test_append_audio_large_chunk);
    RUN_TEST(test_tool_outputs_then_single_response_create);
    RUN_TEST(test_tool_outputs_without_response);
    RUN_TEST(test_empty_tool_outputs_send_nothing);
    RUN_TEST(test_tool_outputs_escape_strings);
    RUN_TEST(test_tool_outputs_failure_skips_response_create);
    RUN_TEST(test_commit_cancel_clear_messages);
    RUN_TEST(test_parse_event_speech_started);
    RUN_TEST(test_parse_event_speech_stopped);
    RUN_TEST(test_parse_event_input_cleared);
    RUN_TEST(test_parse_event_response_created);
    RUN_TEST(test_parse_event_audio_delta);
    RUN_TEST(test_parse_event_audio_delta_large);
    RUN_TEST(test_parse_event_transcript_done);
    RUN_TEST(test_parse_event_response_done_completed);
    RUN_TEST(test_parse_event_response_done_without_usage);
    RUN_TEST(test_parse_event_response_done_tool_call);
    RUN_TEST(test_parse_event_response_done_several_calls_and_missing_arguments);
    RUN_TEST(test_parse_event_response_done_failed);
    RUN_TEST(test_parse_event_response_done_failed_falls_back_to_error_type);
    RUN_TEST(test_parse_event_error_with_code);
    RUN_TEST(test_parse_event_error_without_code);
    RUN_TEST(test_parse_event_error_hides_keys);
    RUN_TEST(test_parse_event_error_without_details);
    RUN_TEST(test_parse_event_input_transcript);
    RUN_TEST(test_parse_event_session_created_and_updated);
    RUN_TEST(test_parse_event_unknown_type);
    RUN_TEST(test_parse_event_no_type);
    RUN_TEST(test_parse_skips_unknown_and_malformed);
    RUN_TEST(test_parse_oversized_ids_are_ignored);
    RUN_TEST(test_parse_truncates_long_names_but_keeps_call_id);
    RUN_TEST(test_parse_ignores_the_length_not_the_terminator);
    RUN_TEST(test_event_free_is_safe);
    return UNITY_END();
}
