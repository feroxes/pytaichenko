#include "unity.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pt_pcm.h"

#define PI 3.14159265358979323846

void setUp(void) {}
void tearDown(void) {}

static int alloc_calls;
static int free_calls;

static void *counting_alloc(size_t bytes)
{
    alloc_calls++;
    return malloc(bytes);
}

static void counting_free(void *p)
{
    free_calls++;
    free(p);
}

void test_volume_gain_curve(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, pt_volume_gain(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.251f, pt_volume_gain(0.7f));
    TEST_ASSERT_FLOAT_WITHIN(0.0002f, 0.0158f, pt_volume_gain(0.1f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pt_volume_gain(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pt_volume_gain(-0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, pt_volume_gain(1.5f));
}

void test_apply_gain_saturates(void)
{
    int16_t pcm[] = {1000, -1000, 30000, -30000, 32767, -32768, 0};
    pt_pcm_apply_gain(pcm, 7, 2.0f);
    TEST_ASSERT_EQUAL_INT16(2000, pcm[0]);
    TEST_ASSERT_EQUAL_INT16(-2000, pcm[1]);
    TEST_ASSERT_EQUAL_INT16(32767, pcm[2]);
    TEST_ASSERT_EQUAL_INT16(-32768, pcm[3]);
    TEST_ASSERT_EQUAL_INT16(32767, pcm[4]);
    TEST_ASSERT_EQUAL_INT16(-32768, pcm[5]);
    TEST_ASSERT_EQUAL_INT16(0, pcm[6]);

    int16_t quiet[] = {1000, -1000, 32767};
    pt_pcm_apply_gain(quiet, 3, 0.5f);
    TEST_ASSERT_EQUAL_INT16(500, quiet[0]);
    TEST_ASSERT_EQUAL_INT16(-500, quiet[1]);
    TEST_ASSERT_INT16_WITHIN(1, 16383, quiet[2]);

    int16_t mute[] = {1000, -1000};
    pt_pcm_apply_gain(mute, 2, 0.0f);
    TEST_ASSERT_EQUAL_INT16(0, mute[0]);
    TEST_ASSERT_EQUAL_INT16(0, mute[1]);
}

void test_unpack_inmp441_left_aligned_24bit(void)
{
    const int32_t in[] = {
        (int32_t)0x7FFFFF00, (int32_t)0x80000000,
        (int32_t)0x00010000, (int32_t)0xFFFF0000,
        (int32_t)0x0000FF00, (int32_t)0x12345600,
    };
    int16_t left[3] = {0};
    int16_t right[3] = {0};
    size_t n = pt_pcm_unpack_i2s32_stereo(in, 3, left, right);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_INT16(32767, left[0]);
    TEST_ASSERT_EQUAL_INT16(-32768, right[0]);
    TEST_ASSERT_EQUAL_INT16(1, left[1]);
    TEST_ASSERT_EQUAL_INT16(-1, right[1]);
    TEST_ASSERT_EQUAL_INT16(0, left[2]);
    TEST_ASSERT_EQUAL_INT16(0x1234, right[2]);
}

void test_unpack_zero_frames(void)
{
    int16_t l = 77, r = 88;
    int32_t in = 0;
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_unpack_i2s32_stereo(&in, 0, &l, &r));
    TEST_ASSERT_EQUAL_INT16(77, l);
    TEST_ASSERT_EQUAL_INT16(88, r);
}

void test_playback_queue_bounded_drops_nothing_until_cap(void)
{
    alloc_calls = 0;
    free_calls = 0;
    pt_pcm_queue_t *q = pt_pcm_queue_new(8, counting_alloc);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(alloc_calls >= 1);
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_len(q));
    TEST_ASSERT_EQUAL_UINT(8, pt_pcm_queue_free(q));

    int16_t a[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_UINT(6, pt_pcm_queue_push(q, a, 6));
    int16_t b[6] = {7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_UINT(2, pt_pcm_queue_push(q, b, 6));
    TEST_ASSERT_EQUAL_UINT(8, pt_pcm_queue_len(q));
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_free(q));
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_push(q, b, 6));

    int16_t out[16] = {0};
    TEST_ASSERT_EQUAL_UINT(8, pt_pcm_queue_pop(q, out, 16));
    int16_t want[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 8);
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_pop(q, out, 4));
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_len(q));

    pt_pcm_queue_delete(q, counting_free);
    TEST_ASSERT_EQUAL_INT(alloc_calls, free_calls);
}

