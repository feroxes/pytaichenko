#include "pt_api.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define COOKIE_NAME "pytaichenko_session"
#define FAILED_DELAY_MS 1000u
#define MAX_BODY 16384u
#define MAX_PASSWORD_BYTES 256u
#define MIN_PASSWORD_CHARS 8u
#define MAX_JSON_DEPTH 8
#define HOST_MAX 255

struct pt_api {
    pt_settings_t *settings;
    pt_log_t *log;
    pt_api_env_t env;
    pt_logins_t *logins;
    pt_throttle_t *throttle;
    bool testing;
    bool uploading;
    size_t upload_total;
    size_t upload_got;
};

typedef struct {
    pt_settings_t old;
    pt_settings_t next;
} edit_t;

static void upload_close(pt_api_t *a);

typedef void (*handler_fn)(pt_api_t *, const pt_http_req_t *, pt_http_resp_t *);

typedef struct {
    const char *path;
    const char *method;
    handler_fn fn;
} route_t;

static bool ci_equal(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static bool ci_prefix(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    return strlen(s) >= n && ci_equal(s, prefix, n);
}

static void fail(pt_http_resp_t *resp, int status, const char *code, const char *fmt, ...)
{
    char message[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    resp->status = status;
    free(resp->body);
    resp->body = NULL;
    cJSON *j = cJSON_CreateObject();
    if (j != NULL && cJSON_AddStringToObject(j, "error", code) != NULL &&
        cJSON_AddStringToObject(j, "message", message) != NULL) {
        resp->body = cJSON_PrintUnformatted(j);
    }
    cJSON_Delete(j);
}

static void answer(pt_http_resp_t *resp, int status, cJSON *j)
{
    char *text = j != NULL ? cJSON_PrintUnformatted(j) : NULL;
    cJSON_Delete(j);
    if (text == NULL) {
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    resp->status = status;
    free(resp->body);
    resp->body = text;
}

static bool is_ipv4(const char *s, size_t n)
{
    size_t i = 0;
    for (int group = 0; group < 4; group++) {
        size_t digits = 0;
        unsigned value = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 4) {
            value = value * 10 + (unsigned)(s[i] - '0');
            digits++;
            i++;
        }
        if (digits == 0 || digits > 3 || value > 255) {
            return false;
        }
        if (group < 3) {
            if (i >= n || s[i] != '.') {
                return false;
            }
            i++;
        }
    }
    return i == n;
}

static bool host_allowed(const pt_api_t *a, const char *host)
{
    if (host == NULL || host[0] == '\0') {
        return false;
    }
    size_t len = strlen(host);
    if (len > HOST_MAX) {
        return false;
    }
    size_t name_len = len;
    const char *colon = strchr(host, ':');
    if (colon != NULL) {
        if (strchr(colon + 1, ':') != NULL || strcmp(colon + 1, "80") != 0) {
            return false;
        }
        name_len = (size_t)(colon - host);
    }
    if (is_ipv4(host, name_len)) {
        return true;
    }
    size_t device_len = strlen(a->env.device_name);
    return name_len == device_len + 6 && ci_equal(host, a->env.device_name, device_len) &&
           ci_equal(host + device_len, ".local", 6);
}

static bool same_origin(const char *origin, const char *host)
{
    if (host == NULL) {
        return false;
    }
    const char *p;
    if (ci_prefix(origin, "http://")) {
        p = origin + 7;
    } else if (ci_prefix(origin, "https://")) {
        p = origin + 8;
    } else {
        return false;
    }
    size_t n = strcspn(p, "/?#");
    return n > 0 && n == strlen(host) && ci_equal(p, host, n);
}

static bool json_content_type(const char *type)
{
    if (type == NULL) {
        return false;
    }
    while (*type == ' ' || *type == '\t') {
        type++;
    }
    size_t n = strcspn(type, "; \t");
    return n == strlen("application/json") && ci_equal(type, "application/json", n);
}

static bool octet_content_type(const char *type)
{
    if (type == NULL) {
        return false;
    }
    while (*type == ' ' || *type == '\t') {
        type++;
    }
    size_t n = strcspn(type, "; \t");
    return n == strlen("application/octet-stream") && ci_equal(type, "application/octet-stream", n);
}

static bool session_token(const pt_http_req_t *req, char *out, size_t out_len)
{
    const char *p = req->cookie;
    const size_t name_len = strlen(COOKIE_NAME);
    while (p != NULL && *p != '\0') {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        size_t end = strcspn(p, ";");
        if (end > name_len && strncmp(p, COOKIE_NAME, name_len) == 0 && p[name_len] == '=') {
            size_t n = end - name_len - 1;
            while (n > 0 && p[name_len + n] == ' ') {
                n--;
            }
            if (n == 0 || n >= out_len) {
                return false;
            }
            memcpy(out, p + name_len + 1, n);
            out[n] = '\0';
            return true;
        }
        p += end;
    }
    return false;
}

static bool logged_in(pt_api_t *a, const pt_http_req_t *req)
{
    char token[PT_LOGIN_TOKEN_MAX];
    return session_token(req, token, sizeof(token)) && pt_logins_valid(a->logins, token, a->env.now_s(a->env.ctx));
}

static bool password_set(const pt_api_t *a)
{
    return a->settings->password_hash[0] != '\0';
}

static void log_line(pt_api_t *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void log_line(pt_api_t *a, const char *fmt, ...)
{
    if (a->log == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    pt_log_vadd(a->log, a->env.now_s(a->env.ctx), fmt, ap);
    va_end(ap);
}

static bool depth_ok(const char *s, size_t n)
{
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (in_string) {
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '[' || c == '{') {
            if (++depth > MAX_JSON_DEPTH) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            depth--;
        }
    }
    return true;
}

static cJSON *parse_json(const pt_http_req_t *req)
{
    if (req->body == NULL || req->body_len == 0 || !depth_ok(req->body, req->body_len)) {
        return NULL;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(req->body, req->body_len, &end, 0);
    if (root == NULL) {
        return NULL;
    }
    while (end != NULL && end < req->body + req->body_len && isspace((unsigned char)*end)) {
        end++;
    }
    if (end == NULL || end != req->body + req->body_len) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static cJSON *parse_object(const pt_http_req_t *req, pt_http_resp_t *resp, const char *const *allowed)
{
    cJSON *root = parse_json(req);
    if (root == NULL) {
        fail(resp, 400, "bad_json", "the body is not valid JSON");
        return NULL;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(resp, 400, "bad_json", "the body must be a JSON object");
        return NULL;
    }
    if (allowed != NULL) {
        for (const cJSON *it = root->child; it != NULL; it = it->next) {
            bool known = false;
            for (size_t i = 0; allowed[i] != NULL; i++) {
                known = known || strcmp(it->string, allowed[i]) == 0;
            }
            if (!known) {
                fail(resp, 400, "invalid", "%.60s: unknown field", it->string);
                cJSON_Delete(root);
                return NULL;
            }
        }
    }
    return root;
}

static const char *text_of(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

static long utf8_chars(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    long count = 0;
    while (*p != '\0') {
        size_t extra;
        unsigned cp;
        if (p[0] < 0x80) {
            extra = 0;
            cp = p[0];
        } else if (p[0] >= 0xC2 && p[0] <= 0xDF) {
            extra = 1;
            cp = p[0] & 0x1Fu;
        } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
            extra = 2;
            cp = p[0] & 0x0Fu;
        } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
            extra = 3;
            cp = p[0] & 0x07u;
        } else {
            return -1;
        }
        for (size_t i = 1; i <= extra; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                return -1;
            }
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        if ((extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))) {
            return -1;
        }
        p += extra + 1;
        count++;
    }
    return count;
}

static const char *new_password(const cJSON *obj, const char *key, pt_http_resp_t *resp)
{
    const char *pw = text_of(obj, key);
    long chars = pw != NULL ? utf8_chars(pw) : -1;
    if (pw == NULL || chars < (long)MIN_PASSWORD_CHARS || strlen(pw) > MAX_PASSWORD_BYTES) {
        fail(resp, 400, "bad_password", "%s: must be %u to %u characters", key, MIN_PASSWORD_CHARS,
             MAX_PASSWORD_BYTES);
        return NULL;
    }
    return pw;
}

static const char *given_password(const cJSON *obj, const char *key, pt_http_resp_t *resp)
{
    const char *pw = text_of(obj, key);
    if (pw == NULL || strlen(pw) > MAX_PASSWORD_BYTES) {
        fail(resp, 400, "invalid", "%s: must be text of at most %u bytes", key, MAX_PASSWORD_BYTES);
        return NULL;
    }
    return pw;
}

static cJSON *state_view(const pt_api_t *a, bool in)
{
    cJSON *j = cJSON_CreateObject();
    if (j == NULL || cJSON_AddBoolToObject(j, "password_set", password_set(a)) == NULL ||
        cJSON_AddBoolToObject(j, "logged_in", in) == NULL) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}

static void start_session(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    char old[PT_LOGIN_TOKEN_MAX];
    if (session_token(req, old, sizeof(old))) {
        pt_logins_drop(a->logins, old);
    }
    char token[PT_LOGIN_TOKEN_MAX];
    if (pt_logins_create(a->logins, a->env.now_s(a->env.ctx), token, sizeof(token)) != PT_OK) {
        fail(resp, 500, "internal", "could not start a session");
        return;
    }
    snprintf(resp->set_cookie, sizeof(resp->set_cookie), "%s=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%u",
             COOKIE_NAME, token, (unsigned)PT_LOGIN_TTL_S);
    answer(resp, 200, state_view(a, true));
}

static edit_t *edit_begin(const pt_api_t *a)
{
    edit_t *e = malloc(sizeof(*e));
    if (e != NULL) {
        memcpy(&e->old, a->settings, sizeof(e->old));
        memcpy(&e->next, a->settings, sizeof(e->next));
    }
    return e;
}

static pt_err_t edit_commit(pt_api_t *a, edit_t *e, bool notify)
{
    pt_err_t err = a->env.save(a->env.ctx, &e->next);
    if (err != PT_OK) {
        return err;
    }
    memcpy(a->settings, &e->next, sizeof(*a->settings));
    if (notify && a->env.apply != NULL) {
        a->env.apply(a->env.ctx, &e->old, a->settings);
    }
    return PT_OK;
}

static void client_text(const pt_http_req_t *req, char *out, size_t n)
{
    snprintf(out, n, "%u.%u.%u.%u", (unsigned)((req->client_ip >> 24) & 0xFFu),
             (unsigned)((req->client_ip >> 16) & 0xFFu), (unsigned)((req->client_ip >> 8) & 0xFFu),
             (unsigned)(req->client_ip & 0xFFu));
}

typedef enum { CHECK_OK, CHECK_WRONG, CHECK_BUSY } check_t;

static check_t check_password(pt_api_t *a, const pt_http_req_t *req, const char *given)
{
    if (pt_throttle_enter(a->throttle, req->client_ip) != PT_OK) {
        return CHECK_BUSY;
    }
    bool ok = pt_password_verify(given, a->settings->password_hash);
    uint32_t delay = pt_throttle_check_done(a->throttle, ok);
    if (!ok) {
        char who[20];
        client_text(req, who, sizeof(who));
        log_line(a, "wrong password from %s", who);
    }
    if (delay > 0 && a->env.sleep_ms != NULL) {
        a->env.sleep_ms(a->env.ctx, delay);
    }
    pt_throttle_leave(a->throttle, req->client_ip);
    return ok ? CHECK_OK : CHECK_WRONG;
}

static void too_many_checks(pt_http_resp_t *resp)
{
    fail(resp, 429, "slow_down", "too many password checks from this address at once; wait a few seconds");
}

static void h_state(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    answer(resp, 200, state_view(a, logged_in(a, req)));
}

static void h_setup(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {"password", "language", NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    edit_t *e = NULL;
    const char *language = NULL;
    if (cJSON_GetObjectItemCaseSensitive(body, "language") != NULL) {
        language = text_of(body, "language");
        if (language == NULL || pt_default_instructions(language) == NULL) {
            fail(resp, 400, "invalid", "language: must be one of uk, en");
            goto done;
        }
    }
    if (password_set(a)) {
        fail(resp, 409, "password_set", "a password is already set");
        goto done;
    }
    const char *pw = new_password(body, "password", resp);
    if (pw == NULL) {
        goto done;
    }
    char hash[PT_PASSWORD_HASH_MAX];
    if (pt_password_hash(pw, a->env.random, a->env.ctx, hash, sizeof(hash)) != PT_OK) {
        fail(resp, 500, "internal", "could not store the password");
        goto done;
    }
    e = edit_begin(a);
    if (e == NULL) {
        fail(resp, 500, "internal", "out of memory");
        goto done;
    }
    if (language != NULL) {
        char patch[64];
        snprintf(patch, sizeof(patch), "{\"assistant\":{\"language\":\"%s\"}}", language);
        if (pt_settings_patch(&e->next, patch, strlen(patch), NULL, 0) != PT_OK) {
            fail(resp, 400, "invalid", "language: must be one of uk, en");
            goto done;
        }
    }
    snprintf(e->next.password_hash, sizeof(e->next.password_hash), "%s", hash);
    if (edit_commit(a, e, true) != PT_OK) {
        fail(resp, 500, "save_failed", "the settings could not be saved");
        goto done;
    }
    log_line(a, "the page password was created");
    start_session(a, req, resp);
done:
    free(e);
    cJSON_Delete(body);
}

static void h_login(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {"password", NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    const char *given = given_password(body, "password", resp);
    if (given != NULL) {
        if (!password_set(a)) {
            fail(resp, 409, "no_password", "create a password first");
        } else {
            check_t c = check_password(a, req, given);
            if (c == CHECK_BUSY) {
                too_many_checks(resp);
            } else if (c == CHECK_WRONG) {
                fail(resp, 401, "wrong_password", "wrong password");
            } else {
                start_session(a, req, resp);
            }
        }
    }
    cJSON_Delete(body);
}

static void h_logout(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    char token[PT_LOGIN_TOKEN_MAX];
    if (session_token(req, token, sizeof(token))) {
        pt_logins_drop(a->logins, token);
    }
    snprintf(resp->set_cookie, sizeof(resp->set_cookie), "%s=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0",
             COOKIE_NAME);
    answer(resp, 200, state_view(a, false));
}

static void h_password(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {"current", "new", NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    edit_t *e = NULL;
    const char *current = given_password(body, "current", resp);
    if (current == NULL) {
        goto done;
    }
    const char *fresh = new_password(body, "new", resp);
    if (fresh == NULL) {
        goto done;
    }
    check_t c = check_password(a, req, current);
    if (c == CHECK_BUSY) {
        too_many_checks(resp);
        goto done;
    }
    if (c == CHECK_WRONG) {
        fail(resp, 403, "wrong_password", "the current password is wrong");
        goto done;
    }
    char hash[PT_PASSWORD_HASH_MAX];
    if (pt_password_hash(fresh, a->env.random, a->env.ctx, hash, sizeof(hash)) != PT_OK) {
        fail(resp, 500, "internal", "could not store the password");
        goto done;
    }
    e = edit_begin(a);
    if (e == NULL) {
        fail(resp, 500, "internal", "out of memory");
        goto done;
    }
    snprintf(e->next.password_hash, sizeof(e->next.password_hash), "%s", hash);
    if (edit_commit(a, e, false) != PT_OK) {
        fail(resp, 500, "save_failed", "the settings could not be saved");
        goto done;
    }
    pt_logins_clear(a->logins);
    log_line(a, "the page password was changed");
    start_session(a, req, resp);
done:
    free(e);
    cJSON_Delete(body);
}

static void h_get_settings(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    (void)req;
    char *view = pt_settings_public_json(a->settings);
    if (view == NULL) {
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    size_t n = strlen(view) + 16;
    char *out = malloc(n);
    if (out == NULL) {
        free(view);
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    snprintf(out, n, "{\"settings\":%s}", view);
    free(view);
    resp->status = 200;
    free(resp->body);
    resp->body = out;
}

static void h_put_settings(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    cJSON *probe = parse_object(req, resp, NULL);
    if (probe == NULL) {
        return;
    }
    cJSON_Delete(probe);
    edit_t *e = edit_begin(a);
    if (e == NULL) {
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    char field[64];
    pt_err_t err = pt_settings_patch(&e->next, req->body, req->body_len, field, sizeof(field));
    if (err == PT_ERR_PARSE) {
        fail(resp, 400, "bad_json", "the body is not valid JSON");
    } else if (err != PT_OK) {
        if (field[0] == '\0' || strcmp(field, "settings") == 0) {
            fail(resp, 400, "bad_json", "the body must be a JSON object");
        } else {
            fail(resp, 400, "invalid", "%s: invalid value", field);
        }
    } else if (edit_commit(a, e, true) != PT_OK) {
        fail(resp, 500, "save_failed", "the settings could not be saved");
    } else {
        log_line(a, "settings changed");
        h_get_settings(a, req, resp);
    }
    free(e);
}

static void h_volume(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {"volume", NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(body, "volume");
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0.0 && v->valuedouble <= 1.0)) {
        fail(resp, 400, "invalid", "volume: must be a number from 0 to 1");
        cJSON_Delete(body);
        return;
    }
    float level = (float)v->valuedouble;
    cJSON_Delete(body);
    edit_t *e = edit_begin(a);
    if (e == NULL) {
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    e->next.sound.volume = level;
    if (edit_commit(a, e, false) != PT_OK) {
        fail(resp, 500, "save_failed", "the settings could not be saved");
    } else {
        if (a->env.set_volume != NULL) {
            a->env.set_volume(a->env.ctx, level);
        }
        cJSON *j = cJSON_CreateObject();
        if (j == NULL || cJSON_AddNumberToObject(j, "volume", (double)level) == NULL) {
            cJSON_Delete(j);
            j = NULL;
        }
        answer(resp, 200, j);
    }
    free(e);
}

static void h_sound_test(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    cJSON_Delete(body);
    if (a->env.sound_test == NULL) {
        fail(resp, 500, "internal", "the sound test is not available");
        return;
    }
    if (a->env.turn_active != NULL && a->env.turn_active(a->env.ctx)) {
        fail(resp, 409, "busy", "a conversation is running; try again when the speaker is quiet");
        return;
    }
    a->env.sound_test(a->env.ctx);
    log_line(a, "sound test");
    answer(resp, 200, cJSON_CreateObject());
}

static void h_instructions(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    (void)a;
    const char *q = strchr(req->path, '?');
    const char *text = NULL;
    if (q != NULL && strncmp(q + 1, "language=", 9) == 0) {
        text = pt_default_instructions(q + 10);
    }
    if (text == NULL) {
        fail(resp, 400, "invalid", "language: must be one of uk, en, es, fr, de, it, pt, pl, nl, tr");
        return;
    }
    cJSON *j = cJSON_CreateObject();
    if (j == NULL || cJSON_AddStringToObject(j, "instructions", text) == NULL) {
        cJSON_Delete(j);
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    answer(resp, 200, j);
}

static void h_log(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    const char *q = strchr(req->path, '?');
    unsigned long long after = 0;
    if (q != NULL && q[1] != '\0') {
        const char *digits = q + 1;
        if (strncmp(digits, "after=", 6) != 0 || digits[6] == '\0') {
            fail(resp, 400, "invalid", "after: must be a whole number");
            return;
        }
        digits += 6;
        size_t n = strlen(digits);
        if (n > 10 || strspn(digits, "0123456789") != n) {
            fail(resp, 400, "invalid", "after: must be a whole number");
            return;
        }
        after = strtoull(digits, NULL, 10);
        if (after > UINT32_MAX) {
            fail(resp, 400, "invalid", "after: must be a whole number");
            return;
        }
    }
    char *json = pt_log_json(a->log, (uint32_t)after);
    if (json == NULL) {
        fail(resp, 500, "internal", "out of memory");
        return;
    }
    resp->status = 200;
    free(resp->body);
    resp->body = json;
}

static cJSON *heap_area(const pt_heap_area_t *h)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL || cJSON_AddNumberToObject(o, "free", h->free) == NULL ||
        cJSON_AddNumberToObject(o, "min_free", h->min_free) == NULL ||
        cJSON_AddNumberToObject(o, "largest", h->largest) == NULL) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

static bool add_health(cJSON *j, const pt_device_health_t *h)
{
    if (cJSON_AddStringToObject(j, "mic_mode", h->mic_mode[0] != '\0' ? h->mic_mode : "off") == NULL) {
        return false;
    }
    cJSON *heap = cJSON_AddObjectToObject(j, "heap");
    if (heap == NULL) {
        return false;
    }
    cJSON *internal = heap_area(&h->internal);
    if (internal == NULL) {
        return false;
    }
    cJSON_AddItemToObject(heap, "internal", internal);
    cJSON *psram = heap_area(&h->psram);
    if (psram == NULL) {
        return false;
    }
    cJSON_AddItemToObject(heap, "psram", psram);
    return true;
}

static void h_device(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    (void)req;
    char ip[48] = "";
    int rssi = 0;
    if (a->env.device_info != NULL) {
        a->env.device_info(a->env.ctx, ip, sizeof(ip), &rssi);
    }
    pt_device_health_t health;
    memset(&health, 0, sizeof(health));
    if (a->env.device_health != NULL) {
        a->env.device_health(a->env.ctx, &health);
        health.mic_mode[sizeof(health.mic_mode) - 1] = '\0';
    }
    cJSON *j = cJSON_CreateObject();
    if (j == NULL || cJSON_AddStringToObject(j, "name", a->env.device_name) == NULL ||
        cJSON_AddStringToObject(j, "version", pt_version()) == NULL ||
        cJSON_AddStringToObject(j, "ip", ip) == NULL || cJSON_AddNumberToObject(j, "rssi", rssi) == NULL ||
        cJSON_AddNumberToObject(j, "volume", (double)a->settings->sound.volume) == NULL || !add_health(j, &health)) {
        cJSON_Delete(j);
        j = NULL;
    }
    answer(resp, 200, j);
}

static void rejected(pt_api_t *a, pt_http_resp_t *resp, const char *error)
{
    char code[64] = "";
    if (error[0] != '\0') {
        size_t n = strcspn(error, ": ");
        snprintf(code, sizeof(code), "%.*s", (int)(n < sizeof(code) ? n : sizeof(code) - 1), error);
        log_line(a, "OpenAI connection check: OpenAI did not accept the request: %s", error);
    } else {
        log_line(a, "OpenAI connection check: OpenAI did not accept the request");
    }
    fail(resp, 502, "openai_rejected", "OpenAI did not accept the request");
    if (code[0] == '\0') {
        return;
    }
    cJSON *j = cJSON_CreateObject();
    if (j != NULL && cJSON_AddStringToObject(j, "error", "openai_rejected") != NULL &&
        cJSON_AddStringToObject(j, "message", "OpenAI did not accept the request") != NULL &&
        cJSON_AddStringToObject(j, "openai_code", code) != NULL) {
        char *text = cJSON_PrintUnformatted(j);
        if (text != NULL) {
            free(resp->body);
            resp->body = text;
        }
    }
    cJSON_Delete(j);
}

static void h_openai_test(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    cJSON_Delete(body);
    if (a->env.openai_test == NULL) {
        fail(resp, 500, "internal", "the connection check is not available");
        return;
    }
    if (a->testing || (a->env.turn_active != NULL && a->env.turn_active(a->env.ctx))) {
        fail(resp, 409, "busy", "a conversation or another check is running; try again in a moment");
        return;
    }
    if (a->settings->openai.api_key[0] == '\0') {
        fail(resp, 400, "openai_no_key", "the OpenAI key is not set");
        return;
    }
    char model[PT_API_MODEL_MAX] = "";
    char voice[PT_API_VOICE_MAX] = "";
    char error[PT_API_ERROR_MAX] = "";
    a->testing = true;
    pt_ws_status_t status = a->env.openai_test(a->env.ctx, model, voice, error);
    error[sizeof(error) - 1] = '\0';
    a->testing = false;
    if (status == PT_WS_OK) {
        log_line(a, "OpenAI connection check: ok, model %s, voice %s", model[0] != '\0' ? model : "?",
                 voice[0] != '\0' ? voice : "?");
        cJSON *j = cJSON_CreateObject();
        if (j == NULL || cJSON_AddTrueToObject(j, "ok") == NULL || cJSON_AddStringToObject(j, "model", model) == NULL ||
            cJSON_AddStringToObject(j, "voice", voice) == NULL) {
            cJSON_Delete(j);
            j = NULL;
        }
        answer(resp, 200, j);
    } else if (status == PT_WS_BAD_KEY) {
        log_line(a, "OpenAI connection check: the key was refused");
        fail(resp, 502, "openai_bad_key", "OpenAI refused the key");
    } else if (status == PT_WS_TIMEOUT) {
        log_line(a, "OpenAI connection check: no answer in time");
        fail(resp, 504, "openai_timeout", "OpenAI did not answer in time");
    } else if (status == PT_WS_REJECTED) {
        rejected(a, resp, error);
    } else {
        log_line(a, "OpenAI connection check: OpenAI could not be reached");
        fail(resp, 502, "openai_unreachable", "OpenAI could not be reached");
    }
}

static bool update_busy(const pt_api_t *a)
{
    return a->env.turn_active != NULL && a->env.turn_active(a->env.ctx);
}

static void update_failed(pt_http_resp_t *resp, pt_err_t err, int failed_status)
{
    if (err == PT_ERR_NOT_OURS) {
        fail(resp, 400, "update_not_ours", "this file is not a Pytaichenko version 2 firmware");
    } else if (err == PT_ERR_BUSY) {
        fail(resp, 409, "update_busy", "an update is already finishing and the speaker is restarting");
    } else if (err == PT_ERR_TALKING) {
        fail(resp, 409, "update_busy", "a conversation is running; try again when the speaker is quiet");
    } else if (err == PT_ERR_PENDING) {
        fail(resp, 409, "update_busy", "the new firmware is still being checked; try again in a minute");
    } else {
        fail(resp, failed_status, "update_failed", "the update did not work; details are in the log");
    }
}

static void h_update_get(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    (void)req;
    if (a->env.ota_check == NULL) {
        fail(resp, 500, "internal", "updates are not available");
        return;
    }
    pt_release_t latest;
    memset(&latest, 0, sizeof(latest));
    pt_err_t err = a->env.ota_check(a->env.ctx, &latest);
    if (err != PT_OK && err != PT_ERR_NO_RELEASE) {
        log_line(a, "update check failed");
        update_failed(resp, err, 502);
        return;
    }
    bool found = err == PT_OK;
    cJSON *j = cJSON_CreateObject();
    bool ok = j != NULL && cJSON_AddStringToObject(j, "current", pt_version()) != NULL &&
              (found ? cJSON_AddStringToObject(j, "latest", latest.version) : cJSON_AddNullToObject(j, "latest")) !=
                  NULL &&
              cJSON_AddBoolToObject(j, "available", found && pt_version_cmp(latest.version, pt_version()) > 0) != NULL;
    if (!ok) {
        cJSON_Delete(j);
        j = NULL;
    }
    answer(resp, 200, j);
}

static void h_update_post(pt_api_t *a, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    static const char *const allowed[] = {NULL};
    cJSON *body = parse_object(req, resp, allowed);
    if (body == NULL) {
        return;
    }
    cJSON_Delete(body);
    if (a->env.ota_check == NULL || a->env.ota_install == NULL) {
        fail(resp, 500, "internal", "updates are not available");
        return;
    }
    if (a->uploading) {
        fail(resp, 409, "update_busy", "a firmware file is being uploaded; try again when it is finished");
        return;
    }
    if (update_busy(a)) {
        fail(resp, 409, "update_busy", "a conversation is running; try again when the speaker is quiet");
        return;
    }
    pt_release_t latest;
    memset(&latest, 0, sizeof(latest));
    pt_err_t err = a->env.ota_check(a->env.ctx, &latest);
    if (err == PT_ERR_NO_RELEASE || (err == PT_OK && pt_version_cmp(latest.version, pt_version()) <= 0)) {
        fail(resp, 404, "no_release", "there is no newer version to install");
        return;
    }
    if (err != PT_OK) {
        log_line(a, "update check failed");
        update_failed(resp, err, 502);
        return;
    }
    log_line(a, "updating to version %s", latest.version);
    err = a->env.ota_install(a->env.ctx, latest.url, latest.version);
    if (err != PT_OK) {
        log_line(a, err == PT_ERR_NOT_OURS ? "update refused: not a Pytaichenko version 2 firmware"
                                           : "update failed");
        update_failed(resp, err, 502);
        return;
    }
    log_line(a, "version %s is installed, restarting", latest.version);
    cJSON *j = cJSON_CreateObject();
    if (j == NULL || cJSON_AddTrueToObject(j, "installing") == NULL ||
        cJSON_AddStringToObject(j, "version", latest.version) == NULL) {
        cJSON_Delete(j);
        j = NULL;
    }
    answer(resp, 200, j);
}

static const route_t ROUTES[] = {
    {"/api/state", "GET", h_state},
    {"/api/setup", "POST", h_setup},
    {"/api/login", "POST", h_login},
    {"/api/logout", "POST", h_logout},
    {"/api/password", "POST", h_password},
    {"/api/settings", "GET", h_get_settings},
    {"/api/settings", "PUT", h_put_settings},
    {"/api/instructions", "GET", h_instructions},
    {"/api/volume", "PUT", h_volume},
    {"/api/sound/test", "POST", h_sound_test},
    {"/api/log", "GET", h_log},
    {"/api/device", "GET", h_device},
    {"/api/openai/test", "POST", h_openai_test},
    {"/api/update", "GET", h_update_get},
    {"/api/update", "POST", h_update_post},
};

static bool is_public(const char *path, size_t len)
{
    static const char *const open[] = {"/api/state", "/api/setup", "/api/login"};
    for (size_t i = 0; i < sizeof(open) / sizeof(open[0]); i++) {
        if (strlen(open[i]) == len && strncmp(path, open[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_safe_method(const char *m)
{
    return strcmp(m, "GET") == 0 || strcmp(m, "HEAD") == 0 || strcmp(m, "OPTIONS") == 0;
}

pt_api_t *pt_api_new(pt_settings_t *settings, pt_log_t *log, const pt_api_env_t *env)
{
    if (settings == NULL || env == NULL || env->save == NULL || env->now_s == NULL || env->random == NULL ||
        env->device_name == NULL) {
        return NULL;
    }
    pt_api_t *a = calloc(1, sizeof(*a));
    if (a == NULL) {
        return NULL;
    }
    a->settings = settings;
    a->log = log;
    a->env = *env;
    a->logins = pt_logins_new(PT_LOGIN_TTL_S, PT_LOGIN_LIMIT, env->random, env->ctx);
    a->throttle = pt_throttle_new(PT_THROTTLE_QUEUED, FAILED_DELAY_MS);
    if (a->logins == NULL || a->throttle == NULL) {
        pt_api_free(a);
        return NULL;
    }
    return a;
}

void pt_api_free(pt_api_t *api)
{
    if (api == NULL) {
        return;
    }
    upload_close(api);
    pt_logins_free(api->logins);
    pt_throttle_free(api->throttle);
    free(api);
}

static bool admit(pt_api_t *api, const pt_http_req_t *req, pt_http_resp_t *resp, const char *path, size_t path_len)
{
    if (!host_allowed(api, req->host)) {
        fail(resp, 403, "foreign_host", "this server name is not accepted");
        return false;
    }
    bool foreign = req->origin != NULL && !same_origin(req->origin, req->host);
    if (foreign || (req->sec_fetch_site != NULL && strcmp(req->sec_fetch_site, "same-origin") != 0 &&
                    strcmp(req->sec_fetch_site, "none") != 0)) {
        fail(resp, 403, "foreign_origin", "requests from other sites are not accepted");
        return false;
    }
    if (!is_public(path, path_len) && !logged_in(api, req)) {
        fail(resp, 401, "login_required", "log in first");
        return false;
    }
    return true;
}

static bool api_path(const char *path, size_t path_len)
{
    return (path_len == 4 && strncmp(path, "/api", 4) == 0) || strncmp(path, "/api/", 5) == 0;
}

void pt_api_handle(pt_api_t *api, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    const char *path = req->path != NULL ? req->path : "";
    const char *method = req->method != NULL ? req->method : "";
    size_t path_len = strcspn(path, "?");
    if (!api_path(path, path_len)) {
        fail(resp, 404, "not_found", "not found");
        return;
    }
    if (!admit(api, req, resp, path, path_len)) {
        return;
    }
    if (!is_safe_method(method) && !json_content_type(req->content_type)) {
        fail(resp, 415, "json_required", "send JSON with Content-Type: application/json");
        return;
    }
    if (req->body_len > MAX_BODY) {
        fail(resp, 413, "too_large", "the request body is too large");
        return;
    }
    bool path_known = false;
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        if (strlen(ROUTES[i].path) != path_len || strncmp(ROUTES[i].path, path, path_len) != 0) {
            continue;
        }
        path_known = true;
        if (strcmp(ROUTES[i].method, method) == 0) {
            ROUTES[i].fn(api, req, resp);
            return;
        }
    }
    if (path_known) {
        fail(resp, 405, "method_not_allowed", "method not allowed");
    } else {
        fail(resp, 404, "not_found", "not found");
    }
}

static void upload_close(pt_api_t *a)
{
    if (a->uploading) {
        a->uploading = false;
        if (a->env.ota_upload_abort != NULL) {
            a->env.ota_upload_abort(a->env.ctx);
        }
    }
}

bool pt_api_upload_begin(pt_api_t *api, const pt_http_req_t *req, pt_http_resp_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    const char *path = req->path != NULL ? req->path : "";
    const char *method = req->method != NULL ? req->method : "";
    size_t path_len = strcspn(path, "?");
    static const char upload_path[] = "/api/update/upload";
    if (path_len != sizeof(upload_path) - 1 || strncmp(path, upload_path, path_len) != 0) {
        fail(resp, 404, "not_found", "not found");
        return false;
    }
    if (!admit(api, req, resp, path, path_len)) {
        return false;
    }
    resp->drain_refused_body = true;
    if (strcmp(method, "POST") != 0) {
        fail(resp, 405, "method_not_allowed", "method not allowed");
        return false;
    }
    if (!octet_content_type(req->content_type)) {
        fail(resp, 415, "octet_required", "send the file as Content-Type: application/octet-stream");
        return false;
    }
    if (req->body_len == 0) {
        fail(resp, 400, "invalid", "the file is empty");
        return false;
    }
    if (req->body_len > PT_API_UPLOAD_MAX) {
        fail(resp, 413, "too_large", "the file is too large for a firmware");
        return false;
    }
    if (api->env.ota_upload_begin == NULL || api->env.ota_upload_write == NULL ||
        api->env.ota_upload_finish == NULL) {
        fail(resp, 500, "internal", "updates are not available");
        return false;
    }
    if (api->uploading || update_busy(api)) {
        fail(resp, 409, "update_busy", "a conversation or another update is running; try again in a moment");
        return false;
    }
    pt_err_t err = api->env.ota_upload_begin(api->env.ctx);
    if (err != PT_OK) {
        log_line(api, "firmware upload could not start");
        update_failed(resp, err, 500);
        return false;
    }
    api->uploading = true;
    api->upload_total = req->body_len;
    api->upload_got = 0;
    log_line(api, "firmware upload started, %u bytes", (unsigned)req->body_len);
    return true;
}

bool pt_api_upload_chunk(pt_api_t *api, const void *data, size_t n, pt_http_resp_t *resp)
{
    if (!api->uploading) {
        fail(resp, 500, "internal", "no upload is running");
        return false;
    }
    if (n > api->upload_total - api->upload_got) {
        upload_close(api);
        log_line(api, "firmware upload refused: more data than announced");
        fail(resp, 400, "invalid", "the file is longer than announced");
        return false;
    }
    pt_err_t err = api->env.ota_upload_write(api->env.ctx, data, n);
    if (err != PT_OK) {
        upload_close(api);
        log_line(api, err == PT_ERR_NOT_OURS ? "firmware upload refused: not a Pytaichenko version 2 firmware"
                                             : "firmware upload failed");
        update_failed(resp, err, 500);
        return false;
    }
    api->upload_got += n;
    return true;
}

void pt_api_upload_end(pt_api_t *api, pt_http_resp_t *resp)
{
    if (!api->uploading) {
        fail(resp, 500, "internal", "no upload is running");
        return;
    }
    if (api->upload_got != api->upload_total) {
        upload_close(api);
        log_line(api, "firmware upload stopped: the file ended early");
        fail(resp, 400, "invalid", "the file is shorter than announced");
        return;
    }
    api->uploading = false;
    pt_err_t err = api->env.ota_upload_finish(api->env.ctx);
    if (err != PT_OK) {
        log_line(api, err == PT_ERR_NOT_OURS ? "firmware upload refused: not a Pytaichenko version 2 firmware"
                                             : "firmware upload failed");
        update_failed(resp, err, 500);
        return;
    }
    log_line(api, "firmware file is installed, restarting");
    cJSON *j = cJSON_CreateObject();
    if (j == NULL || cJSON_AddTrueToObject(j, "installing") == NULL) {
        cJSON_Delete(j);
        j = NULL;
    }
    answer(resp, 200, j);
}

void pt_api_upload_abort(pt_api_t *api)
{
    upload_close(api);
}

void pt_api_drop_logins(pt_api_t *api)
{
    if (api != NULL) {
        pt_logins_clear(api->logins);
    }
}

void pt_http_resp_free(pt_http_resp_t *resp)
{
    if (resp != NULL) {
        free(resp->body);
        resp->body = NULL;
    }
}
