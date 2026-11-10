#include "pt_otapick.h"

#include <stdio.h>
#include <string.h>

#define IMAGE_MAGIC 0xE9
#define DESC_OFFSET 32
#define DESC_MAGIC 0xABCD5432u
#define DESC_VERSION_OFFSET 16
#define DESC_PROJECT_OFFSET 48
#define DESC_NAME_LEN 32
#define MAX_NUMBER_DIGITS 6

static const char *skip_v(const char *s)
{
    return s != NULL && (s[0] == 'v' || s[0] == 'V') ? s + 1 : s;
}

static unsigned long number(const char **p)
{
    unsigned long v = 0;
    int digits = 0;
    while (**p >= '0' && **p <= '9') {
        if (digits < MAX_NUMBER_DIGITS) {
            v = v * 10 + (unsigned long)(**p - '0');
        }
        digits++;
        (*p)++;
    }
    return v;
}

int pt_version_cmp(const char *a, const char *b)
{
    a = skip_v(a);
    b = skip_v(b);
    if (a == NULL || b == NULL) {
        return (a != NULL) - (b != NULL);
    }
    for (int part = 0; part < 3; part++) {
        unsigned long x = number(&a);
        unsigned long y = number(&b);
        if (x != y) {
            return x > y ? 1 : -1;
        }
        if (part < 2) {
            if (*a == '.') {
                a++;
            }
            if (*b == '.') {
                b++;
            }
        }
    }
    bool pre_a = *a == '-';
    bool pre_b = *b == '-';
    if (pre_a != pre_b) {
        return pre_a ? -1 : 1;
    }
    int c = strcmp(a, b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

static bool parse_tag(const char *tag, int *major)
{
    if (tag[0] != 'v') {
        return false;
    }
    const char *p = tag + 1;
    unsigned long parts[3];
    for (int i = 0; i < 3; i++) {
        const char *start = p;
        parts[i] = number(&p);
        if (p == start || p - start > MAX_NUMBER_DIGITS) {
            return false;
        }
        if (i < 2) {
            if (*p != '.') {
                return false;
            }
            p++;
        }
    }
    if (*p != '\0') {
        return false;
    }
    *major = (int)parts[0];
    return true;
}

bool pt_ota_url_ok(const char *url)
{
    return url != NULL && strncmp(url, PT_OTA_URL_PREFIX, strlen(PT_OTA_URL_PREFIX)) == 0;
}

typedef struct {
    const char *p;
    const char *end;
} cur_t;

typedef struct {
    char tag[32];
    bool tag_ok;
    bool draft;
    bool prerelease;
    const char *assets;
} rel_t;

typedef struct {
    const char *s;
    size_t n;
} span_t;

#define MAX_SCAN_DEPTH 32

static void ws(cur_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n')) {
        c->p++;
    }
}

static bool read_string(cur_t *c, span_t *out)
{
    if (c->p >= c->end || *c->p != '"') {
        return false;
    }
    c->p++;
    const char *start = c->p;
    while (c->p < c->end && *c->p != '"') {
        if (*c->p == '\\') {
            c->p++;
        }
        c->p++;
    }
    if (c->p >= c->end) {
        return false;
    }
    out->s = start;
    out->n = (size_t)(c->p - start);
    c->p++;
    return true;
}

static bool skip_value(cur_t *c)
{
    ws(c);
    if (c->p >= c->end) {
        return false;
    }
    if (*c->p == '"') {
        span_t unused;
        return read_string(c, &unused);
    }
    if (*c->p == '{' || *c->p == '[') {
        int depth = 0;
        while (c->p < c->end) {
            char ch = *c->p;
            if (ch == '"') {
                span_t unused;
                if (!read_string(c, &unused)) {
                    return false;
                }
                continue;
            }
            c->p++;
            if (ch == '{' || ch == '[') {
                if (++depth > MAX_SCAN_DEPTH) {
                    return false;
                }
            } else if (ch == '}' || ch == ']') {
                if (--depth == 0) {
                    return true;
                }
            }
        }
        return false;
    }
    const char *start = c->p;
    while (c->p < c->end && *c->p != ',' && *c->p != ']' && *c->p != '}' && *c->p != ' ' && *c->p != '\t' &&
           *c->p != '\r' && *c->p != '\n') {
        c->p++;
    }
    return c->p > start;
}

static bool is_key(const span_t *k, const char *name)
{
    return k->n == strlen(name) && memcmp(k->s, name, k->n) == 0;
}

static bool is_true(const cur_t *before, const cur_t *after)
{
    return (size_t)(after->p - before->p) == 4 && memcmp(before->p, "true", 4) == 0;
}

static bool plain(const span_t *v)
{
    return memchr(v->s, '\\', v->n) == NULL;
}

static bool member_key(cur_t *c, span_t *key)
{
    ws(c);
    if (!read_string(c, key)) {
        return false;
    }
    ws(c);
    if (c->p >= c->end || *c->p != ':') {
        return false;
    }
    c->p++;
    ws(c);
    return c->p < c->end;
}

static int member_end(cur_t *c, char close)
{
    ws(c);
    if (c->p >= c->end) {
        return -1;
    }
    if (*c->p == ',') {
        c->p++;
        return 1;
    }
    if (*c->p == close) {
        c->p++;
        return 0;
    }
    return -1;
}

static bool read_release(cur_t *c, rel_t *r)
{
    memset(r, 0, sizeof(*r));
    c->p++;
    ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        return true;
    }
    for (;;) {
        span_t key;
        if (!member_key(c, &key)) {
            return false;
        }
        if (is_key(&key, "tag_name") && *c->p == '"') {
            span_t v;
            if (!read_string(c, &v)) {
                return false;
            }
            if (v.n < sizeof(r->tag) && plain(&v)) {
                memcpy(r->tag, v.s, v.n);
                r->tag[v.n] = '\0';
                r->tag_ok = true;
            }
        } else if (is_key(&key, "draft") || is_key(&key, "prerelease")) {
            cur_t before = *c;
            if (!skip_value(c)) {
                return false;
            }
            if (is_key(&key, "draft")) {
                r->draft = is_true(&before, c);
            } else {
                r->prerelease = is_true(&before, c);
            }
        } else {
            if (is_key(&key, "assets") && *c->p == '[') {
                r->assets = c->p;
            }
            if (!skip_value(c)) {
                return false;
            }
        }
        int more = member_end(c, '}');
        if (more < 0) {
            return false;
        }
        if (more == 0) {
            return true;
        }
    }
}

