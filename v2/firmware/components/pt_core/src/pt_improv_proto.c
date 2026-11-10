#include "pt_improv_proto.h"

#include <string.h>

static const uint8_t HEADER[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

static uint8_t checksum(const uint8_t *b, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) {
        s = (uint8_t)(s + b[i]);
    }
    return s;
}

static bool copy_text(char *dst, size_t cap, const uint8_t *src, size_t n)
{
    if (n >= cap || memchr(src, '\0', n) != NULL) {
        return false;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return true;
}

static pt_err_t parse_wifi(const uint8_t *p, size_t n, pt_improv_cmd_t *out)
{
    if (n < 1) {
        return PT_ERR_PARSE;
    }
    size_t ssid_len = p[0];
    size_t ssid_end = 1 + ssid_len;
    if (ssid_len == 0 || ssid_end >= n) {
        return PT_ERR_PARSE;
    }
    size_t pass_len = p[ssid_end];
    size_t pass_end = ssid_end + 1 + pass_len;
    if (pass_end > n) {
        return PT_ERR_PARSE;
    }
    if (!copy_text(out->ssid, sizeof(out->ssid), p + 1, ssid_len) ||
        !copy_text(out->password, sizeof(out->password), p + ssid_end + 1, pass_len)) {
        pt_improv_wipe(out);
        return PT_ERR_PARSE;
    }
    return PT_OK;
}

pt_err_t pt_improv_parse(const uint8_t *buf, size_t len, pt_improv_cmd_t *out)
{
    if (buf == NULL || out == NULL) {
        return PT_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (len < PT_IMPROV_HEADER_LEN + 1 || memcmp(buf, HEADER, sizeof(HEADER)) != 0 ||
        buf[6] != PT_IMPROV_VERSION || (size_t)buf[8] + PT_IMPROV_HEADER_LEN + 1 != len ||
        checksum(buf, len - 1) != buf[len - 1]) {
        return PT_ERR_PARSE;
    }
    out->type = buf[7];
    if (out->type != PT_IMPROV_TYPE_RPC) {
        return PT_OK;
    }
    const uint8_t *data = buf + PT_IMPROV_HEADER_LEN;
    size_t data_len = buf[8];
    if (data_len < 2 || (size_t)data[1] != data_len - 2) {
        return PT_ERR_PARSE;
    }
    out->command = data[0];
    if (out->command == PT_IMPROV_CMD_WIFI_SETTINGS) {
        return parse_wifi(data + 2, data_len - 2, out);
    }
    return PT_OK;
}

size_t pt_improv_build(uint8_t type, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap)
{
    size_t total = PT_IMPROV_HEADER_LEN + len + 1;
    if (out == NULL || len > 255 || (len > 0 && data == NULL) || out_cap < total) {
        return 0;
    }
    memcpy(out, HEADER, sizeof(HEADER));
    out[6] = PT_IMPROV_VERSION;
    out[7] = type;
    out[8] = (uint8_t)len;
    if (len > 0) {
        memcpy(out + PT_IMPROV_HEADER_LEN, data, len);
    }
    out[total - 1] = checksum(out, total - 1);
    return total;
}

size_t pt_improv_result(uint8_t command, const char *const *strings, size_t count, uint8_t *out, size_t out_cap)
{
    if (out == NULL || out_cap < 2 || (count > 0 && strings == NULL)) {
        return 0;
    }
    size_t pos = 2;
    for (size_t i = 0; i < count; i++) {
        size_t n = strings[i] != NULL ? strlen(strings[i]) : 0;
        if (n > 255 || (pos - 2) + 1 + n > 255 || pos + 1 + n > out_cap) {
            return 0;
        }
        out[pos] = (uint8_t)n;
        if (n > 0) {
            memcpy(out + pos + 1, strings[i], n);
        }
        pos += 1 + n;
    }
    out[0] = command;
    out[1] = (uint8_t)(pos - 2);
    return pos;
}

static void restart(pt_improv_rx_t *rx, uint8_t byte)
{
    rx->len = 0;
    if (byte == HEADER[0]) {
        rx->buf[rx->len++] = byte;
    }
}

bool pt_improv_feed(pt_improv_rx_t *rx, uint8_t byte, uint32_t now_ms)
{
    if (rx == NULL) {
        return false;
    }
    if (rx->len > 0 && (uint32_t)(now_ms - rx->last_ms) > PT_IMPROV_RX_GAP_MS) {
        memset(rx->buf, 0, sizeof(rx->buf));
        rx->len = 0;
    }
    rx->last_ms = now_ms;
    if (rx->len >= PT_IMPROV_HEADER_LEN && rx->len == (size_t)rx->buf[8] + PT_IMPROV_HEADER_LEN + 1) {
        rx->len = 0;
    }
    if (rx->len >= PT_IMPROV_FRAME_MAX) {
        rx->len = 0;
    }
    if (rx->len < sizeof(HEADER)) {
        if (byte == HEADER[rx->len]) {
            rx->buf[rx->len++] = byte;
        } else {
            restart(rx, byte);
        }
        return false;
    }
    if (rx->len == 6 && byte != PT_IMPROV_VERSION) {
        restart(rx, byte);
        return false;
    }
    rx->buf[rx->len++] = byte;
    return rx->len >= PT_IMPROV_HEADER_LEN && rx->len == (size_t)rx->buf[8] + PT_IMPROV_HEADER_LEN + 1;
}

void pt_improv_wipe(pt_improv_cmd_t *cmd)
{
    if (cmd != NULL) {
        volatile uint8_t *p = (volatile uint8_t *)cmd;
        for (size_t i = 0; i < sizeof(*cmd); i++) {
            p[i] = 0;
        }
    }
}
