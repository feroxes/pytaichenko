#include "unity.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pt_dsp.h"
#include "pt_settings.h"

#define PT_PI 3.14159265358979323846
#define TOLERANCE 4
#define SECOND16 16000
#define SECOND24 24000

void setUp(void) {}
void tearDown(void) {}

static int16_t *load(const char *name, size_t *count)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.raw", PT_GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *data = malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_INT((int)size, (int)fread(data, 1, (size_t)size, f));
    fclose(f);
    *count = (size_t)size / 2;
    return data;
}

static int max_deviation(const int16_t *a, const int16_t *b, size_t n)
{
    int worst = 0;
    for (size_t i = 0; i < n; i++) {
        int d = abs((int)a[i] - (int)b[i]);
        if (d > worst) worst = d;
    }
    return worst;
}

static void expect_matches(const int16_t *got, size_t got_n, const char *golden_name)
{
    size_t want_n;
    int16_t *want = load(golden_name, &want_n);
    TEST_ASSERT_EQUAL_UINT(want_n, got_n);
    int worst = max_deviation(got, want, want_n);
    printf("%s: max deviation %d\n", golden_name, worst);
    TEST_ASSERT_LESS_OR_EQUAL_INT(TOLERANCE, worst);
    free(want);
}

static void highpass_case(const char *in_name, const char *golden_name)
{
    size_t n;
    int16_t *in = load(in_name, &n);
    int16_t *out = malloc(n * sizeof *out);
    pt_hp4_t hp;
    pt_hp4_init(&hp, 16000, 250.0f);
    for (size_t at = 0; at < n; at += 160) {
        size_t k = n - at < 160 ? n - at : 160;
        pt_hp4_run(&hp, in + at, out + at, k);
    }
    expect_matches(out, n, golden_name);
    free(in);
    free(out);
}

static void test_highpass_matches_hub(void)
{
    highpass_case("in_sine16", "hp_sine16");
    highpass_case("in_noise16", "hp_noise16");
}

static void loudness_case(const char *in_name, const char *golden_name)
{
    size_t n;
    int16_t *in = load(in_name, &n);
    int16_t *out = malloc(n * sizeof *out);
    pt_hp4_t hp;
    pt_loudness_t loud;
    pt_hp4_init(&hp, 16000, 150.0f);
    pt_loudness_init(&loud, 6.0f);
    pt_hp4_run(&hp, in, out, n);
    pt_loudness_run(&loud, out, out, n);
    expect_matches(out, n, golden_name);
    free(in);
    free(out);
}

static void test_loudness_matches_hub(void)
{
    loudness_case("in_sine16", "loud_sine16");
    loudness_case("in_noise16", "loud_noise16");
}

static void resample_case(const char *in_name, int in_rate, int out_rate, size_t chunk, const char *golden_name)
{
    size_t n;
    int16_t *in = load(in_name, &n);
    size_t cap = n * (size_t)out_rate / (size_t)in_rate + 4096;
    int16_t *out = calloc(cap, sizeof *out);
    pt_resampler_t *r = pt_resampler_new(in_rate, out_rate);
    TEST_ASSERT_NOT_NULL(r);
    size_t total = 0;
    for (size_t at = 0; at < n; at += chunk) {
        size_t k = n - at < chunk ? n - at : chunk;
        total += pt_resampler_run(r, in + at, k, out + total, cap - total);
    }
    total += pt_resampler_flush(r, out + total, cap - total);
    expect_matches(out, total, golden_name);
    pt_resampler_free(r);
    free(in);
    free(out);
}

static void test_resample_16_to_24_matches_hub(void)
{
    resample_case("in_sine16", 16000, 24000, 320, "up_sine16");
    resample_case("in_noise16", 16000, 24000, 320, "up_noise16");
    resample_case("in_noise16", 16000, 24000, 7, "up_noise16");
    resample_case("in_noise16", 16000, 24000, 16000, "up_noise16");
}

static void test_resample_24_to_16_matches_hub(void)
{
    resample_case("in_sine24", 24000, 16000, 480, "down_sine24");
    resample_case("in_noise24", 24000, 16000, 480, "down_noise24");
    resample_case("in_noise24", 24000, 16000, 11, "down_noise24");
    resample_case("in_noise24", 24000, 16000, 24000, "down_noise24");
}

