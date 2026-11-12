#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "mbedtls/base64.h"
#include "pt_rt.h"
#include "pt_rt_internal.h"

#define SAMPLE_RATE 24000
#define PREFIX_PADDING_MS 300
#define TRANSCRIPTION_MODEL "gpt-4o-mini-transcribe"

static const char MSG_COMMIT[] = "{\"type\":\"input_audio_buffer.commit\"}";
static const char MSG_RESPONSE_CREATE[] = "{\"type\":\"response.create\"}";
static const char MSG_CLEAR[] = "{\"type\":\"input_audio_buffer.clear\"}";
static const char MSG_CANCEL[] = "{\"type\":\"response.cancel\"}";
static const char APPEND_SUFFIX[] = "\"}";

bool pt_rt_attach(cJSON *parent, const char *key, cJSON *child)
{
    if (child == NULL) {
        return false;
    }
    if (parent == NULL || !cJSON_AddItemToObject(parent, key, child)) {
        cJSON_Delete(child);
        return false;
    }
    return true;
}

bool pt_rt_add_str(cJSON *parent, const char *key, const char *value)
{
    return cJSON_AddStringToObject(parent, key, value) != NULL;
}

static bool add_audio_format(cJSON *parent)
{
    cJSON *format = cJSON_CreateObject();
    bool ok = format != NULL;
    ok = ok && pt_rt_add_str(format, "type", "audio/pcm");
    ok = ok && cJSON_AddNumberToObject(format, "rate", SAMPLE_RATE) != NULL;
    return pt_rt_attach(parent, "format", format) && ok;
}

static cJSON *turn_detection(const pt_settings_t *s)
{
    cJSON *td = cJSON_CreateObject();
    bool ok = td != NULL;
    double threshold = round((double)s->openai.vad_threshold * 10000.0) / 10000.0;
    ok = ok && pt_rt_add_str(td, "type", "server_vad");
    ok = ok && cJSON_AddNumberToObject(td, "threshold", threshold) != NULL;
    ok = ok && cJSON_AddNumberToObject(td, "prefix_padding_ms", PREFIX_PADDING_MS) != NULL;
    ok = ok && cJSON_AddNumberToObject(td, "silence_duration_ms", s->openai.silence_ms) != NULL;
    ok = ok && cJSON_AddBoolToObject(td, "create_response", 1) != NULL;
    ok = ok && cJSON_AddBoolToObject(td, "interrupt_response", 0) != NULL;
    if (!ok) {
        cJSON_Delete(td);
        return NULL;
    }
    return td;
}

static cJSON *audio_section(const pt_settings_t *s)
{
    cJSON *audio = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON *output = cJSON_CreateObject();
    cJSON *noise = cJSON_CreateObject();
    bool ok = audio != NULL && input != NULL && output != NULL && noise != NULL;
    ok = ok && add_audio_format(input);
    ok = ok && pt_rt_add_str(noise, "type", "far_field");
    ok = pt_rt_attach(input, "noise_reduction", noise) && ok;
    ok = pt_rt_attach(input, "turn_detection", turn_detection(s)) && ok;
    if (ok && s->openai.log_heard) {
        cJSON *transcription = cJSON_CreateObject();
        bool t_ok = transcription != NULL;
        t_ok = t_ok && pt_rt_add_str(transcription, "model", TRANSCRIPTION_MODEL);
        t_ok = t_ok && pt_rt_add_str(transcription, "language", s->assistant.language);
        ok = pt_rt_attach(input, "transcription", transcription) && t_ok && ok;
    }
    ok = ok && add_audio_format(output);
    ok = ok && pt_rt_add_str(output, "voice", s->openai.voice);
    ok = pt_rt_attach(audio, "input", input) && ok;
    ok = pt_rt_attach(audio, "output", output) && ok;
    if (!ok) {
        cJSON_Delete(audio);
        return NULL;
    }
    return audio;
}

