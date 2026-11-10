#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_auth.h"
#include "pt_core.h"
#include "pt_log.h"
#include "pt_otapick.h"
#include "pt_settings.h"
#include "pt_wsurl.h"

#define PT_API_MODEL_MAX 64
#define PT_API_VOICE_MAX 32
#define PT_API_ERROR_MAX 160
#define PT_API_UPLOAD_MAX (4u * 1024u * 1024u)

typedef struct {
    const char *method, *path, *host, *origin, *sec_fetch_site, *content_type, *cookie;
    uint32_t client_ip;
    const char *body;
    size_t body_len;
} pt_http_req_t;

typedef struct {
    int status;
    char *body;
    char set_cookie[160];
    bool drain_refused_body;
} pt_http_resp_t;

typedef struct {
    uint32_t free;
    uint32_t min_free;
    uint32_t largest;
} pt_heap_area_t;

typedef struct {
    char mic_mode[32];
    pt_heap_area_t internal;
    pt_heap_area_t psram;
} pt_device_health_t;

typedef struct {
    pt_err_t (*save)(void *ctx, const pt_settings_t *s);
    void (*apply)(void *ctx, const pt_settings_t *old, const pt_settings_t *now);
    void (*set_volume)(void *ctx, float volume);
    void (*sound_test)(void *ctx);
    uint32_t (*now_s)(void *ctx);
    void (*sleep_ms)(void *ctx, uint32_t ms);
    void (*device_info)(void *ctx, char *ip, size_t ip_len, int *rssi);
    void (*device_health)(void *ctx, pt_device_health_t *out);
    pt_ws_status_t (*openai_test)(void *ctx, char *model, char *voice, char *error);
    bool (*turn_active)(void *ctx);
    pt_err_t (*ota_check)(void *ctx, pt_release_t *latest);
    pt_err_t (*ota_install)(void *ctx, const char *url, const char *version);
    pt_err_t (*ota_upload_begin)(void *ctx);
    pt_err_t (*ota_upload_write)(void *ctx, const void *data, size_t n);
    pt_err_t (*ota_upload_finish)(void *ctx);
    void (*ota_upload_abort)(void *ctx);
    pt_random_fn random;
    void *ctx;
    const char *device_name;
} pt_api_env_t;

typedef struct pt_api pt_api_t;

pt_api_t *pt_api_new(pt_settings_t *settings, pt_log_t *log, const pt_api_env_t *env);
void pt_api_free(pt_api_t *api);
void pt_api_handle(pt_api_t *api, const pt_http_req_t *req, pt_http_resp_t *resp);
bool pt_api_upload_begin(pt_api_t *api, const pt_http_req_t *req, pt_http_resp_t *resp);
bool pt_api_upload_chunk(pt_api_t *api, const void *data, size_t n, pt_http_resp_t *resp);
void pt_api_upload_end(pt_api_t *api, pt_http_resp_t *resp);
void pt_api_upload_abort(pt_api_t *api);
void pt_api_drop_logins(pt_api_t *api);
void pt_http_resp_free(pt_http_resp_t *resp);
