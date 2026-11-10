#include "pt_features.h"

#include <math.h>
#include <string.h>

#include "tools/kiss_fftr.h"

#define WINDOW_BITS 12
#define FILTERBANK_BITS 12
#define LOWER_BAND_HZ 125.0f
#define UPPER_BAND_HZ 7500.0f
#define BLOCK 4
#define ALIGN 2
#define SMOOTHING_BITS 10
#define NOISE_BITS 14
#define EVEN_SMOOTHING 0.025f
#define ODD_SMOOTHING 0.06f
#define MIN_SIGNAL_REMAINING 0.05f
#define PCAN_STRENGTH 0.95f
#define PCAN_OFFSET 80.0f
#define PCAN_GAIN_BITS 21
#define PCAN_SNR_BITS 12
#define PCAN_OUTPUT_BITS 6
#define WDF_BITS 32
#define LOG_SCALE_SHIFT 6
#define LOG_SEGMENTS_LOG2 7
#define LOG_SCALE 65536
#define LOG_SCALE_LOG2 16
#define LOG_COEFF 45426
#define FEATURE_SCALE 0.0390625

static const uint16_t LOG_LUT[] = {
    0,    224,  442,  654,  861,  1063, 1259, 1450, 1636, 1817, 1992, 2163, 2329, 2490, 2646, 2797, 2944,
    3087, 3224, 3358, 3487, 3611, 3732, 3848, 3960, 4068, 4172, 4272, 4368, 4460, 4549, 4633, 4714, 4791,
    4864, 4934, 5001, 5063, 5123, 5178, 5231, 5280, 5326, 5368, 5408, 5444, 5477, 5507, 5533, 5557, 5578,
    5595, 5610, 5622, 5631, 5637, 5640, 5641, 5638, 5633, 5626, 5615, 5602, 5586, 5568, 5547, 5524, 5498,
    5470, 5439, 5406, 5370, 5332, 5291, 5249, 5203, 5156, 5106, 5054, 5000, 4944, 4885, 4825, 4762, 4697,
    4630, 4561, 4490, 4416, 4341, 4264, 4184, 4103, 4020, 3935, 3848, 3759, 3668, 3575, 3481, 3384, 3286,
    3186, 3084, 2981, 2875, 2768, 2659, 2549, 2437, 2323, 2207, 2090, 1971, 1851, 1729, 1605, 1480, 1353,
    1224, 1094, 963,  830,  695,  559,  421,  282,  142,  0,    0};

static int msb32(uint32_t n)
{
    int bits = 0;
    while (n) {
        bits++;
        n >>= 1;
    }
    return bits;
}

static int msb64(uint64_t n)
{
    int bits = 0;
    while (n) {
        bits++;
        n >>= 1;
    }
    return bits;
}

static int correction_bits(void)
{
    return msb32(PT_FEATURES_FFT) - 1 - FILTERBANK_BITS / 2;
}

static float to_mel(float hz)
{
    return 1127.0f * log1pf(hz / 700.0f);
}

static void init_window(pt_features_t *f)
{
    const float arg = 3.14159265358979323846f * 2.0f / (float)PT_FEATURES_WINDOW;
    for (int i = 0; i < PT_FEATURES_WINDOW; i++) {
        float v = 0.5f - 0.5f * cosf(arg * ((float)i + 0.5f));
        f->coefficients[i] = (int16_t)floorf(v * (float)(1 << WINDOW_BITS) + 0.5f);
    }
}

