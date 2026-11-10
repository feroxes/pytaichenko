#include "fake_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "mbedtls/base64.h"

static fp_call_t *add(fake_port_t *f, fp_kind_t kind)
{
    if (f->n == f->cap) {
        size_t cap = f->cap ? f->cap * 2 : 64;
        fp_call_t *calls = realloc(f->calls, cap * sizeof(*calls));
        if (calls == NULL) {
            abort();
        }
        f->calls = calls;
        f->cap = cap;
    }
    fp_call_t *c = &f->calls[f->n++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->at = *f->clock;
    c->conn = f->opened;
    return c;
}

static void *dup_bytes(const void *src, size_t bytes)
{
    void *out = malloc(bytes > 0 ? bytes : 1);
    if (out == NULL) {
        abort();
    }
    if (bytes > 0) {
        memcpy(out, src, bytes);
    }
    return out;
}

static void decode_audio(fp_call_t *c, const char *b64)
{
    size_t len = strlen(b64);
    size_t need = 0;
    mbedtls_base64_decode(NULL, 0, &need, (const unsigned char *)b64, len);
    unsigned char *raw = malloc(need > 0 ? need : 1);
    size_t got = 0;
    if (raw == NULL || mbedtls_base64_decode(raw, need, &got, (const unsigned char *)b64, len) != 0) {
        abort();
    }
    c->n = got / 2;
    c->pcm = malloc(c->n > 0 ? c->n * sizeof(int16_t) : 1);
    if (c->pcm == NULL) {
        abort();
    }
    for (size_t i = 0; i < c->n; i++) {
        c->pcm[i] = (int16_t)(uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
    }
    free(raw);
}

static void on_open(void *ctx, const pt_settings_t *s)
{
    fake_port_t *f = ctx;
    f->opened++;
    if (f->opened > FP_MAX_CONN) {
        abort();
    }
    fp_call_t *c = add(f, FP_RT_OPEN);
    snprintf(c->model, sizeof(c->model), "%s", s->openai.model);
    snprintf(c->voice, sizeof(c->voice), "%s", s->openai.voice);
    c->first_turn_s = s->timing.first_turn_s;
}

static void on_send(void *ctx, const char *json)
{
    fake_port_t *f = ctx;
    fp_call_t *c = add(f, FP_RT_SEND);
    c->json = dup_bytes(json, strlen(json) + 1);
    cJSON *msg = cJSON_Parse(json);
    if (msg == NULL) {
        abort();
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(msg, "type");
    if (cJSON_IsString(type)) {
        snprintf(c->type, sizeof(c->type), "%s", type->valuestring);
    }
    if (strcmp(c->type, "input_audio_buffer.append") == 0) {
        const cJSON *audio = cJSON_GetObjectItemCaseSensitive(msg, "audio");
        if (!cJSON_IsString(audio)) {
            abort();
        }
        decode_audio(c, audio->valuestring);
    }
    if (strcmp(c->type, "session.update") == 0) {
        const cJSON *session = cJSON_GetObjectItemCaseSensitive(msg, "session");
        const cJSON *audio = cJSON_GetObjectItemCaseSensitive(session, "audio");
        const cJSON *output = cJSON_GetObjectItemCaseSensitive(audio, "output");
        const cJSON *voice = cJSON_GetObjectItemCaseSensitive(output, "voice");
        if (cJSON_IsString(voice)) {
            snprintf(c->voice, sizeof(c->voice), "%s", voice->valuestring);
        }
    }
    cJSON_Delete(msg);
}

static void on_close(void *ctx)
{
    fake_port_t *f = ctx;
    add(f, FP_RT_CLOSE);
    if (f->opened > 0) {
        f->conn[f->opened].closed = true;
    }
}

static void on_play(void *ctx, const int16_t *pcm, size_t n)
{
    fake_port_t *f = ctx;
    fp_call_t *c = add(f, FP_PLAY);
    c->pcm = dup_bytes(pcm, n * sizeof(*pcm));
    c->n = n;
}

static void on_play_end(void *ctx)
{
    add(ctx, FP_PLAY_END);
}

static void on_device(void *ctx, pt_dev_event_t ev, bool follow_up, const char *code)
{
    fp_call_t *c = add(ctx, FP_DEVICE);
    c->ev = ev;
    c->follow_up = follow_up;
    c->has_code = code != NULL;
    if (code != NULL) {
        snprintf(c->code, sizeof(c->code), "%s", code);
    }
}

static void on_volume(void *ctx, float volume)
{
    fp_call_t *c = add(ctx, FP_SET_VOLUME);
    c->volume = volume;
}

void fp_init(fake_port_t *f, const uint32_t *clock, const fp_target_t *target)
{
    memset(f, 0, sizeof(*f));
    f->clock = clock;
    f->target = *target;
    f->port.rt_open = on_open;
    f->port.rt_send = on_send;
    f->port.rt_close = on_close;
    f->port.play = on_play;
    f->port.play_end = on_play_end;
    f->port.device = on_device;
    f->port.set_volume = on_volume;
    f->port.ctx = f;
}

void fp_free(fake_port_t *f)
{
    for (size_t i = 0; i < f->n; i++) {
        free(f->calls[i].json);
        free(f->calls[i].pcm);
    }
    free(f->calls);
    f->calls = NULL;
    f->n = f->cap = 0;
}

bool fp_live(const fake_port_t *f, int conn)
{
    return conn > 0 && conn == f->opened && !f->conn[conn].closed;
}

void fp_push(fake_port_t *f, const char *json)
{
    f->target.message(f->target.t, json, strlen(json), *f->clock);
    fp_pump(f);
}

static void push_if_live(fake_port_t *f, int conn, const char *json)
{
    if (fp_live(f, conn)) {
        f->target.message(f->target.t, json, strlen(json), *f->clock);
    }
}

static void created(fake_port_t *f, int conn, const char *model)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_%d\",\"model\":\"%s\"}}", conn,
             model);
    push_if_live(f, conn, json);
}

void fp_connect(fake_port_t *f)
{
    int conn = f->opened;
    const fp_call_t *open = fp_last(f, FP_RT_OPEN);
    if (!fp_live(f, conn)) {
        abort();
    }
    f->target.opened(f->target.t, *f->clock);
    created(f, conn, open != NULL ? open->model : "?");
    fp_pump(f);
}

void fp_pump(fake_port_t *f)
{
    while (f->pumped < f->n) {
        size_t i = f->pumped++;
        fp_call_t c = f->calls[i];
        fp_conn_t *conn = &f->conn[c.conn];
        if (c.kind == FP_RT_OPEN && !conn->manual && fp_live(f, c.conn)) {
            if (conn->fail_connect) {
                f->target.failed(f->target.t, "connect failed", *f->clock);
            } else {
                char model[64];
                snprintf(model, sizeof(model), "%s", c.model);
                f->target.opened(f->target.t, *f->clock);
                created(f, c.conn, model);
            }
        } else if (c.kind == FP_RT_SEND && strcmp(c.type, "session.update") == 0) {
            char json[256];
            if (conn->reject_update) {
                snprintf(json, sizeof(json),
                         "{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"code\":\"invalid_value\","
                         "\"message\":\"bad session\"}}");
            } else {
                snprintf(json, sizeof(json),
                         "{\"type\":\"session.updated\",\"session\":{\"audio\":{\"output\":{\"voice\":\"%s\"}}}}",
                         c.voice);
            }
            push_if_live(f, c.conn, json);
        } else if (c.kind == FP_RT_SEND && strcmp(c.type, "input_audio_buffer.clear") == 0 && !conn->silent) {
            push_if_live(f, c.conn, "{\"type\":\"input_audio_buffer.cleared\"}");
        }
    }
}

size_t fp_count(const fake_port_t *f, fp_kind_t kind)
{
    size_t count = 0;
    for (size_t i = 0; i < f->n; i++) {
        count += f->calls[i].kind == kind;
    }
    return count;
}

size_t fp_count_conn(const fake_port_t *f, fp_kind_t kind, int conn)
{
    size_t count = 0;
    for (size_t i = 0; i < f->n; i++) {
        count += f->calls[i].kind == kind && f->calls[i].conn == conn;
    }
    return count;
}

size_t fp_sends(const fake_port_t *f, const char *type, int conn)
{
    size_t count = 0;
    for (size_t i = 0; i < f->n; i++) {
        const fp_call_t *c = &f->calls[i];
        count += c->kind == FP_RT_SEND && (conn == 0 || c->conn == conn) && strcmp(c->type, type) == 0;
    }
    return count;
}

static const char *short_type(const char *type)
{
    static const char *const map[][2] = {
        {"input_audio_buffer.clear", "clear"},
        {"input_audio_buffer.commit", "commit"},
        {"response.cancel", "cancel"},
        {"response.create", "create"},
        {"conversation.item.create", "item"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(type, map[i][0]) == 0) {
            return map[i][1];
        }
    }
    return NULL;
}

void fp_calls(const fake_port_t *f, int conn, char *out, size_t len)
{
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < f->n; i++) {
        const fp_call_t *c = &f->calls[i];
        const char *name = c->kind == FP_RT_SEND && c->conn == conn ? short_type(c->type) : NULL;
        if (name != NULL) {
            int w = snprintf(out + used, len - used, "%s%s", used ? "," : "", name);
            if (w < 0 || (size_t)w >= len - used) {
                return;
            }
            used += (size_t)w;
        }
    }
}

