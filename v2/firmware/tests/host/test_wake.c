#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "pt_wake_engine.h"

#define WAKE_ARENA (64 * 1024)
#define VAD_ARENA (48 * 1024)
#define MODELS 4
#define VAD 3
#define MAX_INFERENCES 512
#define MEAN_TOLERANCE 0.03f
#define RAW_TOLERANCE 0.06f
#define WINDOW PT_WAKE_WINDOW
#define NEVER 2.0f

static const char *const NAMES[MODELS] = {"hey_jarvis", "okay_nabu", "hey_mycroft", "vad"};
static const float CUTOFFS[MODELS] = {0.85f, 0.80f, 0.90f, 0.5f};
static const char *const CLIPS[] = {"hey_jarvis", "okay_nabu", "hey_mycroft", "speech_uk"};

static uint8_t *models[MODELS];
static uint8_t *vad_arena;
static uint8_t *wake_arena;
static void *engine_mem;
static pt_wake_engine_t *engine;

static void *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t padded = ((size_t)len + 15) / 16 * 16;
    uint8_t *data = aligned_alloc(16, padded);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_INT((int)len, (int)fread(data, 1, (size_t)len, f));
    fclose(f);
    *size = (size_t)len;
    return data;
}

static int16_t *load_clip(const char *clip, size_t *count)
{
    char path[512];
    snprintf(path, sizeof path, "%s/wake_%s.raw", PT_GOLDEN_DIR, clip);
    if (access(path, R_OK) != 0) {
        TEST_IGNORE_MESSAGE("voice clips are not in the repository: run make_wake_golden.py --synthesize on a Mac");
    }
    size_t size;
    int16_t *data = load_file(path, &size);
    *count = size / 2;
    return data;
}

static size_t load_probs(const char *clip, float probs[MAX_INFERENCES][MODELS])
{
    char path[512];
    snprintf(path, sizeof path, "%s/wake_%s.probs.txt", PT_GOLDEN_DIR, clip);
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t count = 0;
    while (count < MAX_INFERENCES &&
           fscanf(f, "%f %f %f %f", &probs[count][0], &probs[count][1], &probs[count][2], &probs[count][3]) == 4) {
        count++;
    }
    fclose(f);
    return count;
}

static pt_wake_model_t model(int i, float cutoff)
{
    pt_wake_model_t m = {models[i], cutoff, WINDOW};
    return m;
}

static void make_engine(float vad_cutoff)
{
    pt_wake_model_t vad = model(VAD, vad_cutoff);
    pt_wake_arena_t va = {vad_arena, VAD_ARENA};
    pt_wake_arena_t wa = {wake_arena, WAKE_ARENA};
    engine = pt_wake_engine_create(engine_mem, &vad, va, wa);
    TEST_ASSERT_NOT_NULL(engine);
}

void setUp(void)
{
    for (int i = 0; i < MODELS; i++) {
        char path[512];
        size_t size;
        snprintf(path, sizeof path, "%s/%s.tflite", PT_MODELS_DIR, NAMES[i]);
        models[i] = load_file(path, &size);
    }
    vad_arena = aligned_alloc(16, VAD_ARENA);
    wake_arena = aligned_alloc(16, WAKE_ARENA);
    engine_mem = aligned_alloc(16, (pt_wake_engine_size() + 15) / 16 * 16);
    TEST_ASSERT_NOT_NULL(engine_mem);
    make_engine(CUTOFFS[VAD]);
}

void tearDown(void)
{
    pt_wake_engine_destroy(engine);
    free(engine_mem);
    free(vad_arena);
    free(wake_arena);
    for (int i = 0; i < MODELS; i++) free(models[i]);
}

static size_t run(const int16_t *audio, size_t n, float out[MAX_INFERENCES][3])
{
    size_t count = 0;
    size_t at = 0;
    size_t chunk = 800;
    while (at + chunk <= n && count < MAX_INFERENCES) {
        TEST_ASSERT_FALSE(pt_wake_engine_feed(engine, audio + at, chunk));
        out[count][0] = pt_wake_engine_raw(engine, false);
        out[count][1] = pt_wake_engine_raw(engine, true);
        out[count][2] = pt_wake_engine_probability(engine);
        count++;
        at += chunk;
        chunk = 480;
    }
    return count;
}