static bool asset_url(const char *assets, const char *end, const char *wanted, char *url, size_t url_len)
{
    cur_t c = {.p = assets, .end = end};
    c.p++;
    ws(&c);
    if (c.p < c.end && *c.p == ']') {
        return false;
    }
    for (;;) {
        ws(&c);
        if (c.p >= c.end) {
            return false;
        }
        if (*c.p == '{') {
            span_t name = {0};
            span_t link = {0};
            bool have_name = false;
            bool have_link = false;
            c.p++;
            ws(&c);
            if (c.p < c.end && *c.p == '}') {
                c.p++;
            } else {
                for (;;) {
                    span_t key;
                    if (!member_key(&c, &key)) {
                        return false;
                    }
                    if ((is_key(&key, "name") || is_key(&key, "browser_download_url")) && *c.p == '"') {
                        span_t v;
                        if (!read_string(&c, &v)) {
                            return false;
                        }
                        if (is_key(&key, "name")) {
                            name = v;
                            have_name = true;
                        } else {
                            link = v;
                            have_link = true;
                        }
                    } else if (!skip_value(&c)) {
                        return false;
                    }
                    int more = member_end(&c, '}');
                    if (more < 0) {
                        return false;
                    }
                    if (more == 0) {
                        break;
                    }
                }
            }
            if (have_name && have_link && name.n == strlen(wanted) && memcmp(name.s, wanted, name.n) == 0 &&
                plain(&link) && link.n < url_len && link.n >= strlen(PT_OTA_URL_PREFIX) &&
                memcmp(link.s, PT_OTA_URL_PREFIX, strlen(PT_OTA_URL_PREFIX)) == 0) {
                memcpy(url, link.s, link.n);
                url[link.n] = '\0';
                return true;
            }
        } else if (!skip_value(&c)) {
            return false;
        }
        int more = member_end(&c, ']');
        if (more <= 0) {
            return false;
        }
    }
}