char *pt_rt_session_update(const pt_settings_t *s)
{
    if (s == NULL) {
        return NULL;
    }
    cJSON *tools = pt_tools_object(s->assistant.language);
    if (tools == NULL) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *session = cJSON_CreateObject();
    cJSON *modalities = cJSON_CreateArray();
    bool ok = root != NULL && session != NULL && modalities != NULL;
    ok = ok && pt_rt_add_str(root, "type", "session.update");
    ok = ok && pt_rt_add_str(session, "type", "realtime");
    ok = ok && cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
    ok = pt_rt_attach(session, "output_modalities", modalities) && ok;
    ok = ok && pt_rt_add_str(session, "instructions", s->assistant.instructions);
    if (ok && s->openai.reasoning_effort[0] != '\0') {
        cJSON *reasoning = cJSON_CreateObject();
        bool r_ok = reasoning != NULL && pt_rt_add_str(reasoning, "effort", s->openai.reasoning_effort);
        ok = pt_rt_attach(session, "reasoning", reasoning) && r_ok && ok;
    }
    ok = pt_rt_attach(session, "audio", ok ? audio_section(s) : NULL) && ok;
    ok = pt_rt_attach(session, "tools", tools) && ok;
    ok = ok && pt_rt_add_str(session, "tool_choice", "auto");
    ok = pt_rt_attach(root, "session", session) && ok;
    char *text = NULL;
    if (ok) {
        text = cJSON_PrintUnformatted(root);
    }
    cJSON_Delete(root);
    return text;
}

char *pt_rt_audio_message(const char *type, const int16_t *pcm, size_t n)
{
    if (pcm == NULL && n > 0) {
        return NULL;
    }
    if (n > (SIZE_MAX / 4) - 64) {
        return NULL;
    }
    size_t bytes = n * 2;
    unsigned char *raw = malloc(bytes > 0 ? bytes : 1);
    if (raw == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t)pcm[i];
        raw[2 * i] = (unsigned char)(v & 0xff);
        raw[2 * i + 1] = (unsigned char)(v >> 8);
    }
    size_t prefix = strlen(type) + 20;
    size_t suffix = sizeof(APPEND_SUFFIX) - 1;
    size_t b64_cap = 4 * ((bytes + 2) / 3) + 1;
    char *out = malloc(prefix + b64_cap + suffix);
    if (out == NULL) {
        free(raw);
        return NULL;
    }
    snprintf(out, prefix + 1, "{\"type\":\"%s\",\"audio\":\"", type);
    size_t written = 0;
    int rc = mbedtls_base64_encode((unsigned char *)out + prefix, b64_cap, &written, raw, bytes);
    free(raw);
    if (rc != 0) {
        free(out);
        return NULL;
    }
    memcpy(out + prefix + written, APPEND_SUFFIX, suffix + 1);
    return out;
}

char *pt_rt_append_audio(const int16_t *pcm, size_t n)
{
    return pt_rt_audio_message("input_audio_buffer.append", pcm, n);
}

const char *pt_rt_commit(void)
{
    return MSG_COMMIT;
}

const char *pt_rt_response_create(void)
{
    return MSG_RESPONSE_CREATE;
}

const char *pt_rt_clear(void)
{
    return MSG_CLEAR;
}

const char *pt_rt_cancel(void)
{
    return MSG_CANCEL;
}

static char *tool_output_message(const char *call_id, const char *output)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *item = cJSON_CreateObject();
    bool ok = root != NULL && item != NULL;
    ok = ok && pt_rt_add_str(root, "type", "conversation.item.create");
    ok = ok && pt_rt_add_str(item, "type", "function_call_output");
    ok = ok && pt_rt_add_str(item, "call_id", call_id);
    ok = ok && pt_rt_add_str(item, "output", output != NULL ? output : "");
    ok = pt_rt_attach(root, "item", item) && ok;
    char *text = NULL;
    if (ok) {
        text = cJSON_PrintUnformatted(root);
    }
    cJSON_Delete(root);
    return text;
}

void pt_rt_tool_outputs(const pt_tool_call_t *calls, const char *const *outputs, size_t n, bool respond,
                        pt_rt_send_fn send, void *ctx)
{
    if (n == 0 || calls == NULL || outputs == NULL || send == NULL) {
        return;
    }
    bool complete = true;
    for (size_t i = 0; i < n; i++) {
        char *msg = tool_output_message(calls[i].call_id, outputs[i]);
        if (msg == NULL) {
            complete = false;
            continue;
        }
        send(ctx, msg);
        free(msg);
    }
    if (respond && complete) {
        send(ctx, MSG_RESPONSE_CREATE);
    }
}

const char *pt_rt_json_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