static int count_detections(const int16_t *audio, size_t n)
{
    int detections = 0;
    for (size_t at = 0; at < n; at += 512) {
        size_t k = n - at < 512 ? n - at : 512;
        if (pt_wake_engine_feed(engine, audio + at, k)) detections++;
    }
    return detections;
}

static float window_mean(float want[MAX_INFERENCES][MODELS], size_t i, int m)
{
    if (i + 1 < WINDOW) return 0.0f;
    float sum = 0.0f;
    for (int k = 0; k < WINDOW; k++) sum += want[i - (size_t)k][m];
    return sum / WINDOW;
}

static void test_wake_matches_pymicro_wakeword_probabilities(void)
{
    static float want[MAX_INFERENCES][MODELS];
    static float got[MAX_INFERENCES][3];
    float worst_raw = 0.0f;
    float worst_mean = 0.0f;
    for (size_t c = 0; c < sizeof CLIPS / sizeof CLIPS[0]; c++) {
        size_t n;
        int16_t *audio = load_clip(CLIPS[c], &n);
        size_t expected = load_probs(CLIPS[c], want);
        for (int m = 0; m < MODELS; m++) {
            pt_wake_model_t wake = model(m, NEVER);
            TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
            if (c == 0) printf("%s: arena used %zu bytes\n", NAMES[m], pt_wake_engine_arena_used(engine, false));
            size_t count = run(audio, n, got);
            TEST_ASSERT_EQUAL_size_t(expected, count);
            for (size_t i = 0; i < count; i++) {
                worst_raw = fmaxf(worst_raw, fabsf(got[i][0] - want[i][m]));
                worst_raw = fmaxf(worst_raw, fabsf(got[i][1] - want[i][VAD]));
                worst_mean = fmaxf(worst_mean, fabsf(got[i][2] - window_mean(want, i, m)));
            }
        }
        free(audio);
    }
    printf("max deviation from pymicro-wakeword: window mean %.4f, single inference %.4f\n", (double)worst_mean,
           (double)worst_raw);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(MEAN_TOLERANCE, worst_mean);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(RAW_TOLERANCE, worst_raw);
}

static void test_wake_detects_synthetic_phrases(void)
{
    for (int m = 0; m < VAD; m++) {
        pt_wake_model_t wake = model(m, CUTOFFS[m]);
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
        for (size_t c = 0; c < sizeof CLIPS / sizeof CLIPS[0]; c++) {
            size_t n;
            int16_t *audio = load_clip(CLIPS[c], &n);
            pt_wake_engine_reset(engine);
            int detections = count_detections(audio, n);
            printf("%s on %s: %d detections, last mean %.3f\n", NAMES[m], CLIPS[c], detections,
                   (double)pt_wake_engine_probability(engine));
            TEST_ASSERT_EQUAL_INT_MESSAGE(strcmp(NAMES[m], CLIPS[c]) == 0 ? 1 : 0, detections, CLIPS[c]);
            free(audio);
        }
    }
}

static void test_wake_detects_again_after_holdoff(void)
{
    size_t n;
    int16_t *audio = load_clip("okay_nabu", &n);
    pt_wake_model_t wake = model(1, CUTOFFS[1]);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
    int detections = count_detections(audio, n);
    detections += count_detections(audio, n);
    TEST_ASSERT_EQUAL_INT(2, detections);
    free(audio);
}

static void test_wake_needs_voice_activity(void)
{
    pt_wake_engine_destroy(engine);
    make_engine(1.01f);
    for (int m = 0; m < VAD; m++) {
        size_t n;
        int16_t *audio = load_clip(CLIPS[m], &n);
        pt_wake_model_t wake = model(m, CUTOFFS[m]);
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
        TEST_ASSERT_EQUAL_INT(0, count_detections(audio, n));
        free(audio);
    }
}

static void test_wake_reset_starts_over(void)
{
    static float fresh[MAX_INFERENCES][3];
    static float again[MAX_INFERENCES][3];
    size_t n, other_n;
    int16_t *audio = load_clip("hey_mycroft", &n);
    int16_t *other = load_clip("speech_uk", &other_n);
    pt_wake_model_t wake = model(2, NEVER);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
    size_t count = run(audio, n, fresh);
    pt_wake_engine_feed(engine, other, other_n / 2 + 37);
    pt_wake_engine_reset(engine);
    TEST_ASSERT_EQUAL_size_t(count, run(audio, n, again));
    TEST_ASSERT_EQUAL_MEMORY(fresh, again, count * sizeof fresh[0]);
    free(audio);
    free(other);
}

