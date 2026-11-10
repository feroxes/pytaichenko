#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "pt_api.h"
#include "pt_app.h"
#include "pt_core.h"
#include "pt_http.h"
#include "pt_improv.h"
#include "pt_ota.h"
#include "pt_shell.h"
#include "pt_store.h"
#include "pt_wifi.h"
#include "pt_ws.h"

static const char *TAG = "main";

static pt_err_t env_save(void *ctx, const pt_settings_t *s)
{
    (void)ctx;
    return pt_store_save(s);
}

static void env_apply(void *ctx, const pt_settings_t *old, const pt_settings_t *now)
{
    (void)ctx;
    (void)old;
    pt_app_apply(now);
    pt_shell_log("settings applied");
}

static void env_set_volume(void *ctx, float volume)
{
    (void)ctx;
    pt_app_volume(volume);
    pt_shell_log("volume set to %d%%", (int)(volume * 100.0f + 0.5f));
}

static void env_sound_test(void *ctx)
{
    (void)ctx;
    pt_app_sound_test();
}

static uint32_t env_now_s(void *ctx)
{
    (void)ctx;
    return pt_shell_now_s();
}

static void env_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    pt_shell_settings_unlock();
    vTaskDelay(pdMS_TO_TICKS(ms));
    pt_shell_settings_lock();
}

static void env_device_info(void *ctx, char *ip, size_t ip_len, int *rssi)
{
    (void)ctx;
    pt_wifi_info(ip, ip_len, rssi);
}

static void env_device_health(void *ctx, pt_device_health_t *out)
{
    (void)ctx;
    snprintf(out->mic_mode, sizeof(out->mic_mode), "%s", pt_app_mic_mode());
    pt_shell_heap(&out->internal, &out->psram);
}

static void env_random(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
}

static bool env_turn_active(void *ctx)
{
    (void)ctx;
    return pt_app_turn_active();
}

static pt_ws_status_t env_openai_test(void *ctx, char *model, char *voice, char *error)
{
    (void)ctx;
    pt_settings_t *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return PT_WS_NO_NETWORK;
    }
    memcpy(copy, pt_shell_settings(), sizeof(*copy));
    pt_shell_settings_unlock();
    pt_ws_status_t status =
        pt_ws_probe(copy, PT_WS_PROBE_MS, model, PT_API_MODEL_MAX, voice, PT_API_VOICE_MAX, error, PT_API_ERROR_MAX);
    pt_shell_settings_lock();
    volatile unsigned char *v = (volatile unsigned char *)copy;
    for (size_t i = 0; i < sizeof(*copy); i++) {
        v[i] = 0;
    }
    free(copy);
    return status;
}

static pt_err_t env_ota_check(void *ctx, pt_release_t *latest)
{
    (void)ctx;
    pt_shell_settings_unlock();
    pt_err_t err = pt_ota_check(latest);
    pt_shell_settings_lock();
    return err;
}

static pt_err_t env_ota_install(void *ctx, const char *url, const char *version)
{
    (void)ctx;
    pt_shell_settings_unlock();
    pt_err_t err = pt_app_quiet_for_update();
    if (err == PT_OK) {
        err = pt_ota_from_url(url, version);
    }
    pt_shell_settings_lock();
    if (err != PT_OK) {
        pt_app_hold(false);
    }
    return err;
}

static pt_err_t env_ota_upload_begin(void *ctx)
{
    (void)ctx;
    pt_shell_settings_unlock();
    pt_err_t err = pt_app_quiet_for_update();
    pt_shell_settings_lock();
    if (err == PT_OK) {
        err = pt_ota_begin_upload();
    }
    if (err != PT_OK) {
        pt_app_hold(false);
    }
    return err;
}

static pt_err_t env_ota_upload_write(void *ctx, const void *data, size_t n)
{
    (void)ctx;
    return pt_ota_write(data, n);
}

static pt_err_t env_ota_upload_finish(void *ctx)
{
    (void)ctx;
    pt_err_t err = pt_ota_finish();
    if (err != PT_OK) {
        pt_app_hold(false);
    }
    return err;
}

static void env_ota_upload_abort(void *ctx)
{
    (void)ctx;
    pt_ota_abort();
    pt_app_hold(false);
}

static void env_drop_logins(void *ctx)
{
    pt_api_drop_logins(ctx);
}

static void stop(const char *what)
{
    ESP_LOGE(TAG, "%s could not start", what);
    abort();
}

static void start_mdns(const char *name)
{
    esp_err_t e = mdns_init();
    if (e == ESP_OK) {
        e = mdns_hostname_set(name);
    }
    if (e == ESP_OK) {
        e = mdns_instance_name_set("Pytaichenko");
    }
    if (e == ESP_OK) {
        e = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    if (e != ESP_OK) {
        pt_shell_log("the name %s.local could not be announced (%s)", name, esp_err_to_name(e));
    }
}

void app_main(void)
{
    if (pt_shell_init() != PT_OK) {
        stop("log");
    }
    if (pt_store_init() != PT_OK) {
        stop("settings storage");
    }
    pt_settings_t *settings = pt_shell_settings();
    pt_shell_settings_lock();
    pt_store_load(settings);
    pt_shell_settings_unlock();
    const char *name = pt_wifi_name();
    pt_shell_log("Pytaichenko %s started, name %s", pt_version(), name);
    if (pt_wifi_start() != PT_OK) {
        pt_shell_log("Wi-Fi could not start");
    }
    pt_improv_start();
    start_mdns(name);
    const pt_api_env_t env = {
        .save = env_save,
        .apply = env_apply,
        .set_volume = env_set_volume,
        .sound_test = env_sound_test,
        .now_s = env_now_s,
        .sleep_ms = env_sleep_ms,
        .device_info = env_device_info,
        .device_health = env_device_health,
        .random = env_random,
        .openai_test = env_openai_test,
        .turn_active = env_turn_active,
        .ota_check = env_ota_check,
        .ota_install = env_ota_install,
        .ota_upload_begin = env_ota_upload_begin,
        .ota_upload_write = env_ota_upload_write,
        .ota_upload_finish = env_ota_upload_finish,
        .ota_upload_abort = env_ota_upload_abort,
        .ctx = NULL,
        .device_name = name,
    };
    pt_api_t *api = pt_api_new(settings, pt_shell_log_get(), &env);
    if (api == NULL) {
        stop("settings page");
    }
    if (pt_http_start(api) == PT_OK) {
        pt_shell_log("settings page at http://%s.local/", name);
        pt_ota_mark_valid_later();
    }
    pt_app_on_password_reset(env_drop_logins, api);
    pt_shell_settings_lock();
    pt_err_t err = pt_app_start(settings);
    pt_shell_settings_unlock();
    if (err != PT_OK) {
        pt_shell_log("the speaker could not start (error %d); the settings page still works", err);
    }
    pt_shell_log_health("after start");
}