void test_queue_wraps(void)
{
    pt_pcm_queue_t *q = pt_pcm_queue_new(5, malloc);
    TEST_ASSERT_NOT_NULL(q);
    int16_t a[4] = {1, 2, 3, 4};
    int16_t out[8];
    TEST_ASSERT_EQUAL_UINT(4, pt_pcm_queue_push(q, a, 4));
    TEST_ASSERT_EQUAL_UINT(3, pt_pcm_queue_pop(q, out, 3));
    int16_t b[4] = {5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT(4, pt_pcm_queue_push(q, b, 4));
    TEST_ASSERT_EQUAL_UINT(5, pt_pcm_queue_len(q));
    TEST_ASSERT_EQUAL_UINT(2, pt_pcm_queue_pop(q, out, 2));
    int16_t first[2] = {4, 5};
    TEST_ASSERT_EQUAL_INT16_ARRAY(first, out, 2);
    int16_t c[2] = {9, 10};
    TEST_ASSERT_EQUAL_UINT(2, pt_pcm_queue_push(q, c, 2));
    TEST_ASSERT_EQUAL_UINT(5, pt_pcm_queue_pop(q, out, 8));
    int16_t rest[5] = {6, 7, 8, 9, 10};
    TEST_ASSERT_EQUAL_INT16_ARRAY(rest, out, 5);
    pt_pcm_queue_delete(q, free);
}

void test_queue_clear(void)
{
    pt_pcm_queue_t *q = pt_pcm_queue_new(4, malloc);
    int16_t a[4] = {1, 2, 3, 4};
    pt_pcm_queue_push(q, a, 4);
    int16_t out[4];
    pt_pcm_queue_pop(q, out, 1);
    pt_pcm_queue_clear(q);
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_len(q));
    TEST_ASSERT_EQUAL_UINT(4, pt_pcm_queue_free(q));
    TEST_ASSERT_EQUAL_UINT(0, pt_pcm_queue_pop(q, out, 4));
    int16_t b[3] = {7, 8, 9};
    TEST_ASSERT_EQUAL_UINT(3, pt_pcm_queue_push(q, b, 3));
    TEST_ASSERT_EQUAL_UINT(3, pt_pcm_queue_pop(q, out, 4));
    TEST_ASSERT_EQUAL_INT16_ARRAY(b, out, 3);
    pt_pcm_queue_delete(q, free);
}

static void *failing_alloc(size_t bytes)
{
    (void)bytes;
    return NULL;
}

void test_queue_new_handles_alloc_failure(void)
{
    TEST_ASSERT_NULL(pt_pcm_queue_new(8, failing_alloc));
    TEST_ASSERT_NULL(pt_pcm_queue_new(0, malloc));
    TEST_ASSERT_NULL(pt_pcm_queue_new(SIZE_MAX / 2, malloc));
}

#define UP_IN 4800

static double tone_level(const int16_t *x, size_t n, double freq, double rate)
{
    double re = 0.0;
    double im = 0.0;
    for (size_t i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * PI * (double)i / (double)(n - 1));
        re += w * x[i] * cos(2.0 * PI * freq * (double)i / rate);
        im += w * x[i] * sin(2.0 * PI * freq * (double)i / rate);
    }
    return sqrt(re * re + im * im) / (double)n;
}

static void sine24(int16_t *x, size_t n, double freq, double amp)
{
    for (size_t i = 0; i < n; i++)
        x[i] = (int16_t)lround(amp * sin(2.0 * PI * freq * (double)i / 24000.0));
}