static void test_resample_same_rate_is_copy(void)
{
    int16_t in[300], out[300];
    for (int i = 0; i < 300; i++) in[i] = (int16_t)(i * 37 - 5000);
    pt_resampler_t *r = pt_resampler_new(16000, 16000);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT(300, pt_resampler_run(r, in, 300, out, 300));
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 300);
    TEST_ASSERT_EQUAL_UINT(0, pt_resampler_flush(r, out, 300));
    pt_resampler_free(r);
}

static void test_resample_rejects_unsupported_ratio(void)
{
    TEST_ASSERT_NULL(pt_resampler_new(16000, 44100));
    TEST_ASSERT_NULL(pt_resampler_new(0, 16000));
}

static pt_settings_t *new_settings(void)
{
    pt_settings_t *s = calloc(1, sizeof *s);
    TEST_ASSERT_NOT_NULL(s);
    pt_settings_defaults(s, "uk");
    s->timing.mic_holdoff_s = 0.4f;
    s->sound.mic_gain_db = 0.0f;
    return s;
}

static void fill_sine(int16_t *buf, size_t n, int rate, float hz, float amplitude)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (int16_t)lrintf(amplitude * sinf(2.0f * (float)PT_PI * hz * (float)i / (float)rate));
}

static float rms(const int16_t *buf, size_t n)
{
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)buf[i] * buf[i];
    return (float)sqrt(sum / (double)n);
}

static void test_mic_path_drops_holdoff(void)
{
    pt_settings_t *s = new_settings();
    pt_mic_path_t *p = pt_mic_path_new(16000, 24000, s);
    TEST_ASSERT_NOT_NULL(p);
    static int16_t in[320];
    static int16_t out[1024];
    for (int i = 0; i < 320; i++) in[i] = 3000;
    size_t emitted = 0;
    for (int chunk = 0; chunk < 20; chunk++) emitted += pt_mic_path_run(p, in, 320, out, 1024);
    TEST_ASSERT_EQUAL_UINT(0, emitted);
    for (int chunk = 0; chunk < 5; chunk++) emitted += pt_mic_path_run(p, in, 320, out, 1024);
    TEST_ASSERT_TRUE(emitted > 5 * 480 - 250);
    TEST_ASSERT_TRUE(emitted <= 5 * 480);
    pt_mic_path_free(p);
    free(s);
}

static void test_mic_path_drops_holdoff_inside_a_chunk(void)
{
    pt_settings_t *s = new_settings();
    s->timing.mic_holdoff_s = 0.5f;
    pt_mic_path_t *p = pt_mic_path_new(16000, 16000, s);
    static int16_t in[16000];
    static int16_t out[16000];
    for (int i = 0; i < 16000; i++) in[i] = 7;
    TEST_ASSERT_EQUAL_UINT(8000, pt_mic_path_run(p, in, 16000, out, 16000));
    pt_mic_path_free(p);
    free(s);
}