static pt_err_t init_filterbank(pt_features_t *f)
{
    enum { N = PT_FEATURES_CHANNELS + 1 };
    float centers[N];
    int16_t starts[N];
    int16_t widths[N];
    const float mel_low = to_mel(LOWER_BAND_HZ);
    const float spacing = (to_mel(UPPER_BAND_HZ) - mel_low) / (float)N;
    for (int i = 0; i < N; i++) centers[i] = mel_low + spacing * (float)(i + 1);

    const float hz_per_bin = 0.5f * (float)PT_FEATURES_RATE / ((float)PT_FEATURES_SPECTRUM - 1.0f);
    f->start_index = (int)(1.5f + LOWER_BAND_HZ / hz_per_bin);
    f->end_index = 0;

    int bin_start = f->start_index;
    int weight_start = 0;
    bool zeros = false;
    for (int ch = 0; ch < N; ch++) {
        int bin = bin_start;
        while (to_mel((float)bin * hz_per_bin) <= centers[ch]) bin++;
        int width = bin - bin_start;
        starts[ch] = (int16_t)bin_start;
        widths[ch] = (int16_t)width;
        if (width == 0) {
            f->channel_frequency_starts[ch] = 0;
            f->channel_weight_starts[ch] = 0;
            f->channel_widths[ch] = BLOCK;
            if (!zeros) {
                zeros = true;
                for (int j = 0; j < ch; j++) f->channel_weight_starts[j] += BLOCK;
                weight_start += BLOCK;
            }
        } else {
            int aligned = (bin_start / ALIGN) * ALIGN;
            int padded = ((bin_start - aligned + width - 1) / BLOCK + 1) * BLOCK;
            f->channel_frequency_starts[ch] = (int16_t)aligned;
            f->channel_weight_starts[ch] = (int16_t)weight_start;
            f->channel_widths[ch] = (int16_t)padded;
            weight_start += padded;
        }
        bin_start = bin;
    }
    if (weight_start > PT_FEATURES_MAX_WEIGHTS) return PT_ERR_NOMEM;

    for (int ch = 0; ch < N; ch++) {
        int bin = starts[ch];
        int offset = bin - f->channel_frequency_starts[ch];
        float below = ch == 0 ? mel_low : centers[ch - 1];
        for (int j = 0; j < widths[ch]; j++, bin++) {
            float w = (centers[ch] - to_mel((float)bin * hz_per_bin)) / (centers[ch] - below);
            int at = f->channel_weight_starts[ch] + offset + j;
            f->weights[at] = (int16_t)floorf(w * (float)(1 << FILTERBANK_BITS) + 0.5f);
            f->unweights[at] = (int16_t)floorf((1.0f - w) * (float)(1 << FILTERBANK_BITS) + 0.5f);
        }
        if (bin > f->end_index) f->end_index = bin;
    }
    for (int ch = 0; ch < N; ch++) {
        if (f->channel_frequency_starts[ch] + f->channel_widths[ch] > PT_FEATURES_ENERGY) return PT_ERR_INVALID;
    }
    return f->end_index < PT_FEATURES_SPECTRUM ? PT_OK : PT_ERR_INVALID;
}

static int16_t pcan_gain(int32_t input_bits, uint32_t x)
{
    float xf = (float)x / (float)((uint32_t)1 << input_bits);
    float gain = (float)((uint32_t)1 << PCAN_GAIN_BITS) * powf(xf + PCAN_OFFSET, -PCAN_STRENGTH);
    if (gain > 32767.0f) return 32767;
    return (int16_t)(gain + 0.5f);
}

static void init_pcan(pt_features_t *f)
{
    f->snr_shift = PCAN_GAIN_BITS - correction_bits() - PCAN_SNR_BITS;
    int32_t input_bits = SMOOTHING_BITS - correction_bits();
    int16_t *lut = f->gain_lut;
    lut[0] = pcan_gain(input_bits, 0);
    lut[1] = pcan_gain(input_bits, 1);
    for (int interval = 2; interval <= WDF_BITS; interval++) {
        uint32_t x0 = (uint32_t)1 << (interval - 1);
        uint32_t x1 = x0 + (x0 >> 1);
        uint32_t x2 = interval == WDF_BITS ? x0 + (x0 - 1) : 2 * x0;
        int16_t y0 = pcan_gain(input_bits, x0);
        int16_t y1 = pcan_gain(input_bits, x1);
        int16_t y2 = pcan_gain(input_bits, x2);
        int32_t d1 = (int32_t)y1 - y0;
        int32_t d2 = (int32_t)y2 - y0;
        int32_t a1 = 4 * d1 - d2;
        int32_t a2 = d2 - a1;
        lut[4 * interval - 6] = y0;
        lut[4 * interval - 5] = (int16_t)a1;
        lut[4 * interval - 4] = (int16_t)a2;
    }
}

pt_err_t pt_features_init(pt_features_t *f)
{
    if (!f) return PT_ERR_INVALID;
    memset(f, 0, sizeof *f);
    init_window(f);
    size_t size = sizeof f->fft_cfg;
    f->fft = kiss_fftr_alloc(PT_FEATURES_FFT, 0, f->fft_cfg, &size);
    if (!f->fft) return PT_ERR_NOMEM;
    pt_err_t err = init_filterbank(f);
    if (err != PT_OK) return err;
    f->even_smoothing = (uint16_t)(EVEN_SMOOTHING * (float)(1 << NOISE_BITS));
    f->odd_smoothing = (uint16_t)(ODD_SMOOTHING * (float)(1 << NOISE_BITS));
    f->min_signal_remaining = (uint16_t)(MIN_SIGNAL_REMAINING * (float)(1 << NOISE_BITS));
    init_pcan(f);
    return PT_OK;
}

