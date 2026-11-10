#include "pt_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pt_shell.h"

#define STORE_NAMESPACE "pytaichenko"
#define STORE_KEY "settings"

static void wipe_free(char *p, size_t n)
{
    if (p == NULL) {
        return;
    }
    volatile char *v = p;
    for (size_t i = 0; i < n; i++) {
        v[i] = 0;
    }
    free(p);
}

pt_err_t pt_store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        pt_shell_log("settings storage could not be read and was erased");
        if (nvs_flash_erase() != ESP_OK) {
            return PT_ERR_INVALID;
        }
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        pt_shell_log("settings storage failed to start (%s)", esp_err_to_name(e));
        return PT_ERR_INVALID;
    }
    return PT_OK;
}

static pt_err_t fall_back(pt_settings_t *s, pt_err_t why, const char *text)
{
    pt_settings_defaults(s, "uk");
    pt_shell_log("%s", text);
    return why;
}

pt_err_t pt_store_load(pt_settings_t *s)
{
    if (s == NULL) {
        return PT_ERR_INVALID;
    }
    nvs_handle_t h;
    esp_err_t e = nvs_open(STORE_NAMESPACE, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        return fall_back(s, PT_ERR_INVALID, "no saved settings, using defaults");
    }
    if (e != ESP_OK) {
        return fall_back(s, PT_ERR_PARSE, "saved settings could not be read, using defaults");
    }
    size_t len = 0;
    e = nvs_get_blob(h, STORE_KEY, NULL, &len);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return fall_back(s, PT_ERR_INVALID, "no saved settings, using defaults");
    }
    char *buf = (e == ESP_OK && len > 0) ? malloc(len) : NULL;
    if (buf == NULL || nvs_get_blob(h, STORE_KEY, buf, &len) != ESP_OK) {
        nvs_close(h);
        wipe_free(buf, len);
        return fall_back(s, PT_ERR_PARSE, "saved settings could not be read, using defaults");
    }
    nvs_close(h);
    int dropped = 0;
    pt_err_t p = pt_settings_load_stored(s, buf, len, &dropped);
    wipe_free(buf, len);
    if (p != PT_OK) {
        return fall_back(s, PT_ERR_PARSE, "saved settings are damaged, using defaults");
    }
    if (dropped > 0) {
        pt_shell_log("settings: %d fields reset to defaults", dropped);
    }
    return PT_OK;
}

pt_err_t pt_store_save(const pt_settings_t *s)
{
    if (s == NULL) {
        return PT_ERR_INVALID;
    }
    char *json = pt_settings_to_json(s);
    if (json == NULL) {
        return PT_ERR_NOMEM;
    }
    size_t n = strlen(json);
    nvs_handle_t h;
    esp_err_t e = nvs_open(STORE_NAMESPACE, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, STORE_KEY, json, n);
        if (e == ESP_OK) {
            e = nvs_commit(h);
        }
        nvs_close(h);
    }
    wipe_free(json, n);
    if (e != ESP_OK) {
        pt_shell_log("settings could not be saved (%s)", esp_err_to_name(e));
        return PT_ERR_INVALID;
    }
    return PT_OK;
}
