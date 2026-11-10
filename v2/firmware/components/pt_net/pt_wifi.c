#include "pt_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "pt_shell.h"

#define BIT_IP BIT0
#define BIT_DOWN BIT1
#define RETRY_US (5 * 1000000LL)
#define FALLBACK_US (60 * 1000000LL)
#define SCAN_MAX 32
#define TRY(x)                   \
    do {                         \
        if ((x) != ESP_OK) {     \
            return PT_ERR_INVALID; \
        }                        \
    } while (0)

static char s_name[32];
static esp_netif_t *s_netif;
static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry;
static esp_timer_handle_t s_fallback;
static volatile bool s_busy;

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n-- > 0) {
        *v++ = 0;
    }
}

const char *pt_wifi_name(void)
{
    if (s_name[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_name, sizeof(s_name), "pytaichenko-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return s_name;
}

bool pt_wifi_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & BIT_IP) != 0;
}

static bool has_network(void)
{
    wifi_config_t c = {0};
    bool yes = esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && c.sta.ssid[0] != '\0';
    wipe(&c, sizeof(c));
    return yes;
}

static void schedule_retry(void)
{
    esp_timer_stop(s_retry);
    esp_timer_start_once(s_retry, RETRY_US);
}

static void retry_now(void *arg)
{
    (void)arg;
    if (!s_busy && !pt_wifi_connected() && has_network()) {
        esp_wifi_connect();
    }
}

static void fallback_note(void *arg)
{
    (void)arg;
    if (!pt_wifi_connected()) {
        pt_shell_log("no Wi-Fi connection for 60 s; set up Wi-Fi again from the website over USB");
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        if (has_network()) {
            esp_wifi_connect();
        } else {
            pt_shell_log("no Wi-Fi network set yet; set it up from the website over USB");
        }
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        esp_netif_create_ip6_linklocal(s_netif);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        EventBits_t was = xEventGroupClearBits(s_events, BIT_IP);
        xEventGroupSetBits(s_events, BIT_DOWN);
        if (s_busy) {
            return;
        }
        if ((was & BIT_IP) != 0) {
            const wifi_event_sta_disconnected_t *d = data;
            pt_shell_log("Wi-Fi connection lost (reason %d), reconnecting", d != NULL ? d->reason : 0);
            esp_timer_stop(s_fallback);
            esp_timer_start_once(s_fallback, FALLBACK_US);
        }
        schedule_retry();
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *e = data;
    xEventGroupClearBits(s_events, BIT_DOWN);
    xEventGroupSetBits(s_events, BIT_IP);
    esp_timer_stop(s_fallback);
    wifi_ap_record_t ap;
    int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    pt_shell_log("Wi-Fi connected, address " IPSTR ", signal %d dBm", IP2STR(&e->ip_info.ip), rssi);
}

pt_err_t pt_wifi_start(void)
{
    pt_wifi_name();
    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return PT_ERR_NOMEM;
    }
    const esp_timer_create_args_t retry = {.callback = retry_now, .name = "wifi_retry"};
    const esp_timer_create_args_t fallback = {.callback = fallback_note, .name = "wifi_fallback"};
    TRY(esp_timer_create(&retry, &s_retry));
    TRY(esp_timer_create(&fallback, &s_fallback));
    TRY(esp_netif_init());
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return PT_ERR_INVALID;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    if (s_netif == NULL) {
        return PT_ERR_NOMEM;
    }
    TRY(esp_netif_set_hostname(s_netif, s_name));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    TRY(esp_wifi_init(&cfg));
    TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    TRY(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, NULL));
    TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    TRY(esp_timer_start_once(s_fallback, FALLBACK_US));
    TRY(esp_wifi_start());
    TRY(esp_wifi_set_ps(WIFI_PS_NONE));
    return PT_OK;
}

void pt_wifi_info(char *ip, size_t ip_len, int *rssi)
{
    if (ip != NULL && ip_len > 0) {
        ip[0] = '\0';
    }
    if (rssi != NULL) {
        *rssi = 0;
    }
    if (!pt_wifi_connected()) {
        return;
    }
    esp_netif_ip_info_t info;
    if (ip != NULL && ip_len > 0 && esp_netif_get_ip_info(s_netif, &info) == ESP_OK) {
        snprintf(ip, ip_len, IPSTR, IP2STR(&info.ip));
    }
    wifi_ap_record_t ap;
    if (rssi != NULL && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        *rssi = ap.rssi;
    }
}

