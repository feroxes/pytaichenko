#include "pt_afe.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "pt_chan.h"

#define FETCH_TIMEOUT_MS 100
#define AFE_CORE 1
#define AFE_PRIORITY 5
#define AFE_RINGBUF_FRAMES 8

static const char *TAG = "pt_afe";

static const esp_afe_sr_iface_t *iface;
static esp_afe_sr_data_t *afe;
static int feed_channels;
static size_t feed_chunk;
static size_t fetch_chunk;
static int16_t *feed_buf;
static int16_t *stage_left;
static int16_t *stage_right;
static size_t staged;
static size_t owed;
static pt_chan_t picker;
static const char *mode = "off";

static void *aligned(size_t bytes)
{
    return heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_8BIT);
}

static afe_config_t *make_config(bool two_mics)
{
    afe_config_t *cfg = afe_config_init(two_mics ? "MM" : "M", NULL, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!cfg) return NULL;
    cfg->aec_init = false;
    cfg->se_init = two_mics;
    cfg->ns_init = !two_mics;
    cfg->afe_ns_mode = AFE_NS_MODE_WEBRTC;
    cfg->vad_init = two_mics;
    cfg->vad_model_name = NULL;
    cfg->vad_enable_channel_trigger = two_mics;
    cfg->wakenet_init = false;
    cfg->agc_init = false;
    cfg->debug_init = false;
    cfg->afe_linear_gain = 1.0f;
    cfg->afe_perferred_core = AFE_CORE;
    cfg->afe_perferred_priority = AFE_PRIORITY;
    cfg->afe_ringbuf_size = AFE_RINGBUF_FRAMES;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    return afe_config_check(cfg);
}

static bool create(bool two_mics)
{
    afe_config_t *cfg = make_config(two_mics);
    if (!cfg) return false;
    if (two_mics && !cfg->se_init) {
        afe_config_free(cfg);
        return false;
    }
    if (!two_mics && !cfg->ns_init) {
        afe_config_free(cfg);
        return false;
    }
    iface = esp_afe_handle_from_config(cfg);
    afe = iface ? iface->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (!afe) {
        iface = NULL;
        return false;
    }
    feed_channels = two_mics ? 2 : 1;
    return true;
}

pt_err_t pt_afe_init(void)
{
    if (afe) return PT_OK;
    bool two = create(true);
    if (two) {
        mode = "two-mic bss";
    } else if (create(false)) {
        mode = "best-mic ns";
    } else {
        ESP_LOGE(TAG, "no usable front end");
        return PT_ERR_INVALID;
    }
    feed_chunk = (size_t)iface->get_feed_chunksize(afe);
    fetch_chunk = (size_t)iface->get_fetch_chunksize(afe);
    feed_buf = aligned(feed_chunk * (size_t)feed_channels * sizeof(int16_t));
    stage_left = aligned(feed_chunk * sizeof(int16_t));
    stage_right = aligned(feed_chunk * sizeof(int16_t));
    if (!feed_buf || !stage_left || !stage_right || feed_chunk == 0 || fetch_chunk == 0) {
        iface->destroy(afe);
        afe = NULL;
        iface = NULL;
        free(feed_buf);
        free(stage_left);
        free(stage_right);
        feed_buf = stage_left = stage_right = NULL;
        mode = "off";
        return PT_ERR_NOMEM;
    }
    staged = 0;
    owed = 0;
    pt_chan_init(&picker);
    ESP_LOGI(TAG, "mode %s, chunk %u in, %u out", mode, (unsigned)feed_chunk, (unsigned)fetch_chunk);
    return PT_OK;
}

size_t pt_afe_chunk(void)
{
    return afe ? feed_chunk : 0;
}

const char *pt_afe_mode(void)
{
    return mode;
}

static void feed_chunk_now(void)
{
    if (feed_channels == 2) {
        for (size_t i = 0; i < feed_chunk; i++) {
            feed_buf[2 * i] = stage_left[i];
            feed_buf[2 * i + 1] = stage_right[i];
        }
    } else {
        int pick = pt_chan_update(&picker, stage_left, stage_right, feed_chunk);
        memcpy(feed_buf, pick ? stage_right : stage_left, feed_chunk * sizeof(int16_t));
    }
    iface->feed(afe, feed_buf);
    owed += feed_chunk;
}

static size_t drain(int16_t *out, size_t cap)
{
    size_t written = 0;
    while (owed >= fetch_chunk && cap - written >= fetch_chunk) {
        afe_fetch_result_t *res = iface->fetch_with_delay(afe, pdMS_TO_TICKS(FETCH_TIMEOUT_MS));
        if (!res || res->ret_value != ESP_OK || !res->data || res->data_size <= 0) break;
        size_t got = (size_t)res->data_size / sizeof(int16_t);
        if (got == 0) break;
        size_t n = got;
        if (n > cap - written) {
            ESP_LOGW(TAG, "front end returned %u samples, %u did not fit and were dropped", (unsigned)got,
                     (unsigned)(got - (cap - written)));
            n = cap - written;
        }
        memcpy(out + written, res->data, n * sizeof(int16_t));
        written += n;
        owed -= got < owed ? got : owed;
    }
    return written;
}

size_t pt_afe_process(const int16_t *left, const int16_t *right, size_t frames, int16_t *out, size_t cap)
{
    if (!afe || !left || !right || !out) return 0;
    size_t written = 0;
    size_t used = 0;
    while (used < frames) {
        size_t take = feed_chunk - staged;
        if (take > frames - used) take = frames - used;
        memcpy(stage_left + staged, left + used, take * sizeof(int16_t));
        memcpy(stage_right + staged, right + used, take * sizeof(int16_t));
        staged += take;
        used += take;
        if (staged == feed_chunk) {
            feed_chunk_now();
            staged = 0;
            written += drain(out + written, cap - written);
        }
    }
    return written;
}
