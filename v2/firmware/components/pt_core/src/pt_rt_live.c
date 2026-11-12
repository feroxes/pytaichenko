#include <stdlib.h>
#include <string.h>

#include "pt_rt_internal.h"

#define LIVE_SAMPLE_RATE 24000
#define LIVE_ACK_DELAY_MS 800
#define LIVE_PREFIX "gpt-live"
#define HELPER_ERROR "responses_validation_failed"

const char PT_LIVE_SUFFIX[] =
    "Volume changes and ending the conversation are carried out by your helper. Wherever these instructions say to "
    "change the volume or to call end_conversation, delegate that request to the helper instead. Never mention the "
    "helper and never say that you are passing a request on or checking something. After a volume change say only a "
    "one-word confirmation in the language of the conversation. Answer every other question yourself, without "
    "delegating.";

static const char HELPER_INSTRUCTIONS[] =
    "You carry out actions for a home voice speaker. Call set_volume for any request about the volume. Call "
    "end_conversation when the person says goodbye, says that is all, asks to stop, or when people are talking to each "
    "other rather than to the speaker. After a tool result reply with at most two words in the language of the "
    "conversation.";

static const char MSG_CLOSE[] = "{\"type\":\"session.close\"}";
static const char MSG_RESPONSE_CREATE[] = "{\"type\":\"response.create\"}";

bool pt_rt_is_live(const char *model)
{
    return model != NULL && strncmp(model, LIVE_PREFIX, sizeof(LIVE_PREFIX) - 1) == 0;
}

static cJSON *audio_section(const pt_settings_t *s)
{
    cJSON *audio = cJSON_CreateObject();
    cJSON *format = cJSON_CreateObject();
    cJSON *output = cJSON_CreateObject();
    bool ok = audio != NULL && format != NULL && output != NULL;
    ok = ok && pt_rt_add_str(format, "type", "audio/pcm");
    ok = ok && cJSON_AddNumberToObject(format, "rate", LIVE_SAMPLE_RATE) != NULL;
    ok = pt_rt_attach(audio, "format", format) && ok;
    ok = ok && pt_rt_add_str(output, "voice", s->openai.voice);
    ok = pt_rt_attach(audio, "output", output) && ok;
    if (!ok) {
        cJSON_Delete(audio);
        return NULL;
    }
    return audio;
}

static const char *helper_effort(const char *effort)
{
    return strcmp(effort, "minimal") == 0 ? "none" : effort;
}