void test_up2_doubles_the_sample_count_and_keeps_dc(void)
{
    static int16_t in[UP_IN];
    static int16_t out[2 * UP_IN];
    pt_up2_t s;
    pt_up2_reset(&s);
    for (size_t i = 0; i < UP_IN; i++)
        in[i] = 10000;
    TEST_ASSERT_EQUAL_size_t(2 * UP_IN, pt_up2_run(&s, in, UP_IN, out));
    for (size_t i = 2 * PT_UP2_TAPS; i < 2 * UP_IN; i++)
        TEST_ASSERT_INT_WITHIN(2, 10000, out[i]);
}

void test_up2_odd_outputs_are_the_input_delayed(void)
{
    static int16_t in[UP_IN];
    static int16_t out[2 * UP_IN];
    pt_up2_t s;
    pt_up2_reset(&s);
    srand(7);
    for (size_t i = 0; i < UP_IN; i++)
        in[i] = (int16_t)(rand() % 20001 - 10000);
    pt_up2_run(&s, in, UP_IN, out);
    for (size_t m = PT_UP2_DELAY; m < UP_IN; m++)
        TEST_ASSERT_EQUAL_INT16(in[m - PT_UP2_DELAY], out[2 * m + 1]);
}

void test_up2_keeps_a_tone_and_removes_its_image(void)
{
    static const double freqs[] = {1000.0, 4000.0, 9000.0};
    static int16_t in[UP_IN];
    static int16_t out[2 * UP_IN];
    for (size_t k = 0; k < sizeof(freqs) / sizeof(freqs[0]); k++) {
        pt_up2_t s;
        pt_up2_reset(&s);
        sine24(in, UP_IN, freqs[k], 16000.0);
        pt_up2_run(&s, in, UP_IN, out);
        const int16_t *tail = out + 4 * PT_UP2_TAPS;
        size_t n = 2 * UP_IN - 4 * PT_UP2_TAPS;
        double keep = tone_level(tail, n, freqs[k], 48000.0);
        double image = tone_level(tail, n, 24000.0 - freqs[k], 48000.0);
        double ref = tone_level(in, UP_IN, freqs[k], 24000.0);
        TEST_ASSERT_FLOAT_WITHIN((float)(0.012 * ref), (float)ref, (float)keep);
        TEST_ASSERT_TRUE(20.0 * log10(image / keep) < -60.0);
    }
}

void test_up2_gives_the_same_output_in_any_chunks(void)
{
    static int16_t in[UP_IN];
    static int16_t whole[2 * UP_IN];
    static int16_t parts[2 * UP_IN];
    pt_up2_t a;
    pt_up2_t b;
    pt_up2_reset(&a);
    pt_up2_reset(&b);
    sine24(in, UP_IN, 3000.0, 20000.0);
    pt_up2_run(&a, in, UP_IN, whole);
    size_t done = 0;
    size_t step = 1;
    while (done < UP_IN) {
        size_t n = step < UP_IN - done ? step : UP_IN - done;
        pt_up2_run(&b, in + done, n, parts + 2 * done);
        done += n;
        step = step % 37 + 3;
    }
    TEST_ASSERT_EQUAL_MEMORY(whole, parts, sizeof(whole));
}

void test_up2_clamps_overshoot_instead_of_wrapping(void)
{
    static int16_t in[UP_IN];
    static int16_t out[2 * UP_IN];
    pt_up2_t s;
    pt_up2_reset(&s);
    for (size_t i = 0; i < UP_IN; i++)
        in[i] = (i / 40) % 2 ? INT16_MIN : INT16_MAX;
    pt_up2_run(&s, in, UP_IN, out);
    for (size_t i = 2 * PT_UP2_TAPS; i + 1 < 2 * UP_IN; i++) {
        int16_t now = in[(i / 2) - PT_UP2_DELAY];
        if (now == INT16_MAX && in[(i / 2) - PT_UP2_DELAY + 1] == INT16_MAX && in[(i / 2) - PT_UP2_DELAY - 1] == INT16_MAX)
            TEST_ASSERT_TRUE(out[i] > 0);
    }
}

