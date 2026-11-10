#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PT_WAKE_HEY_JARVIS, PT_WAKE_OKAY_NABU, PT_WAKE_HEY_MYCROFT } pt_wake_word_t;

pt_err_t pt_wake_init(void);
pt_err_t pt_wake_select(const char *wake_word);
const char *pt_wake_current(void);
void pt_wake_reset(void);
bool pt_wake_feed(const int16_t *pcm16k, size_t n);
float pt_wake_last_probability(void);

#ifdef __cplusplus
}
#endif