static void quiet_disconnect(void)
{
    xEventGroupClearBits(s_events, BIT_DOWN);
    if (esp_wifi_disconnect() == ESP_OK) {
        xEventGroupWaitBits(s_events, BIT_DOWN, pdFALSE, pdTRUE, pdMS_TO_TICKS(500));
    }
}

static bool connect_within(uint32_t timeout_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        int64_t left_ms = (end - esp_timer_get_time()) / 1000;
        if (left_ms <= 0) {
            return false;
        }
        xEventGroupClearBits(s_events, BIT_DOWN | BIT_IP);
        if (esp_wifi_connect() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        EventBits_t b = xEventGroupWaitBits(s_events, BIT_IP | BIT_DOWN, pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS((uint32_t)left_ms) + 1);
        if ((b & BIT_IP) != 0) {
            return true;
        }
        if ((b & BIT_DOWN) == 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

pt_err_t pt_wifi_try(const char *ssid, const char *password, uint32_t timeout_ms)
{
    size_t ssid_len = ssid != NULL ? strlen(ssid) : 0;
    size_t pass_len = password != NULL ? strlen(password) : 0;
    if (s_events == NULL || ssid_len == 0 || ssid_len > 32 || pass_len > 64) {
        return PT_ERR_INVALID;
    }
    wifi_config_t old = {0};
    wifi_config_t cfg = {0};
    esp_wifi_get_config(WIFI_IF_STA, &old);
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    if (pass_len > 0) {
        memcpy(cfg.sta.password, password, pass_len);
    }
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    s_busy = true;
    esp_timer_stop(s_retry);
    quiet_disconnect();
    bool ok = esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK && connect_within(timeout_ms);
    if (!ok) {
        quiet_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &old);
    }
    bool had_old = old.sta.ssid[0] != '\0';
    wipe(&cfg, sizeof(cfg));
    wipe(&old, sizeof(old));
    s_busy = false;
    if (ok) {
        esp_timer_stop(s_fallback);
        pt_shell_log("new Wi-Fi settings saved");
    } else {
        pt_shell_log("could not connect to the new Wi-Fi network; previous settings kept");
        if (had_old) {
            retry_now(NULL);
        }
    }
    return ok ? PT_OK : PT_ERR_INVALID;
}

static size_t merge(const wifi_ap_record_t *recs, size_t n, pt_wifi_net_t *out, size_t cap)
{
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        size_t j = 0;
        while (j < count && strcmp(out[j].ssid, ssid) != 0) {
            j++;
        }
        if (j < count) {
            if (recs[i].rssi > out[j].rssi) {
                out[j].rssi = recs[i].rssi;
            }
            continue;
        }
        if (count == cap) {
            continue;
        }
        snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", ssid);
        out[count].rssi = recs[i].rssi;
        out[count].secured = recs[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    return count;
}

size_t pt_wifi_scan(pt_wifi_net_t *out, size_t cap)
{
    if (out == NULL || cap == 0 || s_events == NULL) {
        return 0;
    }
    bool connected = pt_wifi_connected();
    if (!connected) {
        s_busy = true;
        esp_timer_stop(s_retry);
        quiet_disconnect();
    }
    size_t count = 0;
    wifi_scan_config_t sc = {0};
    uint16_t total = 0;
    if (esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_num(&total) == ESP_OK && total > 0) {
        uint16_t want = total > SCAN_MAX ? SCAN_MAX : total;
        wifi_ap_record_t *recs = calloc(want, sizeof(*recs));
        if (recs != NULL && esp_wifi_scan_get_ap_records(&want, recs) == ESP_OK) {
            count = merge(recs, want, out, cap);
        }
        free(recs);
    }
    esp_wifi_clear_ap_list();
    if (!connected) {
        s_busy = false;
        retry_now(NULL);
    }
    return count;
}