void test_up2_reset_clears_the_history(void)
{
    static int16_t in[64];
    static int16_t out[128];
    pt_up2_t s;
    pt_up2_reset(&s);
    for (size_t i = 0; i < 64; i++)
        in[i] = 30000;
    pt_up2_run(&s, in, 64, out);
    pt_up2_reset(&s);
    memset(in, 0, sizeof(in));
    pt_up2_run(&s, in, 64, out);
    for (size_t i = 0; i < 128; i++)
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
}

static double goertzel(const int16_t *x, size_t n, double freq)
{
    return tone_level(x, n, freq, 24000.0);
}

void test_test_sound_is_four_rising_notes(void)
{
    static int16_t out[PT_TEST_SOUND_SAMPLES];
    TEST_ASSERT_EQUAL_size_t(PT_TEST_SOUND_SAMPLES, pt_test_sound(out, PT_TEST_SOUND_SAMPLES));
    static const double notes[] = {523.25, 659.25, 783.99, 1046.5};
    size_t at = 0;
    for (size_t k = 0; k < 4; k++) {
        size_t len = k < 3 ? PT_TEST_SOUND_NOTE : PT_TEST_SOUND_LAST;
        const int16_t *x = out + at + len / 4;
        size_t n = len / 2;
        double want = goertzel(x, n, notes[k]);
        for (size_t j = 0; j < 4; j++) {
            if (j != k)
                TEST_ASSERT_TRUE(goertzel(x, n, notes[j]) < want / 20.0);
        }
        at += len;
    }
    TEST_ASSERT_EQUAL_size_t(PT_TEST_SOUND_SAMPLES, at);
}

void test_test_sound_is_moderate_and_starts_and_ends_quietly(void)
{
    static int16_t out[PT_TEST_SOUND_SAMPLES];
    pt_test_sound(out, PT_TEST_SOUND_SAMPLES);
    int peak = 0;
    for (size_t i = 0; i < PT_TEST_SOUND_SAMPLES; i++)
        peak = abs(out[i]) > peak ? abs(out[i]) : peak;
    TEST_ASSERT_INT_WITHIN(500, 7300, peak);
    TEST_ASSERT_INT_WITHIN(300, 0, out[0]);
    TEST_ASSERT_INT_WITHIN(300, 0, out[PT_TEST_SOUND_SAMPLES - 1]);
    for (size_t k = 1; k < 4; k++) {
        size_t edge = k * PT_TEST_SOUND_NOTE;
        TEST_ASSERT_INT_WITHIN(300, 0, out[edge - 1]);
        TEST_ASSERT_INT_WITHIN(300, 0, out[edge]);
    }
}

void test_test_sound_needs_room_for_all_of_it(void)
{
    static int16_t out[PT_TEST_SOUND_SAMPLES];
    TEST_ASSERT_EQUAL_size_t(0, pt_test_sound(out, PT_TEST_SOUND_SAMPLES - 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_gain_curve);
    RUN_TEST(test_apply_gain_saturates);
    RUN_TEST(test_unpack_inmp441_left_aligned_24bit);
    RUN_TEST(test_unpack_zero_frames);
    RUN_TEST(test_playback_queue_bounded_drops_nothing_until_cap);
    RUN_TEST(test_queue_wraps);
    RUN_TEST(test_queue_clear);
    RUN_TEST(test_queue_new_handles_alloc_failure);
    RUN_TEST(test_up2_doubles_the_sample_count_and_keeps_dc);
    RUN_TEST(test_up2_odd_outputs_are_the_input_delayed);
    RUN_TEST(test_up2_keeps_a_tone_and_removes_its_image);
    RUN_TEST(test_up2_gives_the_same_output_in_any_chunks);
    RUN_TEST(test_up2_clamps_overshoot_instead_of_wrapping);
    RUN_TEST(test_up2_reset_clears_the_history);
    RUN_TEST(test_test_sound_is_four_rising_notes);
    RUN_TEST(test_test_sound_is_moderate_and_starts_and_ends_quietly);
    RUN_TEST(test_test_sound_needs_room_for_all_of_it);
    return UNITY_END();
}
