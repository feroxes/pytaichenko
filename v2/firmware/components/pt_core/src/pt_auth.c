#include "pt_auth.h"

#include <stdlib.h>
#include <string.h>

#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

#define HASH_PREFIX "pbkdf2-sha256$"
#define ITERATIONS 20000u
#define MAX_ITERATIONS 1000000u
#define SALT_BYTES 16u
#define HASH_BYTES 32u
#define MAX_FIELD_BYTES 64u
#define MAX_STORED_LEN 160u
#define TOKEN_BYTES 32u
#define TOKEN_CHARS 43u
#define THROTTLE_SLOTS 16u

static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static int derive(const char *password, const uint8_t *salt, size_t salt_len, uint32_t iterations,
                  uint8_t *out, size_t out_len)
{
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)password, strlen(password),
                                         salt, salt_len, iterations, (uint32_t)out_len, out);
}

static bool b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_size, &olen, in, in_len) != 0) {
        return false;
    }
    out[olen] = '\0';
    return true;
}

static bool b64_decode_strict(const char *in, size_t in_len, uint8_t *out, size_t out_size, size_t *out_len)
{
    if (in_len == 0 || in_len % 4 != 0 || in_len > 4 * ((MAX_FIELD_BYTES + 2) / 3)) {
        return false;
    }
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
                     c == '/';
        if (!alpha && !(c == '=' && i >= in_len - 2)) {
            return false;
        }
    }
    return mbedtls_base64_decode(out, out_size, out_len, (const unsigned char *)in, in_len) == 0 && *out_len > 0;
}

pt_err_t pt_password_hash(const char *password, pt_random_fn rnd, void *ctx, char *out, size_t out_len)
{
    static const char prefix[] = HASH_PREFIX "20000$";
    char salt_b64[25];
    char hash_b64[45];
    uint8_t salt[SALT_BYTES];
    uint8_t digest[HASH_BYTES];
    if (password == NULL || rnd == NULL || out == NULL) {
        return PT_ERR_INVALID;
    }
    size_t need = (sizeof(prefix) - 1) + (sizeof(salt_b64) - 1) + 1 + (sizeof(hash_b64) - 1) + 1;
    if (out_len < need) {
        return PT_ERR_INVALID;
    }
    rnd(ctx, salt, sizeof(salt));
    if (derive(password, salt, sizeof(salt), ITERATIONS, digest, sizeof(digest)) != 0 ||
        !b64_encode(salt, sizeof(salt), salt_b64, sizeof(salt_b64)) ||
        !b64_encode(digest, sizeof(digest), hash_b64, sizeof(hash_b64))) {
        memset(digest, 0, sizeof(digest));
        return PT_ERR_INVALID;
    }
    memset(digest, 0, sizeof(digest));
    memcpy(out, prefix, sizeof(prefix) - 1);
    char *p = out + sizeof(prefix) - 1;
    memcpy(p, salt_b64, sizeof(salt_b64) - 1);
    p += sizeof(salt_b64) - 1;
    *p++ = '$';
    memcpy(p, hash_b64, sizeof(hash_b64));
    return PT_OK;
}

bool pt_password_verify(const char *password, const char *stored)
{
    if (password == NULL || stored == NULL) {
        return false;
    }
    size_t len = strnlen(stored, MAX_STORED_LEN + 1);
    size_t plen = strlen(HASH_PREFIX);
    if (len > MAX_STORED_LEN || len <= plen || memcmp(stored, HASH_PREFIX, plen) != 0) {
        return false;
    }
    const char *it = stored + plen;
    const char *end = stored + len;
    const char *d1 = memchr(it, '$', (size_t)(end - it));
    if (d1 == NULL || d1 == it || d1 - it > 7) {
        return false;
    }
    uint32_t iterations = 0;
    for (const char *c = it; c < d1; c++) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        iterations = iterations * 10 + (uint32_t)(*c - '0');
    }
    if (iterations == 0 || iterations > MAX_ITERATIONS) {
        return false;
    }
    const char *salt_s = d1 + 1;
    const char *d2 = memchr(salt_s, '$', (size_t)(end - salt_s));
    if (d2 == NULL) {
        return false;
    }
    const char *hash_s = d2 + 1;
    if (memchr(hash_s, '$', (size_t)(end - hash_s)) != NULL) {
        return false;
    }
    uint8_t salt[MAX_FIELD_BYTES];
    uint8_t expected[MAX_FIELD_BYTES];
    uint8_t actual[MAX_FIELD_BYTES];
    size_t salt_len = 0;
    size_t expected_len = 0;
    if (!b64_decode_strict(salt_s, (size_t)(d2 - salt_s), salt, sizeof(salt), &salt_len) ||
        !b64_decode_strict(hash_s, (size_t)(end - hash_s), expected, sizeof(expected), &expected_len)) {
        return false;
    }
    bool ok = derive(password, salt, salt_len, iterations, actual, expected_len) == 0 &&
              ct_equal(actual, expected, expected_len);
    memset(actual, 0, sizeof(actual));
    return ok;
}

typedef struct {
    char token[PT_LOGIN_TOKEN_MAX];
    uint32_t expires;
} login_t;

struct pt_logins {
    uint32_t ttl_s;
    size_t limit;
    size_t count;
    pt_random_fn rnd;
    void *ctx;
    login_t *items;
};

pt_logins_t *pt_logins_new(uint32_t ttl_s, size_t limit, pt_random_fn rnd, void *ctx)
{
    if (rnd == NULL || limit == 0) {
        return NULL;
    }
    pt_logins_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->items = calloc(limit, sizeof(login_t));
    if (s->items == NULL) {
        free(s);
        return NULL;
    }
    s->ttl_s = ttl_s;
    s->limit = limit;
    s->rnd = rnd;
    s->ctx = ctx;
    return s;
}