static void test_wake_load_rejects_bad_model(void)
{
    static uint8_t junk[64];
    pt_wake_model_t bad = {junk, 0.5f, 5};
    TEST_ASSERT_NOT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &bad));
    TEST_ASSERT_FALSE(pt_wake_engine_feed(engine, (const int16_t *)junk, sizeof junk / 2));
    pt_wake_model_t big_window = model(0, 0.5f);
    big_window.window = 1000;
    TEST_ASSERT_NOT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &big_window));
    pt_wake_model_t good = model(0, CUTOFFS[0]);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &good));
}

static void test_wake_reports_the_detection_probability_after_a_big_chunk(void)
{
    size_t n;
    int16_t *audio = load_clip("hey_mycroft", &n);
    pt_wake_model_t wake = model(2, CUTOFFS[2]);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
    TEST_ASSERT_TRUE(pt_wake_engine_feed(engine, audio, n));
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(CUTOFFS[2], pt_wake_engine_probability(engine));
    TEST_ASSERT_FALSE(pt_wake_engine_feed(engine, audio, 480));
    TEST_ASSERT_LESS_THAN_FLOAT(CUTOFFS[2], pt_wake_engine_probability(engine));
    free(audio);
}

static size_t first_detection_end(const int16_t *audio, size_t n)
{
    for (size_t at = 0; at + 160 <= n; at += 160) {
        if (pt_wake_engine_feed(engine, audio + at, 160)) return at + 160;
    }
    return 0;
}

static size_t phrase_onset(const int16_t *audio, size_t n)
{
    for (size_t at = 0; at + 160 <= n; at += 160) {
        int peak = 0;
        for (size_t i = 0; i < 160; i++) {
            int v = abs(audio[at + i]);
            if (v > peak) peak = v;
        }
        if (peak > 500) return at;
    }
    return 0;
}

static void test_wake_holdoff_delays_a_phrase_right_after_a_reset_or_detection(void)
{
    size_t n;
    int16_t *audio = load_clip("hey_mycroft", &n);
    size_t onset = phrase_onset(audio, n);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(1600, onset);
    size_t from = onset - 1600;
    pt_wake_model_t wake = model(2, CUTOFFS[2]);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_wake_engine_load(engine, &wake));
    size_t first = first_detection_end(audio + from, n - from);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(PT_WAKE_HOLDOFF_FRAMES * 160, first, "phrase 0.1 s after a reset");
    size_t second = first_detection_end(audio + from, n - from);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(PT_WAKE_HOLDOFF_FRAMES * 160, second, "phrase right after a detection");
    free(audio);
}

static double json_number(const char *name, const char *group, const char *key)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.json", PT_MODELS_DIR, name);
    size_t size;
    char *text = load_file(path, &size);
    char *copy = malloc(size + 1);
    TEST_ASSERT_NOT_NULL(copy);
    memcpy(copy, text, size);
    copy[size] = '\0';
    free(text);
    cJSON *root = cJSON_Parse(copy);
    free(copy);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, path);
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, group), key);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(item), path);
    double value = item->valuedouble;
    cJSON_Delete(root);
    return value;
}

static void test_wake_constants_match_the_model_manifests(void)
{
    for (int m = 0; m < MODELS; m++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(PT_WAKE_WINDOW, (int)json_number(NAMES[m], "micro", "sliding_window_size"),
                                      NAMES[m]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(10, (int)json_number(NAMES[m], "micro", "feature_step_size"), NAMES[m]);
    }
    TEST_ASSERT_EQUAL_FLOAT(PT_WAKE_VAD_CUTOFF, (float)json_number("vad", "micro", "probability_cutoff"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wake_matches_pymicro_wakeword_probabilities);
    RUN_TEST(test_wake_detects_synthetic_phrases);
    RUN_TEST(test_wake_detects_again_after_holdoff);
    RUN_TEST(test_wake_needs_voice_activity);
    RUN_TEST(test_wake_reset_starts_over);
    RUN_TEST(test_wake_load_rejects_bad_model);
    RUN_TEST(test_wake_reports_the_detection_probability_after_a_big_chunk);
    RUN_TEST(test_wake_holdoff_delays_a_phrase_right_after_a_reset_or_detection);
    RUN_TEST(test_wake_constants_match_the_model_manifests);
    return UNITY_END();
}
