#include "pt_improv.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pt_core.h"
#include "pt_improv_proto.h"
#include "pt_shell.h"
#include "pt_wifi.h"

#define IMPROV_UART UART_NUM_0
#define IMPROV_RX_BUF 1024
#define IMPROV_STACK 6144
#define IMPROV_CONNECT_MS 20000
#define IMPROV_NETWORKS 20

static const char *TAG = "improv";

static void send_frame(uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t buf[PT_IMPROV_FRAME_MAX + 2];
    buf[0] = '\n';
    size_t n = pt_improv_build(type, data, len, buf + 1, sizeof(buf) - 2);
    if (n == 0) {
        return;
    }
    buf[n + 1] = '\n';
    uart_write_bytes(IMPROV_UART, buf, n + 2);
}

static void send_byte(uint8_t type, uint8_t value)
{
    send_frame(type, &value, 1);
}

static void send_result(uint8_t command, const char *const *strings, size_t count)
{
    uint8_t data[256];
    size_t n = pt_improv_result(command, strings, count, data, sizeof(data));
    if (n == 0) {
        send_byte(PT_IMPROV_TYPE_ERROR, PT_IMPROV_ERROR_UNKNOWN);
        return;
    }
    send_frame(PT_IMPROV_TYPE_RESULT, data, n);
}

static void send_url(uint8_t command)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s.local/", pt_wifi_name());
    const char *strings[] = {url};
    send_result(command, strings, 1);
}

static uint8_t current_state(void)
{
    return pt_wifi_connected() ? PT_IMPROV_STATE_PROVISIONED : PT_IMPROV_STATE_READY;
}

static void wifi_settings(const pt_improv_cmd_t *cmd)
{
    pt_shell_log("Wi-Fi settings received over USB, connecting");
    send_byte(PT_IMPROV_TYPE_STATE, PT_IMPROV_STATE_PROVISIONING);
    if (pt_wifi_try(cmd->ssid, cmd->password, IMPROV_CONNECT_MS) == PT_OK) {
        send_byte(PT_IMPROV_TYPE_STATE, PT_IMPROV_STATE_PROVISIONED);
        send_url(PT_IMPROV_CMD_WIFI_SETTINGS);
    } else {
        send_byte(PT_IMPROV_TYPE_ERROR, PT_IMPROV_ERROR_UNABLE_TO_CONNECT);
        send_byte(PT_IMPROV_TYPE_STATE, current_state());
    }
}

static void networks(void)
{
    static pt_wifi_net_t found[IMPROV_NETWORKS];
    size_t n = pt_wifi_scan(found, IMPROV_NETWORKS);
    for (size_t i = 0; i < n; i++) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", found[i].rssi);
        const char *strings[] = {found[i].ssid, rssi, found[i].secured ? "YES" : "NO"};
        send_result(PT_IMPROV_CMD_GET_NETWORKS, strings, 3);
    }
    send_result(PT_IMPROV_CMD_GET_NETWORKS, NULL, 0);
}

static void handle(const pt_improv_cmd_t *cmd)
{
    if (cmd->type != PT_IMPROV_TYPE_RPC) {
        return;
    }
    send_byte(PT_IMPROV_TYPE_ERROR, PT_IMPROV_ERROR_NONE);
    switch (cmd->command) {
    case PT_IMPROV_CMD_WIFI_SETTINGS:
        wifi_settings(cmd);
        break;
    case PT_IMPROV_CMD_GET_STATE: {
        uint8_t state = current_state();
        send_byte(PT_IMPROV_TYPE_STATE, state);
        if (state == PT_IMPROV_STATE_PROVISIONED) {
            send_url(PT_IMPROV_CMD_GET_STATE);
        }
        break;
    }
    case PT_IMPROV_CMD_GET_INFO: {
        const char *strings[] = {PT_IMPROV_FIRMWARE_NAME, pt_version(), "ESP32-S3", pt_wifi_name()};
        send_result(PT_IMPROV_CMD_GET_INFO, strings, 4);
        break;
    }
    case PT_IMPROV_CMD_GET_NETWORKS:
        networks();
        break;
    default:
        send_byte(PT_IMPROV_TYPE_ERROR, PT_IMPROV_ERROR_UNKNOWN_RPC);
        break;
    }
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n-- > 0) {
        *v++ = 0;
    }
}

static void improv_task(void *arg)
{
    (void)arg;
    static pt_improv_rx_t rx;
    static pt_improv_cmd_t cmd;
    uint8_t chunk[64];
    for (;;) {
        int n = uart_read_bytes(IMPROV_UART, chunk, 1, portMAX_DELAY);
        if (n <= 0) {
            continue;
        }
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        size_t more = 0;
        if (uart_get_buffered_data_len(IMPROV_UART, &more) == ESP_OK && more > 0) {
            size_t want = more < sizeof(chunk) - 1 ? more : sizeof(chunk) - 1;
            int extra = uart_read_bytes(IMPROV_UART, chunk + 1, want, 0);
            if (extra > 0) {
                n += extra;
            }
        }
        for (int i = 0; i < n; i++) {
            if (!pt_improv_feed(&rx, chunk[i], now_ms)) {
                continue;
            }
            if (pt_improv_parse(rx.buf, rx.len, &cmd) == PT_OK) {
                handle(&cmd);
            } else {
                send_byte(PT_IMPROV_TYPE_ERROR, PT_IMPROV_ERROR_INVALID_RPC);
            }
            pt_improv_wipe(&cmd);
            wipe(rx.buf, sizeof(rx.buf));
            rx.len = 0;
        }
        wipe(chunk, sizeof(chunk));
    }
}

void pt_improv_start(void)
{
    if (!uart_is_driver_installed(IMPROV_UART)) {
        esp_err_t e = uart_driver_install(IMPROV_UART, IMPROV_RX_BUF, 0, 0, NULL, 0);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "uart driver: %s", esp_err_to_name(e));
            pt_shell_log("USB setup (Improv) could not start");
            return;
        }
    }
    uart_vfs_dev_use_driver(IMPROV_UART);
    if (xTaskCreate(improv_task, "improv", IMPROV_STACK, NULL, 5, NULL) != pdPASS) {
        pt_shell_log("USB setup (Improv) could not start");
    }
}
