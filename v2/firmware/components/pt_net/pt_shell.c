#include "pt_shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "pytaichenko";

static SemaphoreHandle_t s_log_lock;
static SemaphoreHandle_t s_settings_lock;
static pt_log_t *s_log;
static pt_settings_t *s_settings;

static void log_lock(void *ctx)
{
    xSemaphoreTake((SemaphoreHandle_t)ctx, portMAX_DELAY);
}

static void log_unlock(void *ctx)
{
    xSemaphoreGive((SemaphoreHandle_t)ctx);
}

pt_err_t pt_shell_init(void)
{
    s_log_lock = xSemaphoreCreateMutex();
    s_settings_lock = xSemaphoreCreateRecursiveMutex();
    if (s_log_lock == NULL || s_settings_lock == NULL) {
        return PT_ERR_NOMEM;
    }
    heap_caps_malloc_extmem_enable(0);
    s_log = pt_log_new(PT_SHELL_LOG_LINES, PT_SHELL_LOG_LINE_LEN);
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    s_settings = heap_caps_calloc(1, sizeof(*s_settings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_settings == NULL) {
        s_settings = calloc(1, sizeof(*s_settings));
    }
    if (s_log == NULL || s_settings == NULL) {
        return PT_ERR_NOMEM;
    }
    pt_log_set_lock(s_log, log_lock, log_unlock, s_log_lock);
    pt_settings_defaults(s_settings, "uk");
    return PT_OK;
}

pt_log_t *pt_shell_log_get(void)
{
    return s_log;
}

uint32_t pt_shell_now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

void pt_shell_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "%s", line);
    pt_log_add(s_log, pt_shell_now_s(), "%s", line);
}

pt_settings_t *pt_shell_settings(void)
{
    return s_settings;
}

void pt_shell_settings_lock(void)
{
    xSemaphoreTakeRecursive(s_settings_lock, portMAX_DELAY);
}

void pt_shell_settings_unlock(void)
{
    xSemaphoreGiveRecursive(s_settings_lock);
}

static void heap_area(uint32_t caps, pt_heap_area_t *out)
{
    out->free = (uint32_t)heap_caps_get_free_size(caps);
    out->min_free = (uint32_t)heap_caps_get_minimum_free_size(caps);
    out->largest = (uint32_t)heap_caps_get_largest_free_block(caps);
}

void pt_shell_heap(pt_heap_area_t *internal, pt_heap_area_t *psram)
{
    heap_area(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, internal);
    heap_area(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, psram);
}

void pt_shell_log_health(const char *when)
{
    static const char *const tasks[] = {"pt_mic", "pt_convo", "pt_work", "pt_audio_out", "pt_ui", "pt_ws", "pt_ws_io",
                                        "httpd", "improv"};
    pt_heap_area_t internal;
    pt_heap_area_t psram;
    pt_shell_heap(&internal, &psram);
    pt_shell_log("memory %s: internal free %u, lowest %u, largest block %u; psram free %u, lowest %u, largest block %u",
                 when, (unsigned)internal.free, (unsigned)internal.min_free, (unsigned)internal.largest,
                 (unsigned)psram.free, (unsigned)psram.min_free, (unsigned)psram.largest);
    char line[200];
    size_t used = (size_t)snprintf(line, sizeof(line), "stack never used:");
    for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]) && used < sizeof(line); i++) {
        TaskHandle_t t = xTaskGetHandle(tasks[i]);
        if (t != NULL) {
            used += (size_t)snprintf(line + used, sizeof(line) - used, " %s %u", tasks[i],
                                     (unsigned)uxTaskGetStackHighWaterMark(t));
        }
    }
    pt_shell_log("%s", line);
}
