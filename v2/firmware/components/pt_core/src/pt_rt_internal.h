#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "pt_rt.h"

bool pt_rt_attach(cJSON *parent, const char *key, cJSON *child);
bool pt_rt_add_str(cJSON *parent, const char *key, const char *value);
char *pt_rt_audio_message(const char *type, const int16_t *pcm, size_t n);
const char *pt_rt_json_str(const cJSON *obj, const char *key);
void pt_rt_copy_trunc(char *dst, size_t cap, const char *src);
pt_err_t pt_rt_decode_pcm(const char *b64, pt_rt_event_t *out);
pt_err_t pt_rt_live_dispatch(const char *type, const cJSON *msg, pt_rt_event_t *out);