void pt_rt_copy_trunc(char *dst, size_t cap, const char *src)
{
    size_t len = strlen(src);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool copy_id(char *dst, size_t cap, const char *src)
{
    if (src == NULL || strlen(src) >= cap) {
        return false;
    }
    strcpy(dst, src);
    return true;
}

static pt_err_t fill_session(const cJSON *msg, pt_rt_event_t *out, const char *stage)
{
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(msg, "session");
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive(session, "audio");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(audio, "output");
    const char *model = pt_rt_json_str(session, "model");
    const char *voice = pt_rt_json_str(output, "voice");
    out->kind = PT_RT_SESSION_READY;
    pt_rt_copy_trunc(out->status, sizeof(out->status), stage);
    if (model != NULL) {
        pt_rt_copy_trunc(out->model, sizeof(out->model), model);
    }
    if (voice != NULL) {
        pt_rt_copy_trunc(out->voice, sizeof(out->voice), voice);
    }
    return PT_OK;
}

pt_err_t pt_rt_decode_pcm(const char *delta, pt_rt_event_t *out)
{
    if (delta == NULL) {
        return PT_ERR_PARSE;
    }
    size_t slen = strlen(delta);
    size_t olen = 0;
    int rc = mbedtls_base64_decode(NULL, 0, &olen, (const unsigned char *)delta, slen);
    if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return PT_ERR_PARSE;
    }
    if (olen % 2 != 0) {
        return PT_ERR_PARSE;
    }
    unsigned char *buf = malloc(olen > 0 ? olen : 2);
    if (buf == NULL) {
        return PT_ERR_NOMEM;
    }
    size_t written = 0;
    if (mbedtls_base64_decode(buf, olen, &written, (const unsigned char *)delta, slen) != 0 || written != olen) {
        free(buf);
        return PT_ERR_PARSE;
    }
    out->pcm = (int16_t *)(void *)buf;
    out->pcm_len = olen / 2;
    return PT_OK;
}

static pt_err_t fill_audio_delta(const cJSON *msg, pt_rt_event_t *out)
{
    const char *delta = pt_rt_json_str(msg, "delta");
    if (!copy_id(out->response_id, sizeof(out->response_id), pt_rt_json_str(msg, "response_id")) || delta == NULL) {
        return PT_ERR_PARSE;
    }
    pt_err_t rc = pt_rt_decode_pcm(delta, out);
    if (rc != PT_OK) {
        return rc;
    }
    out->kind = PT_RT_AUDIO_DELTA;
    return PT_OK;
}

static pt_err_t fill_text(const cJSON *msg, pt_rt_event_t *out, const char *key, pt_rt_kind_t kind, bool with_id)
{
    const char *value = pt_rt_json_str(msg, key);
    if (value == NULL || (with_id && !copy_id(out->response_id, sizeof(out->response_id), pt_rt_json_str(msg, "response_id")))) {
        return PT_ERR_PARSE;
    }
    out->text = strdup(value);
    if (out->text == NULL) {
        return PT_ERR_NOMEM;
    }
    out->kind = kind;
    return PT_OK;
}

static bool key_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '*';
}

static void hide_keys(char *s)
{
    char *at = s;
    while ((at = strstr(at, "sk-")) != NULL) {
        if (at != s && key_char(at[-1])) {
            at += 3;
            continue;
        }
        size_t len = 3;
        while (key_char(at[len])) {
            len++;
        }
        size_t keep = len - 3 < 3 ? len - 3 : 3;
        memset(at + 3, '*', keep);
        memmove(at + 3 + keep, at + len, strlen(at + len) + 1);
        at += 3 + keep;
    }
}

static pt_err_t fill_error(const cJSON *msg, pt_rt_event_t *out)
{
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(msg, "error");
    const char *code = pt_rt_json_str(error, "code");
    if (code == NULL || code[0] == '\0') {
        code = pt_rt_json_str(error, "type");
    }
    if (code == NULL || code[0] == '\0') {
        code = "unknown";
    }
    const char *message = pt_rt_json_str(error, "message");
    if (message == NULL) {
        message = "";
    }
    size_t len = strlen(code) + (message[0] != '\0' ? strlen(message) + 2 : 0);
    out->text = malloc(len + 1);
    if (out->text == NULL) {
        return PT_ERR_NOMEM;
    }
    if (message[0] != '\0') {
        strcpy(out->text, code);
        strcat(out->text, ": ");
        strcat(out->text, message);
    } else {
        strcpy(out->text, code);
    }
    hide_keys(out->text);
    out->kind = PT_RT_ERROR;
    return PT_OK;
}

