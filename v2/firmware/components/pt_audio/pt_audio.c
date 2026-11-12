#include "pt_audio.h"

#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pt_pcm.h"

#define MIC_RATE 16000
#define SPK_RATE 24000
#define MIC_BCLK GPIO_NUM_5
#define MIC_WS GPIO_NUM_6
#define MIC_DIN GPIO_NUM_7
#define SPK_BCLK GPIO_NUM_15
#define SPK_LRC GPIO_NUM_16
#define SPK_DOUT GPIO_NUM_17

#define MIC_DMA_FRAMES 256
#define MIC_DMA_DESCS 12
#define MIC_CHUNK_FRAMES 512
#define SPK_OUT_RATE 48000
#define SPK_DMA_FRAMES 480
#define SPK_DMA_DESCS 4
#define SPK_CHUNK 240
#define QUEUE_SECONDS 6
#define QUEUE_SAMPLES (QUEUE_SECONDS * SPK_RATE)
#define FADE_SAMPLES (SPK_RATE * 5 / 1000)
#define PREBUFFER_SAMPLES (SPK_RATE * 80 / 1000)

#define TASK_STACK 4096
#define TASK_PRIORITY 10
#define TASK_CORE 1

extern const uint8_t wake_raw_start[] asm("_binary_wake_raw_start");
extern const uint8_t wake_raw_end[] asm("_binary_wake_raw_end");
extern const uint8_t end_raw_start[] asm("_binary_end_raw_start");
extern const uint8_t end_raw_end[] asm("_binary_end_raw_end");

static const char *TAG = "audio";

static i2s_chan_handle_t rx_chan;
static i2s_chan_handle_t tx_chan;
static int32_t *rx_buf;
static pt_pcm_queue_t *queue;
static SemaphoreHandle_t lock;
static TaskHandle_t out_task;
static bool rx_enabled;
static bool tx_enabled;
static bool started;
static atomic_uint rx_overflows;
static atomic_uint tx_underruns;

static volatile float gain = 1.0f;
static bool stream_open;
static bool stream_ended;
static bool stop_req;
static pt_up2_t upsampler;
static int16_t *test_sound;

