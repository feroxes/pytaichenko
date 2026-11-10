#include "pt_wake.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pt_wake_engine.h"

extern const uint8_t hey_jarvis_model[] asm("_binary_hey_jarvis_tflite_start");
extern const uint8_t okay_nabu_model[] asm("_binary_okay_nabu_tflite_start");
extern const uint8_t hey_mycroft_model[] asm("_binary_hey_mycroft_tflite_start");
extern const uint8_t vad_model[] asm("_binary_vad_tflite_start");

namespace {

constexpr const char *TAG = "pt_wake";
constexpr size_t kWakeArena = 48 * 1024;
constexpr size_t kVadArena = 32 * 1024;

struct Word {
    pt_wake_word_t id;
    const char *name;
    const uint8_t *model;
    float cutoff;
};

const Word WORDS[] = {
    {PT_WAKE_HEY_JARVIS, "hey_jarvis", hey_jarvis_model, 0.85f},
    {PT_WAKE_OKAY_NABU, "okay_nabu", okay_nabu_model, 0.80f},
    {PT_WAKE_HEY_MYCROFT, "hey_mycroft", hey_mycroft_model, 0.90f},
};

const pt_wake_model_t VAD = {vad_model, PT_WAKE_VAD_CUTOFF, PT_WAKE_WINDOW};

pt_wake_engine_t *engine;
SemaphoreHandle_t lock;
const Word *current;
float last_probability;

void *psram(size_t size)
{
    return heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

}

extern "C" {

pt_err_t pt_wake_init(void)
{
    if (engine) return current ? PT_OK : pt_wake_select(WORDS[0].name);
    void *mem = psram(pt_wake_engine_size());
    uint8_t *vad_arena = static_cast<uint8_t *>(psram(kVadArena));
    uint8_t *wake_arena = static_cast<uint8_t *>(psram(kWakeArena));
    lock = lock ? lock : xSemaphoreCreateMutex();
    pt_wake_engine_t *e = nullptr;
    if (mem && vad_arena && wake_arena && lock) {
        e = pt_wake_engine_create(mem, &VAD, {vad_arena, kVadArena}, {wake_arena, kWakeArena});
    }
    if (!e) {
        ESP_LOGE(TAG, "wake word engine did not start");
        heap_caps_free(mem);
        heap_caps_free(vad_arena);
        heap_caps_free(wake_arena);
        return mem && vad_arena && wake_arena && lock ? PT_ERR_INVALID : PT_ERR_NOMEM;
    }
    ESP_LOGI(TAG, "voice activity model ready, arena %u of %u bytes", (unsigned)pt_wake_engine_arena_used(e, true),
             (unsigned)kVadArena);
    engine = e;
    return pt_wake_select(WORDS[0].name);
}

pt_err_t pt_wake_select(const char *wake_word)
{
    if (!engine || !wake_word) return PT_ERR_INVALID;
    const Word *word = nullptr;
    for (const Word &w : WORDS) {
        if (strcmp(w.name, wake_word) == 0) word = &w;
    }
    if (!word) return PT_ERR_INVALID;
    xSemaphoreTake(lock, portMAX_DELAY);
    pt_err_t err = PT_OK;
    if (word != current) {
        pt_wake_model_t m = {word->model, word->cutoff, PT_WAKE_WINDOW};
        err = pt_wake_engine_load(engine, &m);
        if (err == PT_OK) {
            ESP_LOGI(TAG, "wake word %s ready, arena %u of %u bytes", word->name,
                     (unsigned)pt_wake_engine_arena_used(engine, false), (unsigned)kWakeArena);
            current = word;
        } else {
            ESP_LOGE(TAG, "wake word %s did not load", word->name);
            if (current) {
                pt_wake_model_t back = {current->model, current->cutoff, PT_WAKE_WINDOW};
                if (pt_wake_engine_load(engine, &back) != PT_OK) {
                    ESP_LOGE(TAG, "wake word %s could not be loaded back", current->name);
                    current = nullptr;
                }
            }
        }
    }
    xSemaphoreGive(lock);
    return err;
}

void pt_wake_reset(void)
{
    if (!engine) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    pt_wake_engine_reset(engine);
    last_probability = 0.0f;
    xSemaphoreGive(lock);
}

bool pt_wake_feed(const int16_t *pcm16k, size_t n)
{
    if (!engine || !pcm16k) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool detected = pt_wake_engine_feed(engine, pcm16k, n);
    last_probability = pt_wake_engine_probability(engine);
    const Word *word = current;
    xSemaphoreGive(lock);
    if (detected && word) ESP_LOGI(TAG, "wake word %s heard, probability %.2f", word->name, (double)last_probability);
    return detected;
}

const char *pt_wake_current(void)
{
    if (!lock) return nullptr;
    xSemaphoreTake(lock, portMAX_DELAY);
    const char *name = current ? current->name : nullptr;
    xSemaphoreGive(lock);
    return name;
}

float pt_wake_last_probability(void)
{
    return last_probability;
}
}