pt_err_t pt_ota_pick(const char *releases_json, size_t len, int major, pt_release_t *out)
{
    if (releases_json == NULL || out == NULL) {
        return PT_ERR_INVALID;
    }
    const char *end = releases_json + len;
    cur_t c = {.p = releases_json, .end = end};
    ws(&c);
    if (c.p >= end || *c.p != '[') {
        return PT_ERR_PARSE;
    }
    c.p++;
    ws(&c);
    bool found = false;
    if (c.p < end && *c.p == ']') {
        c.p++;
    } else {
        for (;;) {
            ws(&c);
            if (c.p >= end) {
                return PT_ERR_PARSE;
            }
            if (*c.p == '{') {
                rel_t r;
                if (!read_release(&c, &r)) {
                    return PT_ERR_PARSE;
                }
                int tag_major = 0;
                char wanted[64];
                if (r.tag_ok && !r.draft && !r.prerelease && r.assets != NULL && parse_tag(r.tag, &tag_major) &&
                    tag_major == major && strlen(r.tag + 1) < sizeof(out->version) &&
                    (!found || pt_version_cmp(r.tag, out->tag) > 0) &&
                    snprintf(wanted, sizeof(wanted), "pytaichenko-%s.ota.bin", r.tag + 1) < (int)sizeof(wanted)) {
                    char url[sizeof(out->url)];
                    if (asset_url(r.assets, end, wanted, url, sizeof(url))) {
                        memcpy(out->tag, r.tag, strlen(r.tag) + 1);
                        memcpy(out->version, r.tag + 1, strlen(r.tag));
                        memcpy(out->url, url, strlen(url) + 1);
                        found = true;
                    }
                }
            } else if (!skip_value(&c)) {
                return PT_ERR_PARSE;
            }
            int more = member_end(&c, ']');
            if (more < 0) {
                return PT_ERR_PARSE;
            }
            if (more == 0) {
                break;
            }
        }
    }
    ws(&c);
    if (c.p != end) {
        return PT_ERR_PARSE;
    }
    return found ? PT_OK : PT_ERR_NO_RELEASE;
}

pt_err_t pt_ota_check_desc(const char *project_name, const char *version)
{
    char major[16];
    snprintf(major, sizeof(major), "%d.", PT_OTA_MAJOR);
    if (project_name == NULL || version == NULL || strcmp(project_name, PT_OTA_PROJECT) != 0 ||
        strncmp(version, major, strlen(major)) != 0) {
        return PT_ERR_NOT_OURS;
    }
    return PT_OK;
}

pt_err_t pt_ota_check_version(const char *image_version, const char *expected)
{
    if (image_version == NULL || expected == NULL || expected[0] == '\0' || strcmp(image_version, expected) != 0) {
        return PT_ERR_INVALID;
    }
    return PT_OK;
}

static bool terminated(const void *field)
{
    return memchr(field, '\0', DESC_NAME_LEN) != NULL;
}

pt_err_t pt_ota_check_app_desc(const char *project_name, const char *version)
{
    if (project_name == NULL || version == NULL || !terminated(project_name) || !terminated(version)) {
        return PT_ERR_NOT_OURS;
    }
    return pt_ota_check_desc(project_name, version);
}

pt_err_t pt_ota_check_head(const void *head, size_t len)
{
    const uint8_t *h = head;
    if (h == NULL || len < PT_OTA_HEAD_LEN) {
        return PT_ERR_INVALID;
    }
    uint32_t magic = (uint32_t)h[DESC_OFFSET] | ((uint32_t)h[DESC_OFFSET + 1] << 8) |
                     ((uint32_t)h[DESC_OFFSET + 2] << 16) | ((uint32_t)h[DESC_OFFSET + 3] << 24);
    if (h[0] != IMAGE_MAGIC || magic != DESC_MAGIC) {
        return PT_ERR_NOT_OURS;
    }
    return pt_ota_check_app_desc((const char *)h + DESC_OFFSET + DESC_PROJECT_OFFSET,
                                 (const char *)h + DESC_OFFSET + DESC_VERSION_OFFSET);
}