static cJSON *delegation(const pt_settings_t *s)
{
    cJSON *tools = pt_tools_object(s->assistant.language);
    cJSON *root = cJSON_CreateObject();
    cJSON *responses = cJSON_CreateObject();
    bool ok = tools != NULL && root != NULL && responses != NULL;
    ok = ok && pt_rt_add_str(root, "type", "responses");
    ok = ok && cJSON_AddNumberToObject(root, "acknowledgement_delay_ms", LIVE_ACK_DELAY_MS) != NULL;
    ok = ok && pt_rt_add_str(responses, "model", PT_LIVE_HELPER_MODEL);
    ok = ok && pt_rt_add_str(responses, "instructions", HELPER_INSTRUCTIONS);
    if (ok && s->openai.reasoning_effort[0] != '\0') {
        cJSON *reasoning = cJSON_CreateObject();
        bool r_ok = reasoning != NULL && pt_rt_add_str(reasoning, "effort", helper_effort(s->openai.reasoning_effort));
        ok = pt_rt_attach(responses, "reasoning", reasoning) && r_ok && ok;
    }
    ok = ok && cJSON_AddFalseToObject(responses, "parallel_tool_calls") != NULL;
    if (ok) {
        ok = pt_rt_attach(responses, "tools", tools);
    } else {
        cJSON_Delete(tools);
    }
    ok = pt_rt_attach(root, "responses", responses) && ok;
    if (!ok) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static cJSON *history_item(const pt_rt_msg_t *msg)
{
    bool user = msg->role != NULL && strcmp(msg->role, "user") == 0;
    cJSON *item = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    if (item == NULL || content == NULL || part == NULL || !cJSON_AddItemToArray(content, part)) {
        cJSON_Delete(item);
        cJSON_Delete(content);
        cJSON_Delete(part);
        return NULL;
    }
    bool ok = pt_rt_add_str(part, "type", user ? "input_text" : "output_text");
    ok = ok && pt_rt_add_str(part, "text", msg->text != NULL ? msg->text : "");
    ok = ok && pt_rt_add_str(item, "type", "message");
    ok = ok && pt_rt_add_str(item, "role", user ? "user" : "assistant");
    if (!pt_rt_attach(item, "content", content) || !ok) {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

static cJSON *history_input(const pt_rt_msg_t *history, size_t n)
{
    cJSON *input = cJSON_CreateArray();
    for (size_t i = 0; input != NULL && i < n; i++) {
        cJSON *item = history_item(&history[i]);
        if (item == NULL || !cJSON_AddItemToArray(input, item)) {
            cJSON_Delete(item);
            cJSON_Delete(input);
            return NULL;
        }
    }
    return input;
}

char *pt_rt_live_start(const pt_settings_t *s, const pt_rt_msg_t *history, size_t n, bool with_helper)
{
    if (s == NULL || (history == NULL && n > 0)) {
        return NULL;
    }
    size_t base = strlen(s->assistant.instructions);
    char *instructions = malloc(base + 2 + sizeof(PT_LIVE_SUFFIX));
    if (instructions == NULL) {
        return NULL;
    }
    memcpy(instructions, s->assistant.instructions, base);
    memcpy(instructions + base, "\n\n", 2);
    memcpy(instructions + base + 2, PT_LIVE_SUFFIX, sizeof(PT_LIVE_SUFFIX));
    cJSON *root = cJSON_CreateObject();
    cJSON *session = cJSON_CreateObject();
    bool ok = root != NULL && session != NULL;
    ok = ok && pt_rt_add_str(root, "type", "session.start");
    ok = ok && pt_rt_add_str(session, "model", s->openai.model);
    ok = ok && pt_rt_add_str(session, "instructions", instructions);
    ok = pt_rt_attach(session, "audio", ok ? audio_section(s) : NULL) && ok;
    if (ok && with_helper) {
        ok = pt_rt_attach(session, "delegation", delegation(s));
    }
    if (ok && n > 0) {
        ok = pt_rt_attach(session, "input", history_input(history, n));
    }
    ok = pt_rt_attach(root, "session", session) && ok;
    free(instructions);
    char *text = NULL;
    if (ok) {
        text = cJSON_PrintUnformatted(root);
    }
    cJSON_Delete(root);
    return text;
}

char *pt_rt_live_append(const int16_t *pcm, size_t n)
{
    return pt_rt_audio_message("session.input_audio.append", pcm, n);
}

bool pt_rt_live_helper_error(const pt_rt_event_t *e)
{
    if (e == NULL || e->kind != PT_RT_ERROR || e->text == NULL) {
        return false;
    }
    size_t n = sizeof(HELPER_ERROR) - 1;
    return strncmp(e->text, HELPER_ERROR, n) == 0 && (e->text[n] == '\0' || e->text[n] == ':');
}

const char *pt_rt_live_close(void)
{
    return MSG_CLOSE;
}

void pt_rt_live_tool_output(const pt_tool_call_t *call, const char *output, bool respond, pt_rt_send_fn send, void *ctx)
{
    if (call == NULL || send == NULL) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *item = cJSON_CreateObject();
    bool ok = root != NULL && item != NULL;
    ok = ok && pt_rt_add_str(root, "type", "response.item.create");
    ok = ok && pt_rt_add_str(item, "type", "function_call_output");
    ok = ok && pt_rt_add_str(item, "call_id", call->call_id);
    ok = ok && pt_rt_add_str(item, "output", output != NULL ? output : "");
    ok = pt_rt_attach(root, "item", item) && ok;
    char *text = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (text == NULL) {
        return;
    }
    send(ctx, text);
    free(text);
    if (respond) {
        send(ctx, MSG_RESPONSE_CREATE);
    }
}

static pt_err_t fill_delta(const cJSON *msg, pt_rt_event_t *out, pt_rt_kind_t kind)
{
    const char *delta = pt_rt_json_str(msg, "delta");
    if (delta == NULL) {
        return PT_ERR_PARSE;
    }
    out->text = strdup(delta);
    if (out->text == NULL) {
        return PT_ERR_NOMEM;
    }
    out->kind = kind;
    return PT_OK;
}

static pt_err_t fill_started(const cJSON *msg, pt_rt_event_t *out)
{
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(msg, "session");
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive(session, "audio");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(audio, "output");
    const char *model = pt_rt_json_str(session, "model");
    const char *voice = pt_rt_json_str(output, "voice");
    out->kind = PT_RT_SESSION_READY;
    pt_rt_copy_trunc(out->status, sizeof(out->status), "started");
    if (model != NULL) {
        pt_rt_copy_trunc(out->model, sizeof(out->model), model);
    }
    if (voice != NULL) {
        pt_rt_copy_trunc(out->voice, sizeof(out->voice), voice);
    }
    return PT_OK;
}

static pt_err_t fill_response_event(const cJSON *msg, pt_rt_event_t *out)
{
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(msg, "event");
    const char *type = pt_rt_json_str(event, "type");
    if (type == NULL || strcmp(type, "response.output_item.done") != 0) {
        return PT_OK;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(event, "item");
    const char *item_type = pt_rt_json_str(item, "type");
    if (item_type == NULL || strcmp(item_type, "function_call") != 0) {
        return PT_OK;
    }
    const char *call_id = pt_rt_json_str(item, "call_id");
    const char *name = pt_rt_json_str(item, "name");
    const char *args = pt_rt_json_str(item, "arguments");
    if (call_id == NULL || strlen(call_id) >= sizeof(out->calls->call_id) || name == NULL) {
        return PT_ERR_PARSE;
    }
    out->calls = calloc(1, sizeof(*out->calls));
    if (out->calls == NULL) {
        return PT_ERR_NOMEM;
    }
    strcpy(out->calls[0].call_id, call_id);
    pt_rt_copy_trunc(out->calls[0].name, sizeof(out->calls[0].name), name);
    out->calls[0].arguments = strdup(args != NULL ? args : "");
    if (out->calls[0].arguments == NULL) {
        return PT_ERR_NOMEM;
    }
    out->n_calls = 1;
    out->kind = PT_RT_TOOL_CALL;
    return PT_OK;
}

pt_err_t pt_rt_live_dispatch(const char *type, const cJSON *msg, pt_rt_event_t *out)
{
    if (strcmp(type, "session.started") == 0) {
        return fill_started(msg, out);
    }
    if (strcmp(type, "session.output_audio.delta") == 0) {
        pt_err_t rc = pt_rt_decode_pcm(pt_rt_json_str(msg, "delta"), out);
        if (rc == PT_OK) {
            out->kind = PT_RT_LIVE_AUDIO;
        }
        return rc;
    }
    if (strcmp(type, "session.input_transcript.delta") == 0) {
        return fill_delta(msg, out, PT_RT_INPUT_DELTA);
    }
    if (strcmp(type, "session.output_transcript.delta") == 0) {
        return fill_delta(msg, out, PT_RT_OUTPUT_DELTA);
    }
    if (strcmp(type, "response.event") == 0) {
        return fill_response_event(msg, out);
    }
    if (strcmp(type, "session.closed") == 0) {
        const char *reason = pt_rt_json_str(msg, "reason");
        out->kind = PT_RT_LIVE_CLOSED;
        pt_rt_copy_trunc(out->status, sizeof(out->status), reason != NULL ? reason : "");
        return PT_OK;
    }
    return PT_OK;
}
