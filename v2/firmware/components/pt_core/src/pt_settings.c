#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pt_settings.h"

#define INSTRUCTIONS_MAX_BYTES 8000
#define MIN_STOP_GRACE_S 0.5
#define STORE_VERSION 1

const float PT_EQ_HZ[PT_EQ_BANDS] = {250.0f, 600.0f, 1500.0f, 3500.0f, 8000.0f};

static const char *const WAKE_WORDS[] = {"hey_jarvis", "okay_nabu", "hey_mycroft"};

typedef enum { RANGE_CLOSED, RANGE_OPEN_LOW } range_t;

typedef struct {
    const char *name;
    size_t offset;
    double low;
    double high;
    range_t kind;
} float_rule_t;

static const float_rule_t TIMING_RULES[] = {
    {"first_turn_s", offsetof(pt_timing_t, first_turn_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"follow_up_s", offsetof(pt_timing_t, follow_up_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"max_speech_s", offsetof(pt_timing_t, max_speech_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"response_timeout_s", offsetof(pt_timing_t, response_timeout_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"answer_max_s", offsetof(pt_timing_t, answer_max_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"session_idle_s", offsetof(pt_timing_t, session_idle_s), 0, FLT_MAX, RANGE_OPEN_LOW},
    {"mic_stop_grace_s", offsetof(pt_timing_t, mic_stop_grace_s), MIN_STOP_GRACE_S, FLT_MAX, RANGE_CLOSED},
    {"mic_holdoff_s", offsetof(pt_timing_t, mic_holdoff_s), 0, FLT_MAX, RANGE_CLOSED},
    {"ready_check_s", offsetof(pt_timing_t, ready_check_s), 0, FLT_MAX, RANGE_OPEN_LOW},
};

static const float_rule_t SOUND_RULES[] = {
    {"volume", offsetof(pt_sound_t, volume), 0, 1, RANGE_CLOSED},
    {"mic_gain_db", offsetof(pt_sound_t, mic_gain_db), 0, 30, RANGE_CLOSED},
    {"low_cut_hz", offsetof(pt_sound_t, low_cut_hz), 80, 400, RANGE_CLOSED},
    {"loudness_gain_db", offsetof(pt_sound_t, loudness_gain_db), 0, 12, RANGE_CLOSED},
};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define EQ_LIMIT_DB 12.0

typedef struct {
    char *buf;
    size_t size;
    bool lenient;
    int dropped;
} fail_t;

static pt_err_t fail(fail_t *f, const char *section, const char *key)
{
    if (f != NULL && f->buf != NULL && f->size > 0) {
        const char *dot = (section[0] != '\0' && key[0] != '\0') ? "." : "";
        snprintf(f->buf, f->size, "%s%s%s", section, dot, key);
    }
    return PT_ERR_INVALID;
}

static bool reject(fail_t *f, const char *section, const char *key)
{
    if (f->lenient) {
        f->dropped++;
        return false;
    }
    fail(f, section, key);
    return true;
}

static bool unknown(fail_t *f, const char *section, const char *key)
{
    if (f->lenient) {
        return false;
    }
    fail(f, section, key);
    return true;
}

static bool streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static bool valid_utf8(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        size_t extra;
        uint32_t cp;
        uint32_t min;
        if (c < 0x80) {
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
            min = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= n) {
            return false;
        }
        for (size_t k = 1; k <= extra; k++) {
            if ((p[i + k] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

static bool is_edge_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool read_text(const cJSON *item, char *dst, size_t size, bool allow_null, bool allow_empty)
{
    if (cJSON_IsNull(item)) {
        if (!allow_null) {
            return false;
        }
        dst[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const char *p = item->valuestring;
    size_t n = strlen(p);
    while (n > 0 && is_edge_space(*p)) {
        p++;
        n--;
    }
    while (n > 0 && is_edge_space(p[n - 1])) {
        n--;
    }
    if (!valid_utf8(p, n)) {
        return false;
    }
    if (n == 0) {
        if (!allow_empty) {
            return false;
        }
        dst[0] = '\0';
        return true;
    }
    if (n >= size) {
        return false;
    }
    memcpy(dst, p, n);
    dst[n] = '\0';
    return true;
}

static bool read_double(const cJSON *item, double *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static bool in_range(double v, double low, double high, range_t kind)
{
    if (kind == RANGE_OPEN_LOW ? v <= low : v < low) {
        return false;
    }
    return v <= high;
}

static bool read_float(const cJSON *item, double low, double high, range_t kind, float *out)
{
    double v;
    if (!read_double(item, &v) || !in_range(v, low, high, kind)) {
        return false;
    }
    *out = (float)v;
    return true;
}

static bool read_int(const cJSON *item, int low, int *out)
{
    double v;
    if (!read_double(item, &v) || v != floor(v) || v < low || v > INT_MAX) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool read_bool(const cJSON *item, bool *out)
{
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *out = cJSON_IsTrue(item);
    return true;
}

static bool apply_float_rules(const float_rule_t *rules, size_t count, void *base, const char *name,
                              const cJSON *item, bool *matched, bool *ok)
{
    for (size_t i = 0; i < count; i++) {
        if (streq(name, rules[i].name)) {
            *matched = true;
            *ok = read_float(item, rules[i].low, rules[i].high, rules[i].kind,
                             (float *)((char *)base + rules[i].offset));
            return true;
        }
    }
    return false;
}

static pt_err_t apply_openai(pt_settings_t *w, const cJSON *obj, bool patch, fail_t *f)
{
    pt_openai_t *o = &w->openai;
    for (const cJSON *it = obj->child; it != NULL; it = it->next) {
        const char *k = it->string;
        bool ok;
        if (streq(k, "api_key")) {
            if (patch && cJSON_IsString(it) && it->valuestring != NULL) {
                char probe[2];
                if (read_text(it, probe, sizeof(probe), false, true) && probe[0] == '\0') {
                    continue;
                }
            }
            ok = read_text(it, o->api_key, sizeof(o->api_key), true, true);
        } else if (streq(k, "model")) {
            ok = read_text(it, o->model, sizeof(o->model), false, false);
        } else if (streq(k, "voice")) {
            ok = read_text(it, o->voice, sizeof(o->voice), false, false);
        } else if (streq(k, "reasoning_effort")) {
            ok = read_text(it, o->reasoning_effort, sizeof(o->reasoning_effort), true, false);
        } else if (streq(k, "vad_threshold")) {
            ok = read_float(it, 0, 1, RANGE_CLOSED, &o->vad_threshold);
        } else if (streq(k, "silence_ms")) {
            ok = read_int(it, 1, &o->silence_ms);
        } else if (streq(k, "log_heard")) {
            ok = read_bool(it, &o->log_heard);
        } else {
            if (unknown(f, "openai", k)) {
                return PT_ERR_INVALID;
            }
            continue;
        }
        if (!ok && reject(f, "openai", k)) {
            return PT_ERR_INVALID;
        }
    }
    return PT_OK;
}

static pt_err_t apply_assistant(pt_settings_t *w, const cJSON *obj, bool patch, fail_t *f)
{
    pt_assistant_t *a = &w->assistant;
    char old_language[sizeof(a->language)];
    memcpy(old_language, a->language, sizeof(old_language));
    bool instructions_given = false;
    bool reset = false;

    for (const cJSON *it = obj->child; it != NULL; it = it->next) {
        const char *k = it->string;
        if (streq(k, "language")) {
            if (!cJSON_IsString(it) || it->valuestring == NULL || pt_default_instructions(it->valuestring) == NULL) {
                if (reject(f, "assistant", k)) {
                    return PT_ERR_INVALID;
                }
                continue;
            }
            snprintf(a->language, sizeof(a->language), "%s", it->valuestring);
        } else if (streq(k, "instructions")) {
            if (patch && cJSON_IsNull(it)) {
                reset = true;
                continue;
            }
            if (!cJSON_IsString(it) || it->valuestring == NULL) {
                if (reject(f, "assistant", k)) {
                    return PT_ERR_INVALID;
                }
                continue;
            }
            const char *p = it->valuestring;
            size_t n = strlen(p);
            size_t first = 0;
            while (first < n && (is_edge_space(p[first]) || p[first] == '\f' || p[first] == '\v')) {
                first++;
            }
            if (first == n || n > INSTRUCTIONS_MAX_BYTES || !valid_utf8(p, n)) {
                if (reject(f, "assistant", k)) {
                    return PT_ERR_INVALID;
                }
                continue;
            }
            memcpy(a->instructions, p, n + 1);
            instructions_given = true;
            reset = false;
        } else if (unknown(f, "assistant", k)) {
            return PT_ERR_INVALID;
        }
    }
    if (reset || (!instructions_given && !streq(old_language, a->language))) {
        snprintf(a->instructions, sizeof(a->instructions), "%s", pt_default_instructions(a->language));
    }
    return PT_OK;
}

static pt_err_t apply_timing(pt_settings_t *w, const cJSON *obj, fail_t *f)
{
    for (const cJSON *it = obj->child; it != NULL; it = it->next) {
        const char *k = it->string;
        bool matched = false;
        bool ok = false;
        if (streq(k, "max_follow_ups")) {
            matched = true;
            ok = read_int(it, 0, &w->timing.max_follow_ups);
        } else {
            apply_float_rules(TIMING_RULES, ARRAY_LEN(TIMING_RULES), &w->timing, k, it, &matched, &ok);
        }
        if (!matched) {
            if (unknown(f, "timing", k)) {
                return PT_ERR_INVALID;
            }
        } else if (!ok && reject(f, "timing", k)) {
            return PT_ERR_INVALID;
        }
    }
    return PT_OK;
}

static pt_err_t apply_sound(pt_settings_t *w, const cJSON *obj, fail_t *f)
{
    pt_sound_t *s = &w->sound;
    for (const cJSON *it = obj->child; it != NULL; it = it->next) {
        const char *k = it->string;
        bool matched = false;
        bool ok = false;
        if (streq(k, "wake_word")) {
            matched = true;
            if (cJSON_IsString(it) && it->valuestring != NULL) {
                for (size_t i = 0; i < ARRAY_LEN(WAKE_WORDS); i++) {
                    if (streq(it->valuestring, WAKE_WORDS[i])) {
                        snprintf(s->wake_word, sizeof(s->wake_word), "%s", WAKE_WORDS[i]);
                        ok = true;
                        break;
                    }
                }
            }
        } else if (streq(k, "eq_db")) {
            matched = true;
            ok = cJSON_IsArray(it) && cJSON_GetArraySize(it) == PT_EQ_BANDS;
            float bands[PT_EQ_BANDS];
            int i = 0;
            for (const cJSON *e = ok ? it->child : NULL; e != NULL && ok; e = e->next, i++) {
                ok = read_float(e, -EQ_LIMIT_DB, EQ_LIMIT_DB, RANGE_CLOSED, &bands[i]);
            }
            if (ok) {
                memcpy(s->eq_db, bands, sizeof(bands));
            }
        } else {
            apply_float_rules(SOUND_RULES, ARRAY_LEN(SOUND_RULES), s, k, it, &matched, &ok);
        }
        if (!matched) {
            if (unknown(f, "sound", k)) {
                return PT_ERR_INVALID;
            }
        } else if (!ok && reject(f, "sound", k)) {
            return PT_ERR_INVALID;
        }
    }
    return PT_OK;
}

static pt_err_t apply_root(pt_settings_t *w, const cJSON *root, bool patch, fail_t *f)
{
    if (!cJSON_IsObject(root)) {
        return fail(f, "settings", "");
    }
    for (const cJSON *it = root->child; it != NULL; it = it->next) {
        const char *k = it->string;
        pt_err_t e;
        if (streq(k, "openai") || streq(k, "assistant") || streq(k, "timing") || streq(k, "sound")) {
            if (!cJSON_IsObject(it)) {
                if (reject(f, k, "")) {
                    return PT_ERR_INVALID;
                }
                continue;
            }
            if (streq(k, "openai")) {
                e = apply_openai(w, it, patch, f);
            } else if (streq(k, "assistant")) {
                e = apply_assistant(w, it, patch, f);
            } else if (streq(k, "timing")) {
                e = apply_timing(w, it, f);
            } else {
                e = apply_sound(w, it, f);
            }
            if (e != PT_OK) {
                return e;
            }
        } else if (!patch && streq(k, "version")) {
            if (!f->lenient && (!cJSON_IsNumber(it) || it->valuedouble != STORE_VERSION)) {
                return fail(f, k, "");
            }
        } else if (!patch && streq(k, "password_hash")) {
            if (!read_text(it, w->password_hash, sizeof(w->password_hash), true, true) && reject(f, k, "")) {
                return PT_ERR_INVALID;
            }
        } else if (unknown(f, k, "")) {
            return PT_ERR_INVALID;
        }
    }
    return PT_OK;
}

static pt_err_t run_document(pt_settings_t *s, const char *json, size_t len, bool patch, bool from_defaults,
                             char *field, size_t field_len, bool lenient, int *dropped)
{
    fail_t f = {field, field_len, lenient, 0};
    if (dropped != NULL) {
        *dropped = 0;
    }
    if (field != NULL && field_len > 0) {
        field[0] = '\0';
    }
    if (s == NULL || json == NULL) {
        return PT_ERR_INVALID;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &end, 0);
    if (root == NULL) {
        return PT_ERR_PARSE;
    }
    while (end != NULL && end < json + len && is_edge_space(*end)) {
        end++;
    }
    if (end == NULL || end != json + len) {
        cJSON_Delete(root);
        return PT_ERR_PARSE;
    }
    pt_settings_t *w = malloc(sizeof(*w));
    if (w == NULL) {
        cJSON_Delete(root);
        return PT_ERR_NOMEM;
    }
    if (from_defaults) {
        pt_settings_defaults(w, "uk");
    } else {
        memcpy(w, s, sizeof(*w));
    }
    pt_err_t e = apply_root(w, root, patch, &f);
    if (e == PT_OK) {
        memcpy(s, w, sizeof(*s));
        if (dropped != NULL) {
            *dropped = f.dropped;
        }
    }
    free(w);
    cJSON_Delete(root);
    return e;
}

void pt_settings_defaults(pt_settings_t *s, const char *language)
{
    const char *instructions = pt_default_instructions(language);
    if (instructions == NULL) {
        language = "uk";
        instructions = pt_default_instructions("uk");
    }
    memset(s, 0, sizeof(*s));
    snprintf(s->openai.model, sizeof(s->openai.model), "gpt-realtime-2.1-mini");
    snprintf(s->openai.voice, sizeof(s->openai.voice), "marin");
    snprintf(s->openai.reasoning_effort, sizeof(s->openai.reasoning_effort), "low");
    s->openai.vad_threshold = 0.5f;
    s->openai.silence_ms = 600;
    s->openai.log_heard = false;
    snprintf(s->assistant.language, sizeof(s->assistant.language), "%s", language);
    snprintf(s->assistant.instructions, sizeof(s->assistant.instructions), "%s", instructions);
    s->timing.first_turn_s = 8.0f;
    s->timing.follow_up_s = 10.0f;
    s->timing.max_speech_s = 20.0f;
    s->timing.response_timeout_s = 30.0f;
    s->timing.answer_max_s = 180.0f;
    s->timing.session_idle_s = 300.0f;
    s->timing.mic_stop_grace_s = 0.5f;
    s->timing.mic_holdoff_s = 0.4f;
    s->timing.ready_check_s = 2.0f;
    s->timing.max_follow_ups = 20;
    s->sound.volume = 0.7f;
    snprintf(s->sound.wake_word, sizeof(s->sound.wake_word), "hey_jarvis");
    s->sound.mic_gain_db = 12.0f;
    s->sound.low_cut_hz = 200.0f;
    s->sound.loudness_gain_db = 6.0f;
}

pt_err_t pt_settings_from_json(pt_settings_t *s, const char *json, size_t len)
{
    return run_document(s, json, len, false, true, NULL, 0, false, NULL);
}

pt_err_t pt_settings_load_stored(pt_settings_t *s, const char *json, size_t len, int *dropped)
{
    return run_document(s, json, len, false, true, NULL, 0, true, dropped);
}

pt_err_t pt_settings_patch(pt_settings_t *s, const char *json, size_t len, char *field, size_t field_len)
{
    return run_document(s, json, len, true, false, field, field_len, false, NULL);
}

static cJSON *number_item(float v)
{
    char text[40];
    snprintf(text, sizeof(text), "%.7g", (double)v);
    if ((float)strtod(text, NULL) != v) {
        snprintf(text, sizeof(text), "%.9g", (double)v);
    }
    return cJSON_CreateNumber(strtod(text, NULL));
}

static bool add_text(cJSON *obj, const char *key, const char *value, bool empty_is_null)
{
    cJSON *item = (empty_is_null && value[0] == '\0') ? cJSON_CreateNull() : cJSON_CreateString(value);
    return item != NULL && cJSON_AddItemToObject(obj, key, item);
}

static bool add_float(cJSON *obj, const char *key, float v)
{
    cJSON *item = number_item(v);
    return item != NULL && cJSON_AddItemToObject(obj, key, item);
}

static bool add_int(cJSON *obj, const char *key, int v)
{
    cJSON *item = cJSON_CreateNumber((double)v);
    return item != NULL && cJSON_AddItemToObject(obj, key, item);
}

static bool add_bool(cJSON *obj, const char *key, bool v)
{
    cJSON *item = cJSON_CreateBool(v);
    return item != NULL && cJSON_AddItemToObject(obj, key, item);
}

static cJSON *add_object(cJSON *parent, const char *key)
{
    cJSON *obj = cJSON_CreateObject();
    if (obj != NULL && !cJSON_AddItemToObject(parent, key, obj)) {
        cJSON_Delete(obj);
        return NULL;
    }
    return obj;
}

static bool build_sections(cJSON *root, const pt_settings_t *s, bool secrets)
{
    bool ok = true;
    cJSON *o = add_object(root, "openai");
    cJSON *a = add_object(root, "assistant");
    cJSON *t = add_object(root, "timing");
    cJSON *u = add_object(root, "sound");
    if (o == NULL || a == NULL || t == NULL || u == NULL) {
        return false;
    }
    if (secrets) {
        ok &= add_text(o, "api_key", s->openai.api_key, true);
    } else {
        ok &= add_bool(o, "api_key_set", s->openai.api_key[0] != '\0');
    }
    ok &= add_text(o, "model", s->openai.model, false);
    ok &= add_text(o, "voice", s->openai.voice, false);
    ok &= add_text(o, "reasoning_effort", s->openai.reasoning_effort, true);
    ok &= add_float(o, "vad_threshold", s->openai.vad_threshold);
    ok &= add_int(o, "silence_ms", s->openai.silence_ms);
    ok &= add_bool(o, "log_heard", s->openai.log_heard);
    ok &= add_text(a, "language", s->assistant.language, false);
    ok &= add_text(a, "instructions", s->assistant.instructions, false);
    for (size_t i = 0; i < ARRAY_LEN(TIMING_RULES); i++) {
        ok &= add_float(t, TIMING_RULES[i].name, *(const float *)((const char *)&s->timing + TIMING_RULES[i].offset));
    }
    ok &= add_int(t, "max_follow_ups", s->timing.max_follow_ups);
    ok &= add_float(u, "volume", s->sound.volume);
    ok &= add_text(u, "wake_word", s->sound.wake_word, false);
    ok &= add_float(u, "mic_gain_db", s->sound.mic_gain_db);
    ok &= add_float(u, "low_cut_hz", s->sound.low_cut_hz);
    ok &= add_float(u, "loudness_gain_db", s->sound.loudness_gain_db);
    cJSON *eq = cJSON_CreateArray();
    if (eq == NULL || !cJSON_AddItemToObject(u, "eq_db", eq)) {
        cJSON_Delete(eq);
        return false;
    }
    for (int i = 0; i < PT_EQ_BANDS; i++) {
        cJSON *band = number_item(s->sound.eq_db[i]);
        ok &= band != NULL && cJSON_AddItemToArray(eq, band);
    }
    return ok;
}

static char *render(const pt_settings_t *s, bool secrets)
{
    if (s == NULL) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    bool ok = true;
    if (secrets) {
        ok &= add_int(root, "version", STORE_VERSION);
    }
    ok &= build_sections(root, s, secrets);
    if (secrets) {
        ok &= add_text(root, "password_hash", s->password_hash, true);
    } else {
        ok &= add_bool(root, "password_set", s->password_hash[0] != '\0');
    }
    char *out = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return out;
}

char *pt_settings_to_json(const pt_settings_t *s)
{
    return render(s, true);
}

char *pt_settings_public_json(const pt_settings_t *s)
{
    return render(s, false);
}

bool pt_settings_session_changed(const pt_settings_t *a, const pt_settings_t *b)
{
    const pt_openai_t *x = &a->openai;
    const pt_openai_t *y = &b->openai;
    if (!streq(x->api_key, y->api_key) || !streq(x->model, y->model) || !streq(x->voice, y->voice) ||
        !streq(x->reasoning_effort, y->reasoning_effort) || x->vad_threshold != y->vad_threshold ||
        x->silence_ms != y->silence_ms || x->log_heard != y->log_heard) {
        return true;
    }
    if (!streq(a->assistant.language, b->assistant.language) ||
        !streq(a->assistant.instructions, b->assistant.instructions)) {
        return true;
    }
    for (size_t i = 0; i < ARRAY_LEN(TIMING_RULES); i++) {
        float p = *(const float *)((const char *)&a->timing + TIMING_RULES[i].offset);
        float q = *(const float *)((const char *)&b->timing + TIMING_RULES[i].offset);
        if (p != q) {
            return true;
        }
    }
    return a->timing.max_follow_ups != b->timing.max_follow_ups;
}
