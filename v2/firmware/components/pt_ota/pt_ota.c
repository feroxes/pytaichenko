#include "pt_ota.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "pt_shell.h"
#include "pt_wifi.h"

#define RELEASES_URL "https://api.github.com/repos/feroxes/pytaichenko/releases?per_page=10"
#define RELEASES_MAX (160u * 1024u)
#define HTTP_TIMEOUT_MS 15000
#define HTTP_OK 200
#define HTTP_TX_BUF 2048
#define MARK_VALID_FIRST_US (30ll * 1000000ll)
#define MARK_VALID_RETRY_US (10ll * 1000000ll)
#define RESTART_DELAY_US (2500ll * 1000ll)
#define PROGRESS_STEP_BYTES (256u * 1024u)
#define OTA_HEADER_READ 1024u

_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == 32, "image head layout");
_Static_assert(offsetof(esp_app_desc_t, version) == 16, "app description layout");
_Static_assert(offsetof(esp_app_desc_t, project_name) == 48, "app description layout");
_Static_assert(sizeof(((esp_app_desc_t *)0)->version) == 32, "app description layout");
_Static_assert(sizeof(((esp_app_desc_t *)0)->project_name) == 32, "app description layout");
_Static_assert(ESP_APP_DESC_MAGIC_WORD == 0xABCD5432u, "app description magic");
_Static_assert(ESP_IMAGE_HEADER_MAGIC == 0xE9, "image magic");
_Static_assert(32 + sizeof(esp_app_desc_t) == PT_OTA_HEAD_LEN, "head length");

static struct {
    bool active;
    bool started;
    esp_ota_handle_t handle;
    const esp_partition_t *part;
    uint8_t head[PT_OTA_HEAD_LEN];
    size_t head_len;
    size_t written;
} s_up;

static size_t s_download_seen;

static esp_err_t count_download(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        s_download_seen += (size_t)evt->data_len;
    }
    return ESP_OK;
}

static esp_timer_handle_t s_valid_timer;
static esp_timer_handle_t s_restart_timer;
static bool s_restart_pending;

static const char *user_agent(void)
{
    static char text[40];
    snprintf(text, sizeof(text), "pytaichenko/%s", pt_version());
    return text;
}

static void restart_now(void *arg)
{
    (void)arg;
    esp_restart();
}

static pt_err_t gate(void)
{
    if (s_restart_pending) {
        return PT_ERR_BUSY;
    }
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        pt_shell_log("update refused: the running firmware is not confirmed yet");
        return PT_ERR_PENDING;
    }
    return PT_OK;
}

static void schedule_restart(void)
{
    s_restart_pending = true;
    const esp_timer_create_args_t args = {.callback = restart_now, .name = "ota_restart"};
    if (esp_timer_create(&args, &s_restart_timer) == ESP_OK && esp_timer_start_once(s_restart_timer, RESTART_DELAY_US) == ESP_OK) {
        pt_shell_log("restarting to start the new firmware");
    } else {
        pt_shell_log("the new firmware is installed; restarting now");
        esp_restart();
    }
}