void pt_logins_free(pt_logins_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s->items, 0, s->limit * sizeof(login_t));
    free(s->items);
    free(s);
}

static void remove_at(pt_logins_t *s, size_t i)
{
    memmove(&s->items[i], &s->items[i + 1], (s->count - i - 1) * sizeof(login_t));
    s->count--;
    memset(&s->items[s->count], 0, sizeof(login_t));
}

static bool expired(const login_t *l, uint32_t now_s)
{
    return (int32_t)(l->expires - now_s) <= 0;
}

pt_err_t pt_logins_create(pt_logins_t *s, uint32_t now_s, char *token, size_t token_len)
{
    if (s == NULL || token == NULL || token_len < TOKEN_CHARS + 1) {
        return PT_ERR_INVALID;
    }
    size_t i = 0;
    while (i < s->count) {
        if (expired(&s->items[i], now_s)) {
            remove_at(s, i);
        } else {
            i++;
        }
    }
    while (s->count >= s->limit) {
        size_t oldest = 0;
        for (size_t k = 1; k < s->count; k++) {
            if ((int32_t)(s->items[k].expires - s->items[oldest].expires) < 0) {
                oldest = k;
            }
        }
        remove_at(s, oldest);
    }
    uint8_t raw[TOKEN_BYTES];
    char b64[48];
    s->rnd(s->ctx, raw, sizeof(raw));
    bool ok = b64_encode(raw, sizeof(raw), b64, sizeof(b64));
    memset(raw, 0, sizeof(raw));
    if (!ok) {
        return PT_ERR_INVALID;
    }
    for (size_t k = 0; k < TOKEN_CHARS; k++) {
        b64[k] = b64[k] == '+' ? '-' : b64[k] == '/' ? '_' : b64[k];
    }
    b64[TOKEN_CHARS] = '\0';
    login_t *l = &s->items[s->count++];
    memset(l, 0, sizeof(*l));
    memcpy(l->token, b64, TOKEN_CHARS + 1);
    l->expires = now_s + s->ttl_s;
    memcpy(token, b64, TOKEN_CHARS + 1);
    return PT_OK;
}

static bool find(const pt_logins_t *s, const char *token, size_t *index)
{
    if (token == NULL) {
        return false;
    }
    size_t len = strnlen(token, PT_LOGIN_TOKEN_MAX);
    if (len == 0 || len >= PT_LOGIN_TOKEN_MAX) {
        return false;
    }
    char padded[PT_LOGIN_TOKEN_MAX];
    memset(padded, 0, sizeof(padded));
    memcpy(padded, token, len);
    bool found = false;
    for (size_t i = 0; i < s->count; i++) {
        if (ct_equal((const uint8_t *)s->items[i].token, (const uint8_t *)padded, PT_LOGIN_TOKEN_MAX) && !found) {
            *index = i;
            found = true;
        }
    }
    return found;
}

bool pt_logins_valid(pt_logins_t *s, const char *token, uint32_t now_s)
{
    size_t i;
    if (s == NULL || !find(s, token, &i)) {
        return false;
    }
    if (expired(&s->items[i], now_s)) {
        remove_at(s, i);
        return false;
    }
    return true;
}

void pt_logins_drop(pt_logins_t *s, const char *token)
{
    size_t i;
    if (s != NULL && find(s, token, &i)) {
        remove_at(s, i);
    }
}

void pt_logins_clear(pt_logins_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s->items, 0, s->limit * sizeof(login_t));
    s->count = 0;
}

typedef struct {
    uint32_t ip;
    size_t queued;
} slot_t;

struct pt_throttle {
    size_t max_queued;
    uint32_t failed_delay_ms;
    slot_t slots[THROTTLE_SLOTS];
};

pt_throttle_t *pt_throttle_new(size_t max_queued, uint32_t failed_delay_ms)
{
    if (max_queued == 0) {
        return NULL;
    }
    pt_throttle_t *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->max_queued = max_queued;
    t->failed_delay_ms = failed_delay_ms;
    return t;
}

void pt_throttle_free(pt_throttle_t *t)
{
    free(t);
}

static slot_t *slot_for(pt_throttle_t *t, uint32_t ip)
{
    for (size_t i = 0; i < THROTTLE_SLOTS; i++) {
        if (t->slots[i].queued > 0 && t->slots[i].ip == ip) {
            return &t->slots[i];
        }
    }
    return NULL;
}

pt_err_t pt_throttle_enter(pt_throttle_t *t, uint32_t client_ip)
{
    if (t == NULL) {
        return PT_ERR_INVALID;
    }
    slot_t *s = slot_for(t, client_ip);
    if (s == NULL) {
        for (size_t i = 0; i < THROTTLE_SLOTS; i++) {
            if (t->slots[i].queued == 0) {
                s = &t->slots[i];
                s->ip = client_ip;
                break;
            }
        }
        if (s == NULL) {
            return PT_ERR_BUSY;
        }
    } else if (s->queued > t->max_queued) {
        return PT_ERR_BUSY;
    }
    s->queued++;
    return PT_OK;
}

uint32_t pt_throttle_check_done(const pt_throttle_t *t, bool ok)
{
    return (t != NULL && !ok) ? t->failed_delay_ms : 0;
}

void pt_throttle_leave(pt_throttle_t *t, uint32_t client_ip)
{
    if (t == NULL) {
        return;
    }
    slot_t *s = slot_for(t, client_ip);
    if (s != NULL) {
        s->queued--;
    }
}