static void *psram_alloc(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_free(void *p)
{
    heap_caps_free(p);
}

static int64_t play_slowest_us;

static void write_samples(const int16_t *buf, size_t n)
{
    static int16_t up[2 * SPK_CHUNK];
    static int64_t last_us;
    pt_up2_run(&upsampler, buf, n, up);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx_chan, up, 2 * n * sizeof(int16_t), &written, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (last_us != 0 && now - last_us > play_slowest_us)
        play_slowest_us = now - last_us;
    last_us = now;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker write failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void fade_out_chunk(int16_t *buf, int16_t last)
{
    for (size_t i = 0; i < SPK_CHUNK; i++) {
        buf[i] = i < FADE_SAMPLES ? (int16_t)((int32_t)last * (int32_t)(FADE_SAMPLES - i) / (int32_t)FADE_SAMPLES) : 0;
    }
}

static void output_task(void *arg)
{
    (void)arg;
    int16_t buf[SPK_CHUNK];
    bool active = false;
    int16_t last = 0;
    unsigned under_at_start = 0;
    int64_t started_us = 0;

    for (;;) {
        size_t n = 0;
        bool stop = false;
        bool ramp_down = false;
        bool fade_in = false;

        xSemaphoreTake(lock, portMAX_DELAY);
        if (stop_req) {
            stop_req = false;
            stop = true;
        } else {
            size_t len = pt_pcm_queue_len(queue);
            bool can_run = active || len >= PREBUFFER_SAMPLES || (stream_ended && len > 0);
            if (can_run && len > 0) {
                n = pt_pcm_queue_pop(queue, buf, SPK_CHUNK);
                fade_in = !active;
            } else if (active) {
                ramp_down = true;
            }
            if (n == 0 && stream_ended) {
                stream_ended = false;
                stream_open = false;
            }
        }
        xSemaphoreGive(lock);

        if (stop || ramp_down) {
            if (active) {
                fade_out_chunk(buf, last);
                write_samples(buf, SPK_CHUNK);
                active = false;
                last = 0;
                ESP_LOGI(TAG, "playback %lld ms: speaker underruns %u, slowest write %lld ms",
                         (esp_timer_get_time() - started_us) / 1000, atomic_load(&tx_underruns) - under_at_start,
                         play_slowest_us / 1000);
                continue;
            }
        }
        if (n > 0) {
            if (fade_in) {
                for (size_t i = 0; i < n && i < FADE_SAMPLES; i++)
                    buf[i] = (int16_t)((int32_t)buf[i] * (int32_t)i / (int32_t)FADE_SAMPLES);
            }
            pt_pcm_apply_gain(buf, n, gain);
            last = buf[n - 1];
            if (!active) {
                under_at_start = atomic_load(&tx_underruns);
                started_us = esp_timer_get_time();
                play_slowest_us = 0;
            }
            active = true;
            write_samples(buf, n);
            continue;
        }
        memset(buf, 0, sizeof buf);
        write_samples(buf, SPK_CHUNK);
    }
}

static void teardown(void)
{
    if (rx_chan) {
        if (rx_enabled) {
            i2s_channel_disable(rx_chan);
            rx_enabled = false;
        }
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }
    if (tx_chan) {
        if (tx_enabled) {
            i2s_channel_disable(tx_chan);
            tx_enabled = false;
        }
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    heap_caps_free(rx_buf);
    rx_buf = NULL;
    pt_pcm_queue_delete(queue, psram_free);
    queue = NULL;
    if (lock) {
        vSemaphoreDelete(lock);
        lock = NULL;
    }
}

static bool on_rx_overflow(i2s_chan_handle_t handle, i2s_event_data_t *event, void *ctx)
{
    (void)handle;
    (void)event;
    (void)ctx;
    atomic_fetch_add(&rx_overflows, 1);
    return false;
}

static bool IRAM_ATTR on_tx_underrun(i2s_chan_handle_t handle, i2s_event_data_t *event, void *ctx)
{
    (void)handle;
    (void)event;
    (void)ctx;
    atomic_fetch_add(&tx_underruns, 1);
    return false;
}

uint32_t pt_audio_mic_overflows(void)
{
    return atomic_load(&rx_overflows);
}

static pt_err_t init_rx(void)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cfg.dma_desc_num = MIC_DMA_DESCS;
    cfg.dma_frame_num = MIC_DMA_FRAMES;
    if (i2s_new_channel(&cfg, NULL, &rx_chan) != ESP_OK)
        return PT_ERR_INVALID;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DIN,
        },
    };
    if (i2s_channel_init_std_mode(rx_chan, &std) != ESP_OK)
        return PT_ERR_INVALID;
    i2s_event_callbacks_t cbs = {.on_recv_q_ovf = on_rx_overflow};
    if (i2s_channel_register_event_callback(rx_chan, &cbs, NULL) != ESP_OK)
        ESP_LOGW(TAG, "microphone overflow counter is not available");
    return PT_OK;
}

static pt_err_t init_tx(void)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cfg.dma_desc_num = SPK_DMA_DESCS;
    cfg.dma_frame_num = SPK_DMA_FRAMES;
    cfg.auto_clear_after_cb = true;
    if (i2s_new_channel(&cfg, &tx_chan, NULL) != ESP_OK)
        return PT_ERR_INVALID;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_OUT_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK,
            .ws = SPK_LRC,
            .dout = SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    if (i2s_channel_init_std_mode(tx_chan, &std) != ESP_OK)
        return PT_ERR_INVALID;
    i2s_event_callbacks_t cbs = {.on_send_q_ovf = on_tx_underrun};
    if (i2s_channel_register_event_callback(tx_chan, &cbs, NULL) != ESP_OK)
        ESP_LOGW(TAG, "speaker underrun counter is not available");
    return PT_OK;
}

pt_err_t pt_audio_start(void)
{
    if (started)
        return PT_OK;

    lock = xSemaphoreCreateMutex();
    queue = pt_pcm_queue_new(QUEUE_SAMPLES, psram_alloc);
    rx_buf = heap_caps_malloc(MIC_CHUNK_FRAMES * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lock || !queue || !rx_buf) {
        teardown();
        return PT_ERR_NOMEM;
    }

    pt_err_t err = init_rx();
    if (err == PT_OK)
        err = init_tx();
    if (err != PT_OK) {
        teardown();
        return err;
    }

    rx_enabled = i2s_channel_enable(rx_chan) == ESP_OK;
    tx_enabled = rx_enabled && i2s_channel_enable(tx_chan) == ESP_OK;
    if (!tx_enabled) {
        teardown();
        return PT_ERR_INVALID;
    }

    if (xTaskCreatePinnedToCore(output_task, "pt_audio_out", TASK_STACK, NULL, TASK_PRIORITY, &out_task,
                                TASK_CORE) != pdPASS) {
        teardown();
        return PT_ERR_NOMEM;
    }

    started = true;
    ESP_LOGI(TAG, "audio started: mics %d Hz, speaker %d Hz, queue %d s", MIC_RATE, SPK_OUT_RATE, QUEUE_SECONDS);
    return PT_OK;
}