static char *releases_buffer(void)
{
    char *buf = heap_caps_malloc(RELEASES_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return buf != NULL ? buf : malloc(RELEASES_MAX + 1);
}

pt_err_t pt_ota_check(pt_release_t *latest)
{
    if (latest == NULL) {
        return PT_ERR_INVALID;
    }
    char *buf = releases_buffer();
    if (buf == NULL) {
        return PT_ERR_NOMEM;
    }
    const esp_http_client_config_t config = {
        .url = RELEASES_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .user_agent = user_agent(),
    };
    pt_err_t result = PT_ERR_IO;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(buf);
        return PT_ERR_NOMEM;
    }
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    if (esp_http_client_open(client, 0) != ESP_OK) {
        pt_shell_log("update check: GitHub could not be reached");
        goto done;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != HTTP_OK) {
        pt_shell_log("update check: GitHub answered %d%s", status,
                     status == 403 || status == 429 ? " (too many checks, try again in an hour)" : "");
        goto done;
    }
    size_t got = 0;
    while (got < RELEASES_MAX) {
        int n = esp_http_client_read(client, buf + got, (int)(RELEASES_MAX - got));
        if (n < 0) {
            pt_shell_log("update check: the answer was cut off");
            goto done;
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    if (!esp_http_client_is_complete_data_received(client)) {
        pt_shell_log("update check: the answer is too long or was cut off");
        goto done;
    }
    buf[got] = '\0';
    result = pt_ota_pick(buf, got, PT_OTA_MAJOR, latest);
    if (result == PT_ERR_PARSE) {
        pt_shell_log("update check: the answer could not be read");
        result = PT_ERR_IO;
    } else if (result == PT_ERR_NO_RELEASE) {
        pt_shell_log("update check: no version %d release with a firmware file", PT_OTA_MAJOR);
    } else if (result == PT_OK) {
        pt_shell_log("update check: latest is %s, running %s", latest->version, pt_version());
    }
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buf);
    return result;
}

pt_err_t pt_ota_from_url(const char *url, const char *version)
{
    if (!pt_ota_url_ok(url) || version == NULL) {
        return PT_ERR_INVALID;
    }
    pt_err_t blocked = gate();
    if (blocked != PT_OK) {
        return blocked;
    }
    const esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size_tx = HTTP_TX_BUF,
        .keep_alive_enable = true,
        .user_agent = user_agent(),
        .event_handler = count_download,
    };
    const esp_https_ota_config_t config = {.http_config = &http};
    s_download_seen = 0;
    esp_https_ota_handle_t handle = NULL;
    if (esp_https_ota_begin(&config, &handle) != ESP_OK) {
        pt_shell_log("update: the firmware file could not be downloaded");
        return PT_ERR_IO;
    }
    esp_app_desc_t desc;
    if (esp_https_ota_get_img_desc(handle, &desc) != ESP_OK) {
        bool cut_short = s_download_seen < OTA_HEADER_READ && !esp_https_ota_is_complete_data_received(handle);
        esp_https_ota_abort(handle);
        if (cut_short) {
            pt_shell_log("update: the connection broke before the firmware header arrived");
            return PT_ERR_IO;
        }
        pt_shell_log("update: the downloaded file is not a firmware");
        return PT_ERR_NOT_OURS;
    }
    if (pt_ota_check_app_desc(desc.project_name, desc.version) != PT_OK) {
        pt_shell_log("update refused: the file is not a Pytaichenko version %d firmware", PT_OTA_MAJOR);
        esp_https_ota_abort(handle);
        return PT_ERR_NOT_OURS;
    }
    if (pt_ota_check_version(desc.version, version) != PT_OK) {
        pt_shell_log("update refused: the file says version %s, but the release is %s", desc.version, version);
        esp_https_ota_abort(handle);
        return PT_ERR_IO;
    }
    size_t next_note = PROGRESS_STEP_BYTES;
    esp_err_t e;
    while ((e = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int done = esp_https_ota_get_image_len_read(handle);
        if (done > 0 && (size_t)done >= next_note) {
            pt_shell_log("update: %d KB downloaded", done / 1024);
            next_note = (size_t)done + PROGRESS_STEP_BYTES;
        }
    }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        pt_shell_log("update: the download stopped before the end");
        esp_https_ota_abort(handle);
        return PT_ERR_IO;
    }
    if (esp_https_ota_finish(handle) != ESP_OK) {
        pt_shell_log("update: the downloaded firmware did not pass the check");
        return PT_ERR_IO;
    }
    pt_shell_log("update: version %s is written", desc.version);
    schedule_restart();
    return PT_OK;
}

