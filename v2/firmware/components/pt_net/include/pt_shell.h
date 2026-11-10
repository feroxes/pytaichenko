#pragma once

#include <stdint.h>

#include "pt_api.h"
#include "pt_core.h"
#include "pt_log.h"
#include "pt_settings.h"

#define PT_SHELL_LOG_LINES 400
#define PT_SHELL_LOG_LINE_LEN 200

pt_err_t pt_shell_init(void);
pt_log_t *pt_shell_log_get(void);
void pt_shell_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t pt_shell_now_s(void);
pt_settings_t *pt_shell_settings(void);
void pt_shell_settings_lock(void);
void pt_shell_settings_unlock(void);
void pt_shell_heap(pt_heap_area_t *internal, pt_heap_area_t *psram);
void pt_shell_log_health(const char *when);
