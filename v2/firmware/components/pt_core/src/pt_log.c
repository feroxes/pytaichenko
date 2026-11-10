#include "pt_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pt_core.h"

struct pt_log {
    size_t max_lines;
    size_t max_line_len;
    size_t head;
    size_t count;
    uint32_t next_seq;
    uint32_t *seqs;
    uint32_t *times;
    char *text;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *lock_ctx;
};

static char *slot(const pt_log_t *log, size_t i)
{
    return log->text + i * (log->max_line_len + 1);
}

pt_log_t *pt_log_new(size_t max_lines, size_t max_line_len)
{
    if (max_lines == 0 || max_line_len == 0 || max_lines > 100000 || max_line_len > 4096) {
        return NULL;
    }
    pt_log_t *log = calloc(1, sizeof(*log));
    if (log == NULL) {
        return NULL;
    }
    log->max_lines = max_lines;
    log->max_line_len = max_line_len;
    log->next_seq = 1;
    log->seqs = calloc(max_lines, sizeof(uint32_t));
    log->times = calloc(max_lines, sizeof(uint32_t));
    log->text = calloc(max_lines, max_line_len + 1);
    if (log->seqs == NULL || log->times == NULL || log->text == NULL) {
        pt_log_free(log);
        return NULL;
    }
    return log;
}

void pt_log_free(pt_log_t *log)
{
    if (log == NULL) {
        return;
    }
    free(log->seqs);
    free(log->times);
    free(log->text);
    free(log);
}

void pt_log_set_lock(pt_log_t *log, void (*lock)(void *ctx), void (*unlock)(void *ctx), void *ctx)
{
    if (log == NULL) {
        return;
    }
    bool both = lock != NULL && unlock != NULL;
    log->lock = both ? lock : NULL;
    log->unlock = both ? unlock : NULL;
    log->lock_ctx = both ? ctx : NULL;
}

static void enter(const pt_log_t *log)
{
    if (log->lock != NULL) {
        log->lock(log->lock_ctx);
    }
}

static void leave(const pt_log_t *log)
{
    if (log->unlock != NULL) {
        log->unlock(log->lock_ctx);
    }
}

void pt_log_vadd(pt_log_t *log, uint32_t now_s, const char *fmt, va_list ap)
{
    if (log == NULL || fmt == NULL) {
        return;
    }
    enter(log);
    size_t index = (log->head + log->count) % log->max_lines;
    if (log->count == log->max_lines) {
        index = log->head;
        log->head = (log->head + 1) % log->max_lines;
    } else {
        log->count++;
    }
    char *dst = slot(log, index);
    int n = vsnprintf(dst, log->max_line_len + 1, fmt, ap);
    size_t len = n < 0 ? 0 : (size_t)n;
    if (len > log->max_line_len) {
        len = pt_utf8_cut(dst, len, log->max_line_len);
    }
    dst[len] = '\0';
    log->seqs[index] = log->next_seq++;
    log->times[index] = now_s;
    leave(log);
}

void pt_log_add(pt_log_t *log, uint32_t now_s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pt_log_vadd(log, now_s, fmt, ap);
    va_end(ap);
}

typedef struct {
    char *buf;
    size_t pos;
} sink_t;

static void put(sink_t *k, const char *p, size_t n)
{
    if (k->buf != NULL) {
        memcpy(k->buf + k->pos, p, n);
    }
    k->pos += n;
}

static void put_str(sink_t *k, const char *p)
{
    put(k, p, strlen(p));
}

static void put_uint(sink_t *k, uint32_t v)
{
    char tmp[16];
    int n = snprintf(tmp, sizeof(tmp), "%u", (unsigned)v);
    put(k, tmp, (size_t)n);
}

static size_t utf8_seq_len(const unsigned char *p, size_t left)
{
    unsigned char c = p[0];
    size_t extra;
    uint32_t cp;
    uint32_t min;
    if ((c & 0xE0) == 0xC0) {
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
        return 0;
    }
    if (extra >= left) {
        return 0;
    }
    for (size_t k = 1; k <= extra; k++) {
        if ((p[k] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return extra + 1;
}

static void put_text(sink_t *k, const char *text)
{
    static const char HEX[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)text;
    size_t n = strlen(text);
    size_t i = 0;
    put(k, "\"", 1);
    while (i < n) {
        unsigned char c = p[i];
        if (c == '"') {
            put(k, "\\\"", 2);
        } else if (c == '\\') {
            put(k, "\\\\", 2);
        } else if (c < 0x20) {
            char esc[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 15]};
            put(k, esc, sizeof(esc));
        } else if (c < 0x80) {
            put(k, (const char *)&p[i], 1);
        } else {
            size_t len = utf8_seq_len(p + i, n - i);
            if (len == 0) {
                put(k, "\\ufffd", 6);
            } else {
                put(k, (const char *)&p[i], len);
                i += len - 1;
            }
        }
        i++;
    }
    put(k, "\"", 1);
}

static void write_json(const pt_log_t *log, uint32_t after_seq, sink_t *k)
{
    bool first = true;
    put_str(k, "{\"lines\":[");
    for (size_t i = 0; i < log->count; i++) {
        size_t index = (log->head + i) % log->max_lines;
        if (log->seqs[index] <= after_seq) {
            continue;
        }
        put_str(k, first ? "{\"seq\":" : ",{\"seq\":");
        first = false;
        put_uint(k, log->seqs[index]);
        put_str(k, ",\"t\":");
        put_uint(k, log->times[index]);
        put_str(k, ",\"text\":");
        put_text(k, slot(log, index));
        put(k, "}", 1);
    }
    put_str(k, "]}");
}

static char *json_locked(const pt_log_t *log, uint32_t after_seq)
{
    sink_t count = {NULL, 0};
    write_json(log, after_seq, &count);
    char *out = malloc(count.pos + 1);
    if (out == NULL) {
        return NULL;
    }
    sink_t fill = {out, 0};
    write_json(log, after_seq, &fill);
    out[fill.pos] = '\0';
    return out;
}

char *pt_log_json(const pt_log_t *log, uint32_t after_seq)
{
    if (log == NULL) {
        return NULL;
    }
    enter(log);
    char *out = json_locked(log, after_seq);
    leave(log);
    return out;
}
