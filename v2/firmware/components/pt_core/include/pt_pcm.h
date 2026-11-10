#pragma once

#include <stddef.h>
#include <stdint.h>

float pt_volume_gain(float volume);
void pt_pcm_apply_gain(int16_t *pcm, size_t n, float gain);
size_t pt_pcm_unpack_i2s32_stereo(const int32_t *in, size_t frames, int16_t *left, int16_t *right);

#define PT_UP2_TAPS 32
#define PT_UP2_DELAY (PT_UP2_TAPS / 2 - 1)

typedef struct {
    int16_t hist[PT_UP2_TAPS];
} pt_up2_t;

void pt_up2_reset(pt_up2_t *s);
size_t pt_up2_run(pt_up2_t *s, const int16_t *in, size_t n, int16_t *out);

typedef struct pt_pcm_queue pt_pcm_queue_t;

pt_pcm_queue_t *pt_pcm_queue_new(size_t cap_samples, void *(*alloc)(size_t));
size_t pt_pcm_queue_push(pt_pcm_queue_t *q, const int16_t *pcm, size_t n);
size_t pt_pcm_queue_pop(pt_pcm_queue_t *q, int16_t *out, size_t n);
size_t pt_pcm_queue_len(const pt_pcm_queue_t *q);
size_t pt_pcm_queue_free(const pt_pcm_queue_t *q);
void pt_pcm_queue_clear(pt_pcm_queue_t *q);
void pt_pcm_queue_delete(pt_pcm_queue_t *q, void (*dealloc)(void *));

#define PT_TEST_SOUND_NOTE 4320
#define PT_TEST_SOUND_LAST 9600
#define PT_TEST_SOUND_SAMPLES (3 * PT_TEST_SOUND_NOTE + PT_TEST_SOUND_LAST)

size_t pt_test_sound(int16_t *out, size_t cap);
