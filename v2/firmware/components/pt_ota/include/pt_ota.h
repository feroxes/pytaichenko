#pragma once

#include <stddef.h>

#include "pt_core.h"
#include "pt_otapick.h"

pt_err_t pt_ota_check(pt_release_t *latest);
pt_err_t pt_ota_from_url(const char *url, const char *version);
pt_err_t pt_ota_begin_upload(void);
pt_err_t pt_ota_write(const void *data, size_t n);
pt_err_t pt_ota_finish(void);
void pt_ota_abort(void);
void pt_ota_mark_valid_later(void);
