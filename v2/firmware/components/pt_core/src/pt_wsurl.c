#include "pt_wsurl.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define WS_URL_BASE "wss://api.openai.com/v1/realtime?model="

static bool unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

pt_err_t pt_ws_url(const char *model, char *out, size_t len)
{
    if (model == NULL || model[0] == '\0' || out == NULL || len == 0) {
        return PT_ERR_INVALID;
    }
    static const char hex[] = "0123456789ABCDEF";
    size_t base = strlen(WS_URL_BASE);
    if (len <= base) {
        return PT_ERR_INVALID;
    }
    memcpy(out, WS_URL_BASE, base);
    size_t n = base;
    for (const unsigned char *p = (const unsigned char *)model; *p != '\0'; p++) {
        size_t need = unreserved(*p) ? 1 : 3;
        if (n + need >= len) {
            out[0] = '\0';
            return PT_ERR_INVALID;
        }
        if (need == 1) {
            out[n++] = (char)*p;
        } else {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 0x0F];
        }
    }
    out[n] = '\0';
    return PT_OK;
}

pt_ws_status_t pt_ws_status_from_http(int code)
{
    if (code == 401 || code == 403) {
        return PT_WS_BAD_KEY;
    }
    if (code <= 0) {
        return PT_WS_NO_NETWORK;
    }
    if (code == 101) {
        return PT_WS_OK;
    }
    return PT_WS_REFUSED;
}
