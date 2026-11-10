#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

pt_err_t pt_afe_init(void);
size_t pt_afe_chunk(void);
size_t pt_afe_process(const int16_t *left, const int16_t *right, size_t frames, int16_t *out, size_t cap);
const char *pt_afe_mode(void);
