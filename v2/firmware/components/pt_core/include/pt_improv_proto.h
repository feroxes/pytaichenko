#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#define PT_IMPROV_VERSION 1
#define PT_IMPROV_FIRMWARE_NAME "Pytaichenko.Speaker"
#define PT_IMPROV_HEADER_LEN 9
#define PT_IMPROV_FRAME_MAX (PT_IMPROV_HEADER_LEN + 255 + 1)
#define PT_IMPROV_SSID_MAX 32
#define PT_IMPROV_PASSWORD_MAX 64
#define PT_IMPROV_RX_GAP_MS 100

enum {
    PT_IMPROV_TYPE_STATE = 0x01,
    PT_IMPROV_TYPE_ERROR = 0x02,
    PT_IMPROV_TYPE_RPC = 0x03,
    PT_IMPROV_TYPE_RESULT = 0x04
};

enum {
    PT_IMPROV_CMD_WIFI_SETTINGS = 0x01,
    PT_IMPROV_CMD_GET_STATE = 0x02,
    PT_IMPROV_CMD_GET_INFO = 0x03,
    PT_IMPROV_CMD_GET_NETWORKS = 0x04
};

enum {
    PT_IMPROV_STATE_READY = 0x02,
    PT_IMPROV_STATE_PROVISIONING = 0x03,
    PT_IMPROV_STATE_PROVISIONED = 0x04
};

enum {
    PT_IMPROV_ERROR_NONE = 0x00,
    PT_IMPROV_ERROR_INVALID_RPC = 0x01,
    PT_IMPROV_ERROR_UNKNOWN_RPC = 0x02,
    PT_IMPROV_ERROR_UNABLE_TO_CONNECT = 0x03,
    PT_IMPROV_ERROR_UNKNOWN = 0xFF
};

typedef struct {
    uint8_t type;
    uint8_t command;
    char ssid[PT_IMPROV_SSID_MAX + 1];
    char password[PT_IMPROV_PASSWORD_MAX + 1];
} pt_improv_cmd_t;

typedef struct {
    uint8_t buf[PT_IMPROV_FRAME_MAX];
    size_t len;
    uint32_t last_ms;
} pt_improv_rx_t;

pt_err_t pt_improv_parse(const uint8_t *buf, size_t len, pt_improv_cmd_t *out);
size_t pt_improv_build(uint8_t type, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap);
size_t pt_improv_result(uint8_t command, const char *const *strings, size_t count, uint8_t *out, size_t out_cap);
bool pt_improv_feed(pt_improv_rx_t *rx, uint8_t byte, uint32_t now_ms);
void pt_improv_wipe(pt_improv_cmd_t *cmd);
