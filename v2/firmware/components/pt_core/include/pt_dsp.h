#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pt_settings.h"

typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} pt_biquad_t;

void pt_biquad_highpass(pt_biquad_t *f, int rate, float hz, float q);
void pt_biquad_peaking(pt_biquad_t *f, int rate, float hz, float q, float gain_db);
void pt_biquad_run(pt_biquad_t *f, const int16_t *in, int16_t *out, size_t n);

typedef struct {
    pt_biquad_t s[2];
} pt_hp4_t;

void pt_hp4_init(pt_hp4_t *f, int rate, float hz);
void pt_hp4_run(pt_hp4_t *f, const int16_t *in, int16_t *out, size_t n);

typedef struct {
    float gain;
    float knee;
} pt_loudness_t;

void pt_loudness_init(pt_loudness_t *l, float gain_db);
void pt_loudness_run(const pt_loudness_t *l, const int16_t *in, int16_t *out, size_t n);

typedef struct pt_resampler pt_resampler_t;

pt_resampler_t *pt_resampler_new(int in_rate, int out_rate);
size_t pt_resampler_out_cap(const pt_resampler_t *r, size_t n);
size_t pt_resampler_run(pt_resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t cap);
size_t pt_resampler_flush(pt_resampler_t *r, int16_t *out, size_t cap);
void pt_resampler_free(pt_resampler_t *r);

float pt_db_to_gain(float db);
void pt_gain_apply(int16_t *buf, size_t n, float gain);
int pt_peak_abs(const int16_t *buf, size_t n);
void pt_peak_text(int peak, char *out, size_t len);

typedef struct pt_mic_path pt_mic_path_t;

pt_mic_path_t *pt_mic_path_new(int in_rate, int out_rate, const pt_settings_t *s);
size_t pt_mic_path_run(pt_mic_path_t *p, const int16_t *in, size_t n, int16_t *out, size_t cap);
size_t pt_mic_path_out_cap(const pt_mic_path_t *p, size_t n);
void pt_mic_path_summary(const pt_mic_path_t *p, char *out, size_t len);
void pt_mic_path_free(pt_mic_path_t *p);

typedef struct pt_speaker_path pt_speaker_path_t;

pt_speaker_path_t *pt_speaker_path_new(int in_rate, int out_rate, const pt_settings_t *s);
size_t pt_speaker_path_run(pt_speaker_path_t *p, const int16_t *in, size_t n, int16_t *out, size_t cap);
size_t pt_speaker_path_out_cap(const pt_speaker_path_t *p, size_t n);
size_t pt_speaker_path_flush(pt_speaker_path_t *p, int16_t *out, size_t cap);
void pt_speaker_path_free(pt_speaker_path_t *p);