void pt_features_reset(pt_features_t *f)
{
    memset(f->input, 0, sizeof f->input);
    f->input_used = 0;
    memset(f->noise_estimate, 0, sizeof f->noise_estimate);
}

static uint16_t sqrt32(uint32_t num)
{
    if (num == 0) return 0;
    uint32_t res = 0;
    int top = (32 - msb32(num)) | 1;
    uint32_t bit = 1U << (31 - top);
    int steps = (31 - top) / 2 + 1;
    while (steps--) {
        if (num >= res + bit) {
            num -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    if (num > res && res != 0xFFFF) res++;
    return (uint16_t)res;
}

static uint32_t sqrt64(uint64_t num)
{
    if ((num >> 32) == 0) return sqrt32((uint32_t)num);
    uint64_t res = 0;
    int top = (64 - msb64(num)) | 1;
    uint64_t bit = 1ULL << (63 - top);
    int steps = (63 - top) / 2 + 1;
    while (steps--) {
        if (num >= res + bit) {
            num -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    if (num > res && res != 0xFFFFFFFFULL) res++;
    return (uint32_t)res;
}

static int window(pt_features_t *f)
{
    int16_t peak = 0;
    for (int i = 0; i < PT_FEATURES_WINDOW; i++) {
        int16_t v = (int16_t)(((int32_t)f->input[i] * f->coefficients[i]) >> WINDOW_BITS);
        f->fft_input[i] = v;
        if (v < 0) v = (int16_t)-v;
        if (v > peak) peak = v;
    }
    memmove(f->input, f->input + PT_FEATURES_STEP, sizeof(int16_t) * (PT_FEATURES_WINDOW - PT_FEATURES_STEP));
    f->input_used -= PT_FEATURES_STEP;
    int shift = 15 - msb32((uint32_t)peak);
    for (int i = 0; i < PT_FEATURES_WINDOW; i++) f->fft_input[i] = (int16_t)((uint16_t)f->fft_input[i] << shift);
    memset(f->fft_input + PT_FEATURES_WINDOW, 0, sizeof(int16_t) * (PT_FEATURES_FFT - PT_FEATURES_WINDOW));
    return shift;
}

static void filterbank(pt_features_t *f, int shift)
{
    const kiss_fft_cpx *spectrum = (const kiss_fft_cpx *)f->spectrum;
    for (int i = f->start_index; i < f->end_index; i++) {
        int32_t re = spectrum[i].r;
        int32_t im = spectrum[i].i;
        f->energy[i] = (int32_t)((uint32_t)(re * re) + (uint32_t)(im * im));
    }
    uint64_t weighted = 0;
    uint64_t unweighted = 0;
    for (int ch = 0; ch <= PT_FEATURES_CHANNELS; ch++) {
        const int32_t *mag = f->energy + f->channel_frequency_starts[ch];
        const int16_t *w = f->weights + f->channel_weight_starts[ch];
        const int16_t *u = f->unweights + f->channel_weight_starts[ch];
        for (int j = 0; j < f->channel_widths[ch]; j++) {
            weighted += (uint64_t)w[j] * (uint64_t)mag[j];
            unweighted += (uint64_t)u[j] * (uint64_t)mag[j];
        }
        f->work[ch] = weighted;
        weighted = unweighted;
        unweighted = 0;
    }
    for (int ch = 0; ch < PT_FEATURES_CHANNELS; ch++) f->signal[ch] = sqrt64(f->work[ch + 1]) >> shift;
}

static void reduce_noise(pt_features_t *f)
{
    for (int i = 0; i < PT_FEATURES_CHANNELS; i++) {
        uint32_t smoothing = (i & 1) == 0 ? f->even_smoothing : f->odd_smoothing;
        uint32_t keep = (1U << NOISE_BITS) - smoothing;
        uint32_t scaled = f->signal[i] << SMOOTHING_BITS;
        uint32_t estimate =
            (uint32_t)(((uint64_t)scaled * smoothing + (uint64_t)f->noise_estimate[i] * keep) >> NOISE_BITS);
        f->noise_estimate[i] = estimate;
        if (estimate > scaled) estimate = scaled;
        uint32_t floor_value = (uint32_t)(((uint64_t)f->signal[i] * f->min_signal_remaining) >> NOISE_BITS);
        uint32_t left = (scaled - estimate) >> SMOOTHING_BITS;
        f->signal[i] = left > floor_value ? left : floor_value;
    }
}

static int16_t wide_dynamic(uint32_t x, const int16_t *lut)
{
    if (x <= 2) return lut[x];
    int interval = msb32(x);
    lut += 4 * interval - 6;
    int16_t frac = (int16_t)((interval < 11 ? x << (11 - interval) : x >> (interval - 11)) & 0x3FF);
    int32_t r = ((int32_t)lut[2] * frac) >> 5;
    r += (int32_t)((uint32_t)lut[1] << 5);
    r *= frac;
    r = (r + (1 << 14)) >> 15;
    r += lut[0];
    return (int16_t)r;
}

static uint32_t shrink(uint32_t x)
{
    if (x < (2U << PCAN_SNR_BITS)) return (x * x) >> (2 + 2 * PCAN_SNR_BITS - PCAN_OUTPUT_BITS);
    return (x >> (PCAN_SNR_BITS - PCAN_OUTPUT_BITS)) - (1U << PCAN_OUTPUT_BITS);
}

static void pcan(pt_features_t *f)
{
    for (int i = 0; i < PT_FEATURES_CHANNELS; i++) {
        uint32_t gain = (uint32_t)(int32_t)wide_dynamic(f->noise_estimate[i], f->gain_lut);
        uint32_t snr = (uint32_t)(((uint64_t)f->signal[i] * gain) >> f->snr_shift);
        f->signal[i] = shrink(snr);
    }
}

static uint32_t log2_fraction(uint32_t x, uint32_t log2x)
{
    int32_t frac = (int32_t)(x - (1U << log2x));
    if (log2x < LOG_SCALE_LOG2) {
        frac = (int32_t)((uint32_t)frac << (LOG_SCALE_LOG2 - log2x));
    } else {
        frac >>= log2x - LOG_SCALE_LOG2;
    }
    uint32_t seg = (uint32_t)frac >> (LOG_SCALE_LOG2 - LOG_SEGMENTS_LOG2);
    uint32_t unit = (1U << LOG_SCALE_LOG2) >> LOG_SEGMENTS_LOG2;
    int32_t c0 = LOG_LUT[seg];
    int32_t c1 = LOG_LUT[seg + 1];
    int32_t base = (int32_t)(unit * seg);
    int32_t rel = ((c1 - c0) * (frac - base)) >> LOG_SCALE_LOG2;
    return (uint32_t)(frac + c0 + rel);
}

static uint32_t log_scaled(uint32_t x)
{
    uint32_t integer = (uint32_t)msb32(x) - 1;
    uint32_t log2 = (integer << LOG_SCALE_LOG2) + log2_fraction(x, integer);
    uint32_t round = LOG_SCALE / 2;
    uint32_t loge = (uint32_t)(((uint64_t)LOG_COEFF * log2 + round) >> LOG_SCALE_LOG2);
    return ((loge << LOG_SCALE_SHIFT) + round) >> LOG_SCALE_LOG2;
}

static void log_scale(const pt_features_t *f, uint16_t out[PT_FEATURES_CHANNELS])
{
    int bits = correction_bits();
    for (int i = 0; i < PT_FEATURES_CHANNELS; i++) {
        uint32_t v = f->signal[i];
        v = bits < 0 ? v >> -bits : v << bits;
        v = v > 1 ? log_scaled(v) : 0;
        out[i] = (uint16_t)(v < 0xFFFF ? v : 0xFFFF);
    }
}

bool pt_features_next(pt_features_t *f, const int16_t **pcm, size_t *n, uint16_t out[PT_FEATURES_CHANNELS])
{
    size_t take = PT_FEATURES_WINDOW - f->input_used;
    if (take > *n) take = *n;
    memcpy(f->input + f->input_used, *pcm, take * sizeof(int16_t));
    f->input_used += take;
    *pcm += take;
    *n -= take;
    if (f->input_used < PT_FEATURES_WINDOW) return false;
    int shift = window(f);
    kiss_fftr((kiss_fftr_cfg)f->fft, f->fft_input, (kiss_fft_cpx *)f->spectrum);
    filterbank(f, shift);
    reduce_noise(f);
    pcan(f);
    log_scale(f, out);
    return true;
}

int8_t pt_features_quantize(uint16_t value, float scale, int zero_point)
{
    double q = nearbyint((double)value * FEATURE_SCALE / (double)scale + (double)zero_point);
    if (q < -128.0) return -128;
    if (q > 127.0) return 127;
    return (int8_t)q;
}
