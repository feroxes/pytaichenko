#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

typedef struct {
    char ssid[33];
    int rssi;
    bool secured;
} pt_wifi_net_t;

const char *pt_wifi_name(void);
pt_err_t pt_wifi_start(void);
bool pt_wifi_connected(void);
void pt_wifi_info(char *ip, size_t ip_len, int *rssi);
pt_err_t pt_wifi_try(const char *ssid, const char *password, uint32_t timeout_ms);
size_t pt_wifi_scan(pt_wifi_net_t *out, size_t cap);