size_t fp_appended(const fake_port_t *f, int conn, int16_t *out, size_t cap)
{
    size_t total = 0;
    for (size_t i = 0; i < f->n; i++) {
        const fp_call_t *c = &f->calls[i];
        if (c->kind != FP_RT_SEND || c->conn != conn || c->pcm == NULL) {
            continue;
        }
        for (size_t k = 0; k < c->n; k++, total++) {
            if (out != NULL && total < cap) {
                out[total] = c->pcm[k];
            }
        }
    }
    return total;
}

size_t fp_appended_len(const fake_port_t *f, int conn)
{
    return fp_appended(f, conn, NULL, 0);
}

size_t fp_devices(const fake_port_t *f, pt_dev_event_t *out, size_t cap)
{
    size_t count = 0;
    for (size_t i = 0; i < f->n; i++) {
        if (f->calls[i].kind == FP_DEVICE) {
            if (count < cap) {
                out[count] = f->calls[i].ev;
            }
            count++;
        }
    }
    return count;
}

const fp_call_t *fp_nth(const fake_port_t *f, fp_kind_t kind, size_t nth)
{
    size_t seen = 0;
    for (size_t i = 0; i < f->n; i++) {
        if (f->calls[i].kind == kind && seen++ == nth) {
            return &f->calls[i];
        }
    }
    return NULL;
}