size_t pt_audio_read_mics(int16_t *left, int16_t *right, size_t frames, uint32_t timeout_ms)
{
    if (!started)
        return 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    size_t done = 0;
    while (done < frames) {
        int64_t left_us = deadline - esp_timer_get_time();
        uint32_t wait_ms = left_us > 0 ? (uint32_t)((left_us + 999) / 1000) : 0;
        size_t want = frames - done;
        if (want > MIC_CHUNK_FRAMES)
            want = MIC_CHUNK_FRAMES;
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(rx_chan, rx_buf, want * 2 * sizeof(int32_t), &bytes, wait_ms);
        size_t got = bytes / (2 * sizeof(int32_t));
        if (got > 0)
            done += pt_pcm_unpack_i2s32_stereo(rx_buf, got, left + done, right + done);
        if (err != ESP_OK && got == 0)
            break;
        if (wait_ms == 0 && done < frames)
            break;
    }
    return done;
}

size_t pt_audio_play(const int16_t *pcm24k, size_t n)
{
    if (!started || n == 0)
        return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t pushed = pt_pcm_queue_push(queue, pcm24k, n);
    if (pushed > 0) {
        stream_open = true;
        stream_ended = false;
    }
    xSemaphoreGive(lock);
    return pushed;
}

size_t pt_audio_play_space(void)
{
    if (!started)
        return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t space = pt_pcm_queue_free(queue);
    xSemaphoreGive(lock);
    return space;
}

void pt_audio_play_end(void)
{
    if (!started)
        return;
    xSemaphoreTake(lock, portMAX_DELAY);
    stream_ended = true;
    xSemaphoreGive(lock);
}

void pt_audio_stop_playback(void)
{
    if (!started)
        return;
    xSemaphoreTake(lock, portMAX_DELAY);
    pt_pcm_queue_clear(queue);
    stream_open = false;
    stream_ended = false;
    stop_req = true;
    xSemaphoreGive(lock);
}

bool pt_audio_playing(void)
{
    if (!started)
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool playing = stream_open;
    xSemaphoreGive(lock);
    return playing;
}

void pt_audio_set_volume(float volume)
{
    gain = pt_volume_gain(volume);
}

void pt_audio_cue(pt_cue_t c)
{
    const uint8_t *start;
    const uint8_t *end;
    switch (c) {
    case PT_CUE_WAKE:
        start = wake_raw_start;
        end = wake_raw_end;
        break;
    case PT_CUE_END:
        start = end_raw_start;
        end = end_raw_end;
        break;
    case PT_CUE_TEST:
        if (test_sound == NULL) {
            int16_t *pcm = psram_alloc(PT_TEST_SOUND_SAMPLES * sizeof(int16_t));
            if (pcm == NULL || pt_test_sound(pcm, PT_TEST_SOUND_SAMPLES) != PT_TEST_SOUND_SAMPLES) {
                psram_free(pcm);
                ESP_LOGW(TAG, "test sound skipped, no memory");
                return;
            }
            test_sound = pcm;
        }
        start = (const uint8_t *)test_sound;
        end = start + PT_TEST_SOUND_SAMPLES * sizeof(int16_t);
        break;
    default:
        return;
    }
    if (!started)
        return;
    size_t samples = (size_t)(end - start) / sizeof(int16_t);
    if (samples == 0)
        return;
    int16_t chunk[SPK_CHUNK];
    xSemaphoreTake(lock, portMAX_DELAY);
    if (pt_pcm_queue_free(queue) < samples) {
        xSemaphoreGive(lock);
        ESP_LOGW(TAG, "cue skipped, the speaker queue is full");
        return;
    }
    for (size_t done = 0; done < samples;) {
        size_t take = samples - done;
        if (take > SPK_CHUNK)
            take = SPK_CHUNK;
        memcpy(chunk, start + done * sizeof(int16_t), take * sizeof(int16_t));
        size_t pushed = pt_pcm_queue_push(queue, chunk, take);
        if (pushed == 0)
            break;
        done += pushed;
    }
    stream_open = true;
    stream_ended = true;
    xSemaphoreGive(lock);
}
