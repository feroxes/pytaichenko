#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pt_features.h"

#define MAX_FRAMES 1024

static const char *const CLIPS[] = {"hey_jarvis", "okay_nabu", "hey_mycroft", "speech_uk"};

typedef struct {
    size_t count;
    uint16_t v[MAX_FRAMES][PT_FEATURES_CHANNELS];
} frames_t;

static pt_features_t features;
static frames_t got;
static frames_t want;

void setUp(void) {}
void tearDown(void) {}

static int16_t *load_audio(const char *clip, size_t *count)
{
    char path[512];
    snprintf(path, sizeof path, "%s/wake_%s.raw", PT_GOLDEN_DIR, clip);
    if (access(path, R_OK) != 0) {
        TEST_IGNORE_MESSAGE("voice clips are not in the repository: run make_wake_golden.py --synthesize on a Mac");
    }
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

static void load_features(const char *clip, frames_t *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/wake_%s.features.txt", PT_GOLDEN_DIR, clip);
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    out->count = 0;
    unsigned value;
    while (fscanf(f, "%u", &value) == 1) {
        size_t frame = out->count / PT_FEATURES_CHANNELS;
        TEST_ASSERT_LESS_THAN_size_t(MAX_FRAMES, frame);
        out->v[frame][out->count % PT_FEATURES_CHANNELS] = (uint16_t)value;
        out->count++;
    }
    fclose(f);
    TEST_ASSERT_EQUAL_size_t(0, out->count % PT_FEATURES_CHANNELS);
    out->count /= PT_FEATURES_CHANNELS;
}

static void run(const int16_t *audio, size_t n, size_t chunk, frames_t *out)
{
    out->count = 0;
    for (size_t at = 0; at < n; at += chunk) {
        const int16_t *p = audio + at;
        size_t left = n - at < chunk ? n - at : chunk;
        while (pt_features_next(&features, &p, &left, out->v[out->count])) {
            out->count++;
            TEST_ASSERT_LESS_THAN_size_t(MAX_FRAMES, out->count);
        }
        TEST_ASSERT_EQUAL_size_t(0, left);
    }
}

static int max_deviation(const frames_t *a, const frames_t *b)
{
    int worst = 0;
    for (size_t i = 0; i < a->count; i++) {
        for (int c = 0; c < PT_FEATURES_CHANNELS; c++) {
            int d = abs((int)a->v[i][c] - (int)b->v[i][c]);
            if (d > worst) worst = d;
        }
    }
    return worst;
}

static void test_features_match_pymicro_features(void)
{
    for (size_t k = 0; k < sizeof CLIPS / sizeof CLIPS[0]; k++) {
        size_t n;
        int16_t *audio = load_audio(CLIPS[k], &n);
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_features_init(&features));
        run(audio, n, PT_FEATURES_STEP, &got);
        load_features(CLIPS[k], &want);
        TEST_ASSERT_EQUAL_size_t(want.count, got.count);
        int worst = max_deviation(&got, &want);
        printf("%s: %zu frames, max deviation %d\n", CLIPS[k], got.count, worst);
        TEST_ASSERT_LESS_OR_EQUAL_INT(1, worst);
        free(audio);
    }
}

static void test_features_streaming_chunk_independent(void)
{
    static frames_t reference;
    static const size_t chunks[] = {320, 512, 1, 7, 100000};
    size_t n;
    int16_t *audio = load_audio("hey_jarvis", &n);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_features_init(&features));
    run(audio, n, PT_FEATURES_STEP, &reference);
    TEST_ASSERT_GREATER_THAN_size_t(0, reference.count);
    for (size_t k = 0; k < sizeof chunks / sizeof chunks[0]; k++) {
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_features_init(&features));
        run(audio, n, chunks[k], &got);
        TEST_ASSERT_EQUAL_size_t(reference.count, got.count);
        TEST_ASSERT_EQUAL_MEMORY(reference.v, got.v, got.count * sizeof got.v[0]);
    }
    free(audio);
}

static void test_features_reset_starts_over(void)
{
    static frames_t first;
    size_t n;
    int16_t *audio = load_audio("okay_nabu", &n);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_features_init(&features));
    run(audio, n, PT_FEATURES_STEP, &first);
    pt_features_reset(&features);
    run(audio, n, PT_FEATURES_STEP, &got);
    TEST_ASSERT_EQUAL_size_t(first.count, got.count);
    TEST_ASSERT_EQUAL_MEMORY(first.v, got.v, got.count * sizeof got.v[0]);
    free(audio);
}

static void test_features_need_a_full_window_first(void)
{
    static const int16_t silence[PT_FEATURES_WINDOW + PT_FEATURES_STEP];
    uint16_t out[PT_FEATURES_CHANNELS];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_features_init(&features));
    const int16_t *p = silence;
    size_t left = PT_FEATURES_WINDOW - 1;
    TEST_ASSERT_FALSE(pt_features_next(&features, &p, &left, out));
    TEST_ASSERT_EQUAL_size_t(0, left);
    left = 1;
    TEST_ASSERT_TRUE(pt_features_next(&features, &p, &left, out));
    TEST_ASSERT_EQUAL_size_t(0, left);
    left = PT_FEATURES_STEP - 1;
    TEST_ASSERT_FALSE(pt_features_next(&features, &p, &left, out));
}

static void test_features_quantize_matches_model_input(void)
{
    const float scale = 0.10196078568696976f;
    TEST_ASSERT_EQUAL_INT(-128, pt_features_quantize(0, scale, -128));
    TEST_ASSERT_EQUAL_INT(-128, pt_features_quantize(1, scale, -128));
    TEST_ASSERT_EQUAL_INT(-90, pt_features_quantize(100, scale, -128));
    TEST_ASSERT_EQUAL_INT(0, pt_features_quantize(333, scale, -128));
    TEST_ASSERT_EQUAL_INT(119, pt_features_quantize(646, scale, -128));
    TEST_ASSERT_EQUAL_INT(127, pt_features_quantize(667, scale, -128));
    TEST_ASSERT_EQUAL_INT(127, pt_features_quantize(1000, scale, -128));
    TEST_ASSERT_EQUAL_INT(127, pt_features_quantize(65535, scale, -128));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_features_match_pymicro_features);
    RUN_TEST(test_features_streaming_chunk_independent);
    RUN_TEST(test_features_reset_starts_over);
    RUN_TEST(test_features_need_a_full_window_first);
    RUN_TEST(test_features_quantize_matches_model_input);
    return UNITY_END();
}
