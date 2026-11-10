#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PT_WAKE_HOLDOFF_FRAMES 100
#define PT_WAKE_VAD_MEMORY 34
#define PT_WAKE_WINDOW 5
#define PT_WAKE_VAD_CUTOFF 0.5f

typedef struct {
    const uint8_t *data;
    float cutoff;
    int window;
} pt_wake_model_t;

typedef struct {
    uint8_t *data;
    size_t size;
} pt_wake_arena_t;

typedef struct pt_wake_engine pt_wake_engine_t;

size_t pt_wake_engine_size(void);
pt_wake_engine_t *pt_wake_engine_create(void *mem, const pt_wake_model_t *vad, pt_wake_arena_t vad_arena,
                                        pt_wake_arena_t wake_arena);
void pt_wake_engine_destroy(pt_wake_engine_t *e);
pt_err_t pt_wake_engine_load(pt_wake_engine_t *e, const pt_wake_model_t *wake);
void pt_wake_engine_reset(pt_wake_engine_t *e);
bool pt_wake_engine_feed(pt_wake_engine_t *e, const int16_t *pcm, size_t n);
float pt_wake_engine_probability(const pt_wake_engine_t *e);
float pt_wake_engine_raw(const pt_wake_engine_t *e, bool vad);
size_t pt_wake_engine_arena_used(const pt_wake_engine_t *e, bool vad);

#ifdef __cplusplus
}
#endif
