#pragma once

#include <stddef.h>

#include "pt_core.h"

typedef enum {
    PT_WS_OK,
    PT_WS_BAD_KEY,
    PT_WS_NO_NETWORK,
    PT_WS_REFUSED,
    PT_WS_TIMEOUT,
    PT_WS_REJECTED
} pt_ws_status_t;

pt_err_t pt_ws_url(const char *model, char *out, size_t len);
pt_ws_status_t pt_ws_status_from_http(int code);