static pt_err_t fill_response_done(const cJSON *msg, pt_rt_event_t *out)
{
    const cJSON *response = cJSON_GetObjectItemCaseSensitive(msg, "response");
    const char *status = pt_rt_json_str(response, "status");
    if (!cJSON_IsObject(response) || !copy_id(out->response_id, sizeof(out->response_id), pt_rt_json_str(response, "id")) ||
        status == NULL) {
        return PT_ERR_PARSE;
    }
    pt_rt_copy_trunc(out->status, sizeof(out->status), status);
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(response, "status_details");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(details, "error");
    const char *code = pt_rt_json_str(error, "code");
    if (code == NULL || code[0] == '\0') {
        code = pt_rt_json_str(error, "type");
    }
    if (code != NULL && code[0] != '\0') {
        out->text = strdup(code);
        if (out->text == NULL) {
            return PT_ERR_NOMEM;
        }
    }
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(response, "output");
    size_t count = 0;
    const cJSON *item;
    if (cJSON_IsArray(items)) {
        cJSON_ArrayForEach(item, items)
        {
            const char *type = pt_rt_json_str(item, "type");
            if (type != NULL && strcmp(type, "function_call") == 0) {
                count++;
            }
        }
    }
    if (count > 0) {
        out->calls = calloc(count, sizeof(*out->calls));
        if (out->calls == NULL) {
            return PT_ERR_NOMEM;
        }
        cJSON_ArrayForEach(item, items)
        {
            const char *type = pt_rt_json_str(item, "type");
            if (type == NULL || strcmp(type, "function_call") != 0) {
                continue;
            }
            const char *name = pt_rt_json_str(item, "name");
            const char *args = pt_rt_json_str(item, "arguments");
            pt_tool_call_t *call = &out->calls[out->n_calls];
            if (!copy_id(call->call_id, sizeof(call->call_id), pt_rt_json_str(item, "call_id")) || name == NULL) {
                return PT_ERR_PARSE;
            }
            pt_rt_copy_trunc(call->name, sizeof(call->name), name);
            call->arguments = strdup(args != NULL ? args : "");
            if (call->arguments == NULL) {
                return PT_ERR_NOMEM;
            }
            out->n_calls++;
        }
    }
    out->kind = PT_RT_RESPONSE_DONE;
    return PT_OK;
}

static pt_err_t dispatch(const cJSON *msg, pt_rt_event_t *out)
{
    const char *type = pt_rt_json_str(msg, "type");
    if (type == NULL) {
        return PT_OK;
    }
    if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
        out->kind = PT_RT_SPEECH_STARTED;
    } else if (strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        out->kind = PT_RT_SPEECH_STOPPED;
    } else if (strcmp(type, "input_audio_buffer.cleared") == 0) {
        out->kind = PT_RT_INPUT_CLEARED;
    } else if (strcmp(type, "session.created") == 0) {
        return fill_session(msg, out, "created");
    } else if (strcmp(type, "session.updated") == 0) {
        return fill_session(msg, out, "updated");
    } else if (strcmp(type, "response.created") == 0) {
        const cJSON *response = cJSON_GetObjectItemCaseSensitive(msg, "response");
        if (!copy_id(out->response_id, sizeof(out->response_id), pt_rt_json_str(response, "id"))) {
            return PT_ERR_PARSE;
        }
        out->kind = PT_RT_RESPONSE_CREATED;
    } else if (strcmp(type, "response.output_audio.delta") == 0) {
        return fill_audio_delta(msg, out);
    } else if (strcmp(type, "response.output_audio_transcript.done") == 0) {
        return fill_text(msg, out, "transcript", PT_RT_TRANSCRIPT_DONE, true);
    } else if (strcmp(type, "response.done") == 0) {
        return fill_response_done(msg, out);
    } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
        return fill_text(msg, out, "transcript", PT_RT_INPUT_TRANSCRIPT, false);
    } else if (strcmp(type, "error") == 0) {
        return fill_error(msg, out);
    } else {
        return pt_rt_live_dispatch(type, msg, out);
    }
    return PT_OK;
}

pt_err_t pt_rt_parse(const char *json, size_t len, pt_rt_event_t *out)
{
    if (out == NULL) {
        return PT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->kind = PT_RT_IGNORED;
    if (json == NULL || len == 0) {
        return PT_ERR_PARSE;
    }
    cJSON *msg = cJSON_ParseWithLength(json, len);
    if (msg == NULL) {
        return PT_ERR_PARSE;
    }
    pt_err_t rc = cJSON_IsObject(msg) ? dispatch(msg, out) : PT_ERR_PARSE;
    cJSON_Delete(msg);
    if (rc != PT_OK) {
        pt_rt_event_free(out);
        out->kind = PT_RT_IGNORED;
    }
    return rc;
}

void pt_rt_event_free(pt_rt_event_t *e)
{
    if (e == NULL) {
        return;
    }
    free(e->text);
    free(e->pcm);
    if (e->calls != NULL) {
        for (size_t i = 0; i < e->n_calls; i++) {
            free(e->calls[i].arguments);
        }
        free(e->calls);
    }
    memset(e, 0, sizeof(*e));
}
