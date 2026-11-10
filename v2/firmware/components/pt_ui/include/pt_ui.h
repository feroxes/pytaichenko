#pragma once

#include <stdbool.h>

#include "pt_core.h"
#include "pt_ring.h"

#define PT_UI_BUTTON_BOOT 0
#define PT_UI_BUTTON_TOP 1

pt_err_t pt_ui_start(void (*on_button)(int which, bool long_press));
void pt_ui_ring(pt_ring_state_t s);