pt_err_t pt_ota_begin_upload(void)
{
    if (s_up.active) {
        return PT_ERR_BUSY;
    }
    pt_err_t blocked = gate();
    if (blocked != PT_OK) {
        return blocked;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        pt_shell_log("firmware upload: there is no free update partition");
        return PT_ERR_IO;
    }
    memset(&s_up, 0, sizeof(s_up));
    s_up.part = part;
    s_up.active = true;
    return PT_OK;
}

pt_err_t pt_ota_write(const void *data, size_t n)
{
    if (!s_up.active || (data == NULL && n > 0)) {
        return PT_ERR_INVALID;
    }
    const uint8_t *p = data;
    size_t left = n;
    if (!s_up.started) {
        size_t take = PT_OTA_HEAD_LEN - s_up.head_len;
        if (take > left) {
            take = left;
        }
        memcpy(s_up.head + s_up.head_len, p, take);
        s_up.head_len += take;
        p += take;
        left -= take;
        if (s_up.head_len < PT_OTA_HEAD_LEN) {
            return PT_OK;
        }
        if (pt_ota_check_head(s_up.head, sizeof(s_up.head)) != PT_OK) {
            pt_shell_log("firmware upload refused: the file is not a Pytaichenko version %d firmware", PT_OTA_MAJOR);
            return PT_ERR_NOT_OURS;
        }
        if (esp_ota_begin(s_up.part, OTA_WITH_SEQUENTIAL_WRITES, &s_up.handle) != ESP_OK) {
            pt_shell_log("firmware upload: the update partition could not be opened");
            return PT_ERR_IO;
        }
        s_up.started = true;
        if (esp_ota_write(s_up.handle, s_up.head, sizeof(s_up.head)) != ESP_OK) {
            pt_shell_log("firmware upload: writing to flash failed");
            return PT_ERR_IO;
        }
        s_up.written = sizeof(s_up.head);
    }
    if (left > 0) {
        if (esp_ota_write(s_up.handle, p, left) != ESP_OK) {
            pt_shell_log("firmware upload: writing to flash failed");
            return PT_ERR_IO;
        }
        s_up.written += left;
    }
    return PT_OK;
}

pt_err_t pt_ota_finish(void)
{
    if (!s_up.active) {
        return PT_ERR_INVALID;
    }
    if (!s_up.started) {
        s_up.active = false;
        pt_shell_log("firmware upload refused: the file is too short to be a firmware");
        return PT_ERR_NOT_OURS;
    }
    s_up.active = false;
    if (esp_ota_end(s_up.handle) != ESP_OK) {
        pt_shell_log("firmware upload: the file did not pass the firmware check");
        return PT_ERR_NOT_OURS;
    }
    if (esp_ota_set_boot_partition(s_up.part) != ESP_OK) {
        pt_shell_log("firmware upload: the new firmware could not be selected");
        return PT_ERR_IO;
    }
    pt_shell_log("firmware upload: %u bytes written", (unsigned)s_up.written);
    schedule_restart();
    return PT_OK;
}

void pt_ota_abort(void)
{
    if (s_up.active && s_up.started) {
        esp_ota_abort(s_up.handle);
    }
    s_up.active = false;
    s_up.started = false;
}

static void arm_valid_timer(int64_t after_us)
{
    esp_timer_stop(s_valid_timer);
    esp_timer_start_once(s_valid_timer, (uint64_t)after_us);
}

static void mark_valid(void *arg)
{
    (void)arg;
    if (!pt_wifi_connected()) {
        arm_valid_timer(MARK_VALID_RETRY_US);
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        pt_shell_log("the new firmware works, it is kept");
    } else {
        pt_shell_log("the new firmware could not be marked as working");
    }
}

void pt_ota_mark_valid_later(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    const esp_timer_create_args_t args = {.callback = mark_valid, .name = "ota_valid"};
    if (esp_timer_create(&args, &s_valid_timer) != ESP_OK) {
        return;
    }
    pt_shell_log("this firmware is new; it is kept if Wi-Fi and the page work for 30 seconds");
    arm_valid_timer(MARK_VALID_FIRST_US);
}