static void test_mic_summary_silence_and_peak(void)
{
    pt_settings_t *s = new_settings();
    s->timing.mic_holdoff_s = 0.0f;
    pt_mic_path_t *p = pt_mic_path_new(16000, 24000, s);
    char text[96];
    static int16_t in[16000];
    static int16_t out[32000];

    pt_mic_path_summary(p, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("0.00 s, peak 0 (silence)", text);

    memset(in, 0, sizeof in);
    pt_mic_path_run(p, in, 16000, out, 32000);
    pt_mic_path_summary(p, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("1.00 s, peak 0 (silence)", text);

    fill_sine(in, 16000, 16000, 1000.0f, 10000.0f);
    pt_mic_path_run(p, in, 8000, out, 32000);
    pt_mic_path_summary(p, text, sizeof text);
    int seconds_x100 = 0, peak = 0;
    float db = 0;
    TEST_ASSERT_EQUAL_INT(3, sscanf(text, "%d.%*d s, peak %d (%f dBFS)", &seconds_x100, &peak, &db));
    TEST_ASSERT_TRUE(strstr(text, "1.50 s, peak ") == text);
    TEST_ASSERT_INT_WITHIN(1500, 10000, peak);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f * log10f((float)peak / 32768.0f), db);
    pt_mic_path_free(p);
    free(s);
}

static void test_mic_gain_scales_signal(void)
{
    static int16_t in[1000], out[1000];
    fill_sine(in, 1000, 16000, 1000.0f, 3000.0f);
    memcpy(out, in, sizeof(in));
    pt_gain_apply(out, 1000, pt_db_to_gain(6.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.995f, rms(out, 1000) / rms(in, 1000));
    memcpy(out, in, sizeof(in));
    pt_gain_apply(out, 1000, pt_db_to_gain(0.0f));
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 1000);
    int16_t loud[4] = {20000, -20000, 32767, -32768};
    pt_gain_apply(loud, 4, pt_db_to_gain(12.0f));
    TEST_ASSERT_EQUAL_INT16(32767, loud[0]);
    TEST_ASSERT_EQUAL_INT16(-32768, loud[1]);
    TEST_ASSERT_EQUAL_INT16(32767, loud[2]);
    TEST_ASSERT_EQUAL_INT16(-32768, loud[3]);
    pt_gain_apply(NULL, 4, 2.0f);
}

static void test_peak_of_raw_channels(void)
{
    int16_t x[5] = {10, -300, 200, 0, 299};
    TEST_ASSERT_EQUAL_INT(300, pt_peak_abs(x, 5));
    int16_t low[1] = {-32768};
    TEST_ASSERT_EQUAL_INT(32768, pt_peak_abs(low, 1));
    TEST_ASSERT_EQUAL_INT(0, pt_peak_abs(NULL, 3));
    char text[40];
    pt_peak_text(0, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("peak 0 (silence)", text);
    pt_peak_text(16384, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("peak 16384 (-6.0 dBFS)", text);
    pt_peak_text(32768, text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("peak 32768 (0.0 dBFS)", text);
}

static void test_mic_path_leaves_the_gain_to_the_shell(void)
{
    pt_settings_t *s = new_settings();
    s->timing.mic_holdoff_s = 0.0f;
    static int16_t in[16000], out_flat[16000], out_gain[16000];
    fill_sine(in, 16000, 16000, 1000.0f, 3000.0f);
    pt_mic_path_t *a = pt_mic_path_new(16000, 16000, s);
    pt_mic_path_run(a, in, 16000, out_flat, 16000);
    s->sound.mic_gain_db = 12.0f;
    pt_mic_path_t *b = pt_mic_path_new(16000, 16000, s);
    pt_mic_path_run(b, in, 16000, out_gain, 16000);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_flat, out_gain, 16000);
    pt_mic_path_free(a);
    pt_mic_path_free(b);
    free(s);
}

static const size_t CAP_STEPS[] = {1, 2, 3, 127, 128, 129, 255, 320, 481, 1000, 1023, 4096, 4097};
#define N_CAP_STEPS (sizeof(CAP_STEPS) / sizeof(CAP_STEPS[0]))

static void test_mic_path_out_cap_holds_every_chunk_size(void)
{
    static const int rates[][2] = {{16000, 24000}, {16000, 16000}, {24000, 16000}};
    static int16_t in[4100];
    static int16_t out[8000];
    static int16_t reference[8000];
    fill_sine(in, 4100, 16000, 440.0f, 9000.0f);
    for (size_t r = 0; r < 3; r++) {
        pt_settings_t *s = new_settings();
        s->timing.mic_holdoff_s = 0.0f;
        pt_mic_path_t *p = pt_mic_path_new(rates[r][0], rates[r][1], s);
        pt_mic_path_t *ref = pt_mic_path_new(rates[r][0], rates[r][1], s);
        TEST_ASSERT_NOT_NULL(p);
        size_t produced = 0;
        for (size_t i = 0; i < N_CAP_STEPS; i++) {
            size_t n = CAP_STEPS[i];
            size_t cap = pt_mic_path_out_cap(p, n);
            TEST_ASSERT_TRUE(cap <= sizeof(out) / sizeof(out[0]));
            size_t got = pt_mic_path_run(p, in, n, out, cap);
            size_t want = pt_mic_path_run(ref, in, n, reference, sizeof(reference) / sizeof(reference[0]));
            TEST_ASSERT_EQUAL_UINT(want, got);
            if (got > 0) TEST_ASSERT_EQUAL_INT16_ARRAY(reference, out, got);
            produced += got;
        }
        TEST_ASSERT_TRUE(produced > 4000);
        pt_mic_path_free(p);
        pt_mic_path_free(ref);
        free(s);
    }
}

static void test_speaker_path_out_cap_holds_every_chunk_size(void)
{
    static const int rates[][2] = {{24000, 24000}, {24000, 16000}, {16000, 24000}};
    static int16_t in[4100];
    static int16_t out[8000];
    static int16_t reference[8000];
    fill_sine(in, 4100, 24000, 440.0f, 9000.0f);
    for (size_t r = 0; r < 3; r++) {
        pt_settings_t *s = new_settings();
        pt_speaker_path_t *p = pt_speaker_path_new(rates[r][0], rates[r][1], s);
        pt_speaker_path_t *ref = pt_speaker_path_new(rates[r][0], rates[r][1], s);
        TEST_ASSERT_NOT_NULL(p);
        size_t produced = 0;
        for (size_t i = 0; i < N_CAP_STEPS; i++) {
            size_t n = CAP_STEPS[i];
            size_t cap = pt_speaker_path_out_cap(p, n);
            TEST_ASSERT_TRUE(cap <= sizeof(out) / sizeof(out[0]));
            size_t got = pt_speaker_path_run(p, in, n, out, cap);
            size_t want = pt_speaker_path_run(ref, in, n, reference, sizeof(reference) / sizeof(reference[0]));
            TEST_ASSERT_EQUAL_UINT(want, got);
            if (got > 0) TEST_ASSERT_EQUAL_INT16_ARRAY(reference, out, got);
            produced += got;
        }
        TEST_ASSERT_TRUE(produced > 4000);
        pt_speaker_path_free(p);
        pt_speaker_path_free(ref);
        free(s);
    }
}

static void test_speaker_path_out_cap_is_input_size_without_resampling(void)
{
    pt_settings_t *s = new_settings();
    pt_speaker_path_t *p = pt_speaker_path_new(24000, 24000, s);
    TEST_ASSERT_EQUAL_UINT(480, pt_speaker_path_out_cap(p, 480));
    pt_speaker_path_free(p);
    free(s);
}

static float speaker_level(const pt_settings_t *s, float hz, float amplitude, bool peak)
{
    enum { N = 24000 };
    static int16_t in[N], out[N * 2];
    fill_sine(in, N, 24000, hz, amplitude);
    pt_speaker_path_t *p = pt_speaker_path_new(24000, 24000, s);
    TEST_ASSERT_NOT_NULL(p);
    size_t n = pt_speaker_path_run(p, in, N, out, N * 2);
    n += pt_speaker_path_flush(p, out + n, N * 2 - n);
    TEST_ASSERT_EQUAL_UINT(N, n);
    float level = rms(out + 6000, 12000) * 1.41421356f;
    if (peak) {
        int worst = 0;
        for (size_t i = 6000; i < 18000; i++)
            if (abs((int)out[i]) > worst) worst = abs((int)out[i]);
        level = (float)worst;
    }
    pt_speaker_path_free(p);
    return level;
}

static float speaker_amplitude(const pt_settings_t *s, float hz, float amplitude)
{
    return speaker_level(s, hz, amplitude, false);
}

static pt_settings_t *flat_speaker_settings(void)
{
    pt_settings_t *s = new_settings();
    s->sound.low_cut_hz = 0.0f;
    s->sound.loudness_gain_db = 0.0f;
    for (int i = 0; i < PT_EQ_BANDS; i++) s->sound.eq_db[i] = 0.0f;
    return s;
}

static void test_eq_zero_db_is_transparent(void)
{
    pt_settings_t *s = flat_speaker_settings();
    enum { N = 4800 };
    static int16_t in[N], out[N + 64];
    for (size_t i = 0; i < N; i++) in[i] = (int16_t)(((i * 7919u) % 20000u) - 10000);
    pt_speaker_path_t *p = pt_speaker_path_new(24000, 24000, s);
    size_t n = pt_speaker_path_run(p, in, N, out, N + 64);
    n += pt_speaker_path_flush(p, out + n, N + 64 - n);
    TEST_ASSERT_EQUAL_UINT(N, n);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, N);
    pt_speaker_path_free(p);

    pt_biquad_t f;
    pt_biquad_peaking(&f, 24000, 1500.0f, 1.0f, 0.0f);
    static int16_t again[N];
    pt_biquad_run(&f, in, again, N);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, again, N);
    free(s);
}

static void test_eq_band_boosts_its_frequency(void)
{
    pt_settings_t *s = flat_speaker_settings();
    float flat = speaker_amplitude(s, 1500.0f, 6000.0f);
    s->sound.eq_db[2] = 6.0f;
    float boosted = speaker_amplitude(s, 1500.0f, 6000.0f);
    float far_away = speaker_amplitude(s, 250.0f, 6000.0f);
    TEST_ASSERT_FLOAT_WITHIN(60.0f, 6000.0f, flat);
    TEST_ASSERT_TRUE(boosted / flat >= 1.9f);
    TEST_ASSERT_TRUE(boosted / flat <= 2.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, far_away / 6000.0f);
    free(s);
}

static void test_speaker_path_flush_returns_tail(void)
{
    pt_settings_t *s = flat_speaker_settings();
    pt_speaker_path_t *p = pt_speaker_path_new(24000, 16000, s);
    TEST_ASSERT_NOT_NULL(p);
    static int16_t in[2400], out[4000];
    fill_sine(in, 2400, 24000, 440.0f, 8000.0f);
    size_t streamed = pt_speaker_path_run(p, in, 2400, out, 4000);
    TEST_ASSERT_TRUE(streamed < 1600);
    size_t tail = pt_speaker_path_flush(p, out + streamed, 4000 - streamed);
    TEST_ASSERT_TRUE(tail > 0);
    TEST_ASSERT_EQUAL_UINT(1600, streamed + tail);
    TEST_ASSERT_EQUAL_UINT(0, pt_speaker_path_flush(p, out + 1600, 4000 - 1600));
    pt_speaker_path_free(p);
    free(s);
}

static void test_speaker_path_loudness_boosts_and_limits(void)
{
    pt_settings_t *s = flat_speaker_settings();
    s->sound.loudness_gain_db = 6.0f;
    float quiet = speaker_amplitude(s, 1000.0f, 2000.0f);
    TEST_ASSERT_FLOAT_WITHIN(40.0f, 3990.0f, quiet);
    float loud = speaker_level(s, 1000.0f, 30000.0f, true);
    TEST_ASSERT_TRUE(loud < 32768.0f);
    TEST_ASSERT_TRUE(loud > 25000.0f);
    free(s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_highpass_matches_hub);
    RUN_TEST(test_loudness_matches_hub);
    RUN_TEST(test_resample_16_to_24_matches_hub);
    RUN_TEST(test_resample_24_to_16_matches_hub);
    RUN_TEST(test_resample_same_rate_is_copy);
    RUN_TEST(test_resample_rejects_unsupported_ratio);
    RUN_TEST(test_mic_path_drops_holdoff);
    RUN_TEST(test_mic_path_drops_holdoff_inside_a_chunk);
    RUN_TEST(test_mic_path_out_cap_holds_every_chunk_size);
    RUN_TEST(test_speaker_path_out_cap_holds_every_chunk_size);
    RUN_TEST(test_speaker_path_out_cap_is_input_size_without_resampling);
    RUN_TEST(test_mic_summary_silence_and_peak);
    RUN_TEST(test_mic_gain_scales_signal);
    RUN_TEST(test_mic_path_leaves_the_gain_to_the_shell);
    RUN_TEST(test_peak_of_raw_channels);
    RUN_TEST(test_eq_zero_db_is_transparent);
    RUN_TEST(test_eq_band_boosts_its_frequency);
    RUN_TEST(test_speaker_path_flush_returns_tail);
    RUN_TEST(test_speaker_path_loudness_boosts_and_limits);
    return UNITY_END();
}
