#pragma once

#include <stdbool.h>

#include "pt_core.h"
#include "pt_settings.h"

pt_err_t pt_app_start(const pt_settings_t *snapshot);
void pt_app_apply(const pt_settings_t *snapshot);
void pt_app_volume(float v);
void pt_app_sound_test(void);
const char *pt_app_mic_mode(void);
bool pt_app_turn_active(void);
void pt_app_hold(bool on);
pt_err_t pt_app_quiet_for_update(void);
void pt_app_on_password_reset(void (*hook)(void *ctx), void *ctx);