const fp_call_t *fp_device_at(const fake_port_t *f, size_t index)
{
    return fp_nth(f, FP_DEVICE, index);
}

const fp_call_t *fp_last(const fake_port_t *f, fp_kind_t kind)
{
    for (size_t i = f->n; i > 0; i--) {
        if (f->calls[i - 1].kind == kind) {
            return &f->calls[i - 1];
        }
    }
    return NULL;
}

size_t fp_played(const fake_port_t *f, int16_t *out, size_t cap)
{
    size_t total = 0;
    for (size_t i = 0; i < f->n; i++) {
        const fp_call_t *c = &f->calls[i];
        if (c->kind != FP_PLAY) {
            continue;
        }
        for (size_t k = 0; k < c->n; k++, total++) {
            if (out != NULL && total < cap) {
                out[total] = c->pcm[k];
            }
        }
    }
    return total;
}

size_t fp_played_before_end(const fake_port_t *f, size_t nth_end)
{
    size_t total = 0;
    size_t ends = 0;
    for (size_t i = 0; i < f->n; i++) {
        const fp_call_t *c = &f->calls[i];
        if (c->kind == FP_PLAY_END) {
            if (ends++ == nth_end) {
                return total;
            }
            total = 0;
        } else if (c->kind == FP_PLAY) {
            total += c->n;
        }
    }
    return total;
}

uint32_t fp_time_of(const fake_port_t *f, pt_dev_event_t ev, size_t nth)
{
    size_t seen = 0;
    for (size_t i = 0; i < f->n; i++) {
        if (f->calls[i].kind == FP_DEVICE && f->calls[i].ev == ev && seen++ == nth) {
            return f->calls[i].at;
        }
    }
    return UINT32_MAX;
}

size_t fp_device_count(const fake_port_t *f, pt_dev_event_t ev)
{
    size_t count = 0;
    for (size_t i = 0; i < f->n; i++) {
        count += f->calls[i].kind == FP_DEVICE && f->calls[i].ev == ev;
    }
    return count;
}

char *fp_json_delta(const char *rid, const int16_t *pcm, size_t n)
{
    unsigned char *raw = malloc(n * 2 + 1);
    if (raw == NULL) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t)pcm[i];
        raw[2 * i] = (unsigned char)(v & 0xff);
        raw[2 * i + 1] = (unsigned char)(v >> 8);
    }
    size_t cap = 4 * ((n * 2 + 2) / 3) + 1;
    unsigned char *b64 = malloc(cap);
    size_t written = 0;
    if (b64 == NULL || mbedtls_base64_encode(b64, cap, &written, raw, n * 2) != 0) {
        abort();
    }
    free(raw);
    size_t len = written + strlen(rid) + 96;
    char *json = malloc(len);
    if (json == NULL) {
        abort();
    }
    snprintf(json, len, "{\"type\":\"response.output_audio.delta\",\"response_id\":\"%s\",\"delta\":\"%s\"}", rid,
             (const char *)b64);
    free(b64);
    return json;
}
