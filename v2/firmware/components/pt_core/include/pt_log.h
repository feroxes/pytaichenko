#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pt_log pt_log_t;

pt_log_t *pt_log_new(size_t max_lines, size_t max_line_len);
void pt_log_free(pt_log_t *log);
void pt_log_set_lock(pt_log_t *log, void (*lock)(void *ctx), void (*unlock)(void *ctx), void *ctx);
void pt_log_add(pt_log_t *log, uint32_t now_s, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void pt_log_vadd(pt_log_t *log, uint32_t now_s, const char *fmt, va_list ap) __attribute__((format(printf, 3, 0)));
char *pt_log_json(const pt_log_t *log, uint32_t after_seq);
