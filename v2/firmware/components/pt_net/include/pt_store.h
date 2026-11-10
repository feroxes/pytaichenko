#pragma once

#include "pt_core.h"
#include "pt_settings.h"

pt_err_t pt_store_init(void);
pt_err_t pt_store_load(pt_settings_t *s);
pt_err_t pt_store_save(const pt_settings_t *s);
