#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PT_FEATURES_CHANNELS 40
#define PT_FEATURES_RATE 16000
#define PT_FEATURES_WINDOW 480
#define PT_FEATURES_STEP 160
#define PT_FEATURES_FFT 512
#define PT_FEATURES_SPECTRUM (PT_FEATURES_FFT / 2 + 1)
#define PT_FEATURES_ENERGY (PT_FEATURES_SPECTRUM + 8)
#define PT_FEATURES_MAX_WEIGHTS 512
#define PT_FEATURES_FFT_CFG_BYTES 4096
#define PT_FEATURES_WDF_LUT (4 * 32 - 3)

typedef struct {
    int16_t coefficients[PT_FEATURES_WINDOW];
    int16_t input[PT_FEATURES_WINDOW];
    size_t input_used;
    int16_t fft_input[PT_FEATURES_FFT];
    int16_t spectrum[2 * PT_FEATURES_SPECTRUM];
    int32_t energy[PT_FEATURES_ENERGY];
    uint64_t fft_cfg[PT_FEATURES_FFT_CFG_BYTES / sizeof(uint64_t)];
    void *fft;
    int start_index;
    int end_index;
    int16_t channel_frequency_starts[PT_FEATURES_CHANNELS + 1];
    int16_t channel_weight_starts[PT_FEATURES_CHANNELS + 1];
    int16_t channel_widths[PT_FEATURES_CHANNELS + 1];
    int16_t weights[PT_FEATURES_MAX_WEIGHTS];
    int16_t unweights[PT_FEATURES_MAX_WEIGHTS];
    uint64_t work[PT_FEATURES_CHANNELS + 1];
    uint32_t signal[PT_FEATURES_CHANNELS];
    uint32_t noise_estimate[PT_FEATURES_CHANNELS];
    uint16_t even_smoothing;
    uint16_t odd_smoothing;
    uint16_t min_signal_remaining;
    int16_t gain_lut[PT_FEATURES_WDF_LUT];
    int32_t snr_shift;
} pt_features_t;

pt_err_t pt_features_init(pt_features_t *f);
void pt_features_reset(pt_features_t *f);
bool pt_features_next(pt_features_t *f, const int16_t **pcm, size_t *n, uint16_t out[PT_FEATURES_CHANNELS]);
int8_t pt_features_quantize(uint16_t value, float scale, int zero_point);

#ifdef __cplusplus
}
#endif
