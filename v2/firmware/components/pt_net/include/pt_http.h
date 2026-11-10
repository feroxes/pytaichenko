#pragma once

#include <stdint.h>

#include "pt_api.h"
#include "pt_core.h"

#define PT_HTTP_MAX_BODY 16384u

pt_err_t pt_http_start(pt_api_t *api);
