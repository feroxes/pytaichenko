#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#define PT_OTA_PROJECT "pytaichenko"
#define PT_OTA_MAJOR 2
#define PT_OTA_URL_PREFIX "https://github.com/feroxes/pytaichenko/releases/download/"
#define PT_OTA_HEAD_LEN 288
#define PT_OTA_DESC_FIELD 32

typedef struct {
    char tag[32];
    char version[24];
    char url[256];
} pt_release_t;

int pt_version_cmp(const char *a, const char *b);
pt_err_t pt_ota_pick(const char *releases_json, size_t len, int major, pt_release_t *out);
bool pt_ota_url_ok(const char *url);
pt_err_t pt_ota_check_desc(const char *project_name, const char *version);
pt_err_t pt_ota_check_app_desc(const char *project_name, const char *version);
pt_err_t pt_ota_check_version(const char *image_version, const char *expected);
pt_err_t pt_ota_check_head(const void *head, size_t len);
