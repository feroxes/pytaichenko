#include "pt_app.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "pt_afe.h"
#include "pt_appstate.h"
#include "pt_audio.h"
#include "pt_dsp.h"
#include "pt_session.h"
#include "pt_shell.h"
#include "pt_store.h"
#include "pt_turn.h"
#include "pt_ui.h"
#include "pt_wake.h"
#include "pt_wifi.h"
#include "pt_ws.h"

#define MIC_FRAMES 512
#define MIC_OUT_CAP (MIC_FRAMES + 512)
#define MIC_READ_MS 100
#define MIC_STREAM_BYTES (PT_CONVO_MIC_RATE * sizeof(int16_t))
#define MIC_TASK_STACK 12288
#define MIC_TASK_PRIO 8
#define MIC_TASK_CORE 1
#define CONVO_TASK_STACK 8192
#define CONVO_TASK_PRIO 6
#define CONVO_TASK_CORE 0
#define WORK_TASK_STACK 6144
#define WORK_TASK_PRIO 2
#define FALLBACK_TASK_STACK 3072
#define MSG_QUEUE_LEN 16
#define JOB_QUEUE_LEN 4
#define DEV_FIFO_LEN 16
#define INBOX_MAX_BYTES (5u * 1024u * 1024u)
#define INBOX_BATCH 32
#define WIFI_POLL_MS 100u
#define BUSY_POLL_MS 10u
#define START_GAP_MS 100u
#define START_LIMIT_MS 1500u
#define HOLD_LIMIT_MS 8000u
#define ONESHOT_MS 1200u
#define PLAY_WAIT_MS 10
#define PLAY_STALL_MS 3000u
#define WAKE_RETRY_MS 30000u
#define OVERFLOW_LOG_MS 5000u
#define HOLD_WAIT_MS 2000u

typedef enum { MIC_OFF, MIC_WAKE, MIC_CONVO } mic_mode_t;

typedef enum { MSG_WAKE, MSG_BUTTON, MSG_BUTTON_LONG } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    float probability;
} msg_t;

typedef enum { JOB_VOLUME, JOB_RESET_PASSWORD } job_kind_t;

typedef struct {
    job_kind_t kind;
    float volume;
} job_t;

typedef enum { IN_OPENED, IN_MESSAGE, IN_CLOSED } in_kind_t;

typedef struct inbox_node {
    struct inbox_node *next;
    uint32_t gen;
    in_kind_t kind;
    pt_ws_status_t why;
    char *json;
    size_t len;
} inbox_node_t;

typedef struct {
    pt_dev_event_t ev;
    bool follow_up;
    bool has_code;
    char code[32];
} dev_item_t;

static const char *TAG = "app";

static bool s_started;
static bool s_afe_ok;
static bool s_audio_ok;
static TaskHandle_t s_convo_task;
static TaskHandle_t s_mic_task;
static QueueHandle_t s_msgs;
static QueueHandle_t s_jobs;
static StreamBufferHandle_t s_mic;
static SemaphoreHandle_t s_pending_lock;
static pt_settings_t *s_snap;
static pt_settings_t *s_pending;
static bool s_pending_fresh;
static bool s_volume_fresh;
static float s_pending_volume;

static atomic_int s_mic_mode;
static atomic_int s_mic_gain_cdb;
static atomic_int s_peak_left;
static atomic_int s_peak_right;
static bool s_peaks_armed;
static atomic_bool s_interrupt;
static atomic_bool s_turn;
static atomic_bool s_hold;
static atomic_uint s_hold_asked;
static atomic_uint s_hold_done;
static atomic_uint s_gen = 1;
static atomic_uint s_lost_gen;
static uint32_t s_overflow_gen;

static portMUX_TYPE s_inbox_mux = portMUX_INITIALIZER_UNLOCKED;
static inbox_node_t *s_in_head;
static inbox_node_t *s_in_tail;
static size_t s_in_bytes;

static int16_t s_left[MIC_FRAMES];
static int16_t s_right[MIC_FRAMES];
static int16_t s_out[MIC_OUT_CAP];
static int16_t s_mic_buf[MIC_FRAMES];

static pt_appstate_t *s_app;
static pt_convo_t *s_convo;
static dev_item_t s_devs[DEV_FIFO_LEN];
static size_t s_dev_head;
static size_t s_dev_len;
static bool s_rt_open;
static bool s_opened;
static bool s_closed_delivered;
static bool s_send_failed;
static bool s_turn_started;
static bool s_start_pending;
static bool s_quiet_set;
static uint32_t s_start_limit;
static uint32_t s_quiet_at;
static bool s_play_abort;
static bool s_stalling;
static uint32_t s_stall_since;
static bool s_held_on;
static dev_item_t s_held;
static uint32_t s_held_limit;
static bool s_ring_known;
static pt_ring_state_t s_ring_shown;
static bool s_oneshot_on;
static uint32_t s_oneshot_end;
static bool s_wifi;
static bool s_wake_on;
static int s_ready_logged = -1;
static char s_wake_word[sizeof(((pt_sound_t *)0)->wake_word)];
static bool s_wake_retry_on;
static uint32_t s_wake_retry_at;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void notify(void)
{
    if (s_convo_task != NULL) {
        xTaskNotifyGive(s_convo_task);
    }
}

static bool has_key(const pt_settings_t *s)
{
    return s->openai.api_key[0] != '\0';
}

static void inbox_push(in_kind_t kind, char *json, size_t len, pt_ws_status_t why)
{
    uint32_t gen = atomic_load(&s_gen);
    bool lost = atomic_load(&s_lost_gen) == gen;
    inbox_node_t *n = lost ? NULL : heap_caps_malloc(sizeof(*n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (n == NULL && !lost) {
        n = malloc(sizeof(*n));
    }
    bool queued = false;
    if (n != NULL) {
        n->next = NULL;
        n->gen = gen;
        n->kind = kind;
        n->why = why;
        n->json = json;
        n->len = kind == IN_MESSAGE ? len : 0;
        portENTER_CRITICAL(&s_inbox_mux);
        if (s_in_bytes + n->len <= INBOX_MAX_BYTES) {
            if (s_in_tail != NULL) {
                s_in_tail->next = n;
            } else {
                s_in_head = n;
            }
            s_in_tail = n;
            s_in_bytes += n->len;
            queued = true;
        }
        portEXIT_CRITICAL(&s_inbox_mux);
    }
    if (!queued) {
        free(n);
        free(json);
        atomic_store(&s_lost_gen, gen);
    }
    notify();
}

static inbox_node_t *inbox_pop(void)
{
    portENTER_CRITICAL(&s_inbox_mux);
    inbox_node_t *n = s_in_head;
    if (n != NULL) {
        s_in_head = n->next;
        if (s_in_head == NULL) {
            s_in_tail = NULL;
        }
        s_in_bytes -= n->len;
    }
    portEXIT_CRITICAL(&s_inbox_mux);
    return n;
}

static bool inbox_empty(void)
{
    portENTER_CRITICAL(&s_inbox_mux);
    bool empty = s_in_head == NULL;
    portEXIT_CRITICAL(&s_inbox_mux);
    return empty;
}

static void ws_opened(void *ctx)
{
    (void)ctx;
    inbox_push(IN_OPENED, NULL, 0, PT_WS_OK);
}

static void ws_message(void *ctx, char *json, size_t len)
{
    (void)ctx;
    inbox_push(IN_MESSAGE, json, len, PT_WS_OK);
}

static void ws_closed(void *ctx, pt_ws_status_t why)
{
    (void)ctx;
    inbox_push(IN_CLOSED, NULL, 0, why);
}

static const char *why_text(pt_ws_status_t why)
{
    switch (why) {
    case PT_WS_BAD_KEY:
        return "OpenAI refused the key";
    case PT_WS_NO_NETWORK:
        return "OpenAI could not be reached";
    case PT_WS_REFUSED:
        return "OpenAI refused the connection";
    case PT_WS_TIMEOUT:
        return "OpenAI did not answer in time";
    case PT_WS_REJECTED:
        return "OpenAI did not accept the request";
    default:
        return "the connection to OpenAI closed";
    }
}

static void post_job(job_kind_t kind, float volume)
{
    job_t j = {.kind = kind, .volume = volume};
    if (s_jobs == NULL || xQueueSend(s_jobs, &j, 0) != pdTRUE) {
        pt_shell_log(kind == JOB_VOLUME ? "the new volume could not be saved" : "the password could not be reset");
    }
}

static void update_turn(void)
{
    atomic_store(&s_turn, pt_appstate_in_convo(s_app));
}

static void set_mic_gain(float db)
{
    atomic_store(&s_mic_gain_cdb, (int)lroundf(db * 100.0f));
}

static void set_mic_mode(void)
{
    mic_mode_t m = s_wake_on ? MIC_WAKE : s_turn_started ? MIC_CONVO : MIC_OFF;
    atomic_store(&s_mic_mode, (int)m);
}

static void port_rt_open(void *ctx, const pt_settings_t *s)
{
    (void)ctx;
    atomic_fetch_add(&s_gen, 1);
    s_rt_open = true;
    s_opened = false;
    s_closed_delivered = false;
    s_send_failed = false;
    update_turn();
    pt_ws_open(s->openai.api_key, s->openai.model);
}

static void port_rt_send(void *ctx, const char *json)
{
    (void)ctx;
    if (!pt_ws_send(json)) {
        s_send_failed = true;
    }
}

static void port_rt_close(void *ctx)
{
    (void)ctx;
    pt_ws_close();
    atomic_fetch_add(&s_gen, 1);
    s_rt_open = false;
    s_opened = false;
    s_closed_delivered = false;
    s_send_failed = false;
    update_turn();
}

static size_t sink_push(void *ctx, const int16_t *pcm, size_t n)
{
    (void)ctx;
    return pt_audio_play(pcm, n);
}

static bool sink_wait(void *ctx)
{
    (void)ctx;
    if (atomic_load(&s_interrupt)) {
        s_play_abort = true;
        return false;
    }
    if (pt_audio_play_space() > 0) {
        s_stalling = false;
        return true;
    }
    uint32_t now = now_ms();
    if (!s_stalling) {
        s_stalling = true;
        s_stall_since = now;
    } else if (now - s_stall_since >= PLAY_STALL_MS) {
        pt_shell_log("the speaker stopped taking sound; the rest of the answer is skipped");
        s_play_abort = true;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PLAY_WAIT_MS));
    return true;
}

static const pt_app_sink_t SINK = {.push = sink_push, .wait = sink_wait, .ctx = NULL};

static void port_play(void *ctx, const int16_t *pcm, size_t n)
{
    (void)ctx;
    if (s_play_abort) {
        return;
    }
    s_stalling = false;
    pt_app_feed(&SINK, pcm, n);
}

static void port_play_end(void *ctx)
{
    (void)ctx;
    pt_audio_play_end();
}

static void log_raw_peaks(void)
{
    if (!s_peaks_armed) {
        return;
    }
    s_peaks_armed = false;
    char left[40];
    char right[40];
    pt_peak_text(atomic_exchange(&s_peak_left, 0), left, sizeof(left));
    pt_peak_text(atomic_exchange(&s_peak_right, 0), right, sizeof(right));
    pt_shell_log("raw microphones: left %s, right %s", left, right);
}

static void port_device(void *ctx, pt_dev_event_t ev, bool follow_up, const char *code)
{
    (void)ctx;
    if (ev == PT_DEV_ANSWERED || ev == PT_DEV_ENDED || ev == PT_DEV_FAILED) {
        log_raw_peaks();
    }
    if (s_dev_len == DEV_FIFO_LEN) {
        ESP_LOGE(TAG, "device event %d dropped", (int)ev);
        return;
    }
    dev_item_t *d = &s_devs[(s_dev_head + s_dev_len) % DEV_FIFO_LEN];
    s_dev_len++;
    d->ev = ev;
    d->follow_up = follow_up;
    d->has_code = code != NULL;
    snprintf(d->code, sizeof(d->code), "%s", code != NULL ? code : "");
}

static void port_set_volume(void *ctx, float volume)
{
    (void)ctx;
    pt_audio_set_volume(volume);
    s_snap->sound.volume = volume;
    post_job(JOB_VOLUME, volume);
}

static const pt_port_t PORT = {
    .rt_open = port_rt_open,
    .rt_send = port_rt_send,
    .rt_close = port_rt_close,
    .play = port_play,
    .play_end = port_play_end,
    .device = port_device,
    .set_volume = port_set_volume,
    .ctx = NULL,
};

static void show_ring(pt_ring_state_t r, bool force, uint32_t now)
{
    bool one_shot = r == PT_RING_ERROR || r == PT_RING_RESET;
    if (!one_shot && !force && s_oneshot_on && !pt_time_due(s_oneshot_end, now)) {
        return;
    }
    if (s_ring_known && r == s_ring_shown && !force && !one_shot) {
        return;
    }
    pt_ui_ring(r);
    s_ring_shown = r;
    s_ring_known = true;
    s_oneshot_on = one_shot;
    s_oneshot_end = pt_time_after(now, ONESHOT_MS);
}

static void log_readiness(void)
{
    int ready = pt_appstate_ready(s_app) ? 1 : 0;
    if (ready == s_ready_logged) {
        return;
    }
    s_ready_logged = ready;
    if (ready) {
        pt_shell_log("ready, listening for the wake word %s", s_wake_word);
    } else if (!s_wifi) {
        pt_shell_log("not ready: no Wi-Fi");
    } else {
        pt_shell_log("not ready: the OpenAI key is not set");
    }
}

static void app_event(const pt_app_ev_t *ev, const char *why)
{
    uint32_t now = now_ms();
    bool was_in = pt_appstate_in_convo(s_app);
    pt_app_out_t o = pt_appstate_on(s_app, ev);
    if (o.stop_convo) {
        s_start_pending = false;
        s_held_on = false;
        s_turn_started = false;
        set_mic_mode();
        pt_convo_stop(s_convo, now_ms());
        pt_audio_stop_playback();
    }
    if (o.play_wake_sound) {
        pt_audio_cue(PT_CUE_WAKE);
    }
    if (o.play_end_sound) {
        pt_audio_cue(PT_CUE_END);
    }
    show_ring(o.ring, o.reset_password, now);
    s_wake_on = o.wake_listening;
    bool in = pt_appstate_in_convo(s_app);
    if (!in) {
        s_turn_started = false;
        s_start_pending = false;
        s_held_on = false;
    }
    if (o.start_convo) {
        s_start_pending = true;
        s_quiet_set = false;
        s_start_limit = pt_time_after(now, START_LIMIT_MS);
    }
    set_mic_mode();
    if (o.reset_password) {
        post_job(JOB_RESET_PASSWORD, 0.0f);
    }
    if (!was_in && in) {
        pt_shell_log("conversation started (%s)", why);
    } else if (was_in && !in) {
        pt_shell_log("conversation ended (%s)", why);
        pt_shell_log_health("after the conversation");
    }
    update_turn();
    log_readiness();
}

static void simple_event(pt_app_ev_kind_t kind, const char *why)
{
    pt_app_ev_t ev = {.kind = kind};
    app_event(&ev, why);
}

static void deliver_dev(const dev_item_t *d)
{
    pt_app_ev_t ev = {
        .kind = PT_APP_EV_DEV,
        .dev = d->ev,
        .follow_up = d->follow_up,
        .code = d->has_code ? d->code : NULL,
    };
    char why[48];
    if (d->ev == PT_DEV_FAILED) {
        snprintf(why, sizeof(why), "error %s", d->has_code ? d->code : "unknown");
    } else {
        snprintf(why, sizeof(why), "%s", d->ev == PT_DEV_ANSWERED ? "answered" : "ended");
    }
    app_event(&ev, why);
}

static bool terminal(pt_dev_event_t ev)
{
    return ev == PT_DEV_ANSWERED || ev == PT_DEV_ENDED || ev == PT_DEV_FAILED;
}

static void pump_devices(void)
{
    while (s_dev_len > 0) {
        dev_item_t d = s_devs[s_dev_head];
        s_dev_head = (s_dev_head + 1) % DEV_FIFO_LEN;
        s_dev_len--;
        if (terminal(d.ev) && pt_appstate_in_convo(s_app) && pt_audio_playing()) {
            s_held = d;
            if (!s_held_on) {
                s_held_on = true;
                s_held_limit = pt_time_after(now_ms(), HOLD_LIMIT_MS);
            }
            continue;
        }
        deliver_dev(&d);
    }
}

static void deliver_closed(const char *why)
{
    if (!s_rt_open || s_closed_delivered) {
        return;
    }
    s_closed_delivered = true;
    if (s_opened) {
        pt_convo_rt_closed(s_convo, now_ms());
    } else {
        pt_convo_rt_failed(s_convo, why, now_ms());
    }
}

static void after_core(void)
{
    for (;;) {
        pump_devices();
        if (s_rt_open && !s_closed_delivered && s_send_failed) {
            deliver_closed("sending to OpenAI failed");
            continue;
        }
        return;
    }
}

static void poll_wifi(void)
{
    bool up = pt_wifi_connected();
    if (up == s_wifi) {
        return;
    }
    s_wifi = up;
    simple_event(up ? PT_APP_EV_WIFI_UP : PT_APP_EV_WIFI_DOWN, up ? "Wi-Fi is back" : "Wi-Fi lost");
    after_core();
}

static void update_warm(void)
{
    pt_convo_warm(s_convo, s_wifi && !atomic_load(&s_hold), now_ms());
    after_core();
}

static void wake_apply(bool announce)
{
    const char *want = s_snap->sound.wake_word;
    const char *have = pt_wake_current();
    if (have != NULL && strcmp(have, want) == 0) {
        s_wake_retry_on = false;
        return;
    }
    bool ok = pt_wake_init() == PT_OK && pt_wake_select(want) == PT_OK;
    have = pt_wake_current();
    snprintf(s_wake_word, sizeof(s_wake_word), "%s", have != NULL ? have : "none");
    if (ok) {
        s_wake_retry_on = false;
        if (announce) {
            pt_shell_log("wake word is now %s", s_wake_word);
        }
        return;
    }
    if (!s_wake_retry_on) {
        if (have != NULL) {
            pt_shell_log("wake word %s could not be loaded; %s stays on; trying again every 30 s", want, have);
        } else {
            pt_shell_log("wake word %s could not be loaded; the button still works; trying again every 30 s", want);
        }
    }
    s_wake_retry_on = true;
    s_wake_retry_at = pt_time_after(now_ms(), WAKE_RETRY_MS);
}

static void run_wake_retry(void)
{
    if (s_wake_retry_on && pt_time_due(s_wake_retry_at, now_ms()) && !pt_appstate_in_convo(s_app)) {
        wake_apply(true);
        log_readiness();
    }
}

static void take_settings(void)
{
    bool fresh = false;
    bool volume = false;
    float v = 0.0f;
    xSemaphoreTake(s_pending_lock, portMAX_DELAY);
    if (s_pending_fresh) {
        memcpy(s_snap, s_pending, sizeof(*s_snap));
        s_pending_fresh = false;
        fresh = true;
    }
    if (s_volume_fresh) {
        v = s_pending_volume;
        s_volume_fresh = false;
        volume = true;
    }
    xSemaphoreGive(s_pending_lock);
    if (fresh) {
        s_wake_retry_on = false;
        wake_apply(true);
        set_mic_gain(s_snap->sound.mic_gain_db);
        pt_audio_set_volume(s_snap->sound.volume);
        pt_convo_settings(s_convo, s_snap, now_ms());
        after_core();
        pt_appstate_settings(s_app, has_key(s_snap));
        simple_event(PT_APP_EV_SETTINGS, "the OpenAI key was removed");
        after_core();
    }
    if (volume) {
        s_snap->sound.volume = v;
        pt_convo_settings(s_convo, s_snap, now_ms());
        after_core();
    }
}

static void take_msgs(void)
{
    msg_t m;
    while (xQueueReceive(s_msgs, &m, 0) == pdTRUE) {
        if (atomic_load(&s_hold)) {
            continue;
        }
        switch (m.kind) {
        case MSG_WAKE:
            pt_shell_log("wake word %s heard, probability %.2f", s_wake_word, (double)m.probability);
            simple_event(PT_APP_EV_WAKE, "wake word");
            break;
        case MSG_BUTTON:
            atomic_store(&s_interrupt, false);
            simple_event(PT_APP_EV_BUTTON, pt_appstate_in_convo(s_app) ? "stopped with the button" : "button");
            break;
        case MSG_BUTTON_LONG:
            simple_event(PT_APP_EV_BUTTON_LONG, "button held");
            break;
        }
        after_core();
    }
}

static void handle_node(const inbox_node_t *n)
{
    switch (n->kind) {
    case IN_OPENED:
        if (!s_opened && !s_closed_delivered) {
            s_opened = true;
            pt_convo_rt_opened(s_convo, now_ms());
        }
        break;
    case IN_MESSAGE:
        if (!s_closed_delivered) {
            pt_convo_rt_message(s_convo, n->json, n->len, now_ms());
        }
        break;
    case IN_CLOSED:
        deliver_closed(why_text(n->why));
        break;
    }
}

static void take_inbox(void)
{
    for (int i = 0; i < INBOX_BATCH; i++) {
        inbox_node_t *n = inbox_pop();
        if (n == NULL) {
            break;
        }
        if (s_rt_open && n->gen == atomic_load(&s_gen)) {
            handle_node(n);
        }
        free(n->json);
        free(n);
        after_core();
    }
    uint32_t gen = atomic_load(&s_gen);
    if (s_rt_open && !s_closed_delivered && atomic_load(&s_lost_gen) == gen && gen != s_overflow_gen &&
        inbox_empty()) {
        s_overflow_gen = gen;
        pt_convo_rt_overflow(s_convo, now_ms());
        after_core();
    }
}

static void take_mic(void)
{
    for (;;) {
        size_t got = xStreamBufferReceive(s_mic, s_mic_buf, sizeof(s_mic_buf), 0);
        if (got == 0) {
            return;
        }
        if (s_turn_started) {
            pt_convo_mic(s_convo, s_mic_buf, got / sizeof(int16_t), now_ms());
            after_core();
        }
    }
}

static void run_tick(void)
{
    uint32_t d = pt_convo_next_deadline(s_convo);
    if (d != PT_DEADLINE_NONE && pt_time_due(d, now_ms())) {
        pt_convo_tick(s_convo, now_ms());
        after_core();
    }
}

static void run_held(void)
{
    if (s_held_on && (!pt_audio_playing() || pt_time_due(s_held_limit, now_ms()))) {
        dev_item_t d = s_held;
        s_held_on = false;
        deliver_dev(&d);
        after_core();
    }
}

static void run_start(void)
{
    if (!s_start_pending || s_held_on) {
        return;
    }
    uint32_t now = now_ms();
    if (pt_audio_playing() && !pt_time_due(s_start_limit, now)) {
        s_quiet_set = false;
        return;
    }
    if (!s_quiet_set) {
        s_quiet_set = true;
        s_quiet_at = pt_time_after(now, START_GAP_MS);
    }
    if (!pt_time_due(s_quiet_at, now)) {
        return;
    }
    s_start_pending = false;
    while (xStreamBufferReceive(s_mic, s_mic_buf, sizeof(s_mic_buf), 0) > 0) {
    }
    s_play_abort = false;
    atomic_store(&s_interrupt, false);
    atomic_store(&s_peak_left, 0);
    atomic_store(&s_peak_right, 0);
    s_peaks_armed = true;
    s_turn_started = true;
    set_mic_mode();
    pt_convo_start(s_convo, now_ms());
    after_core();
}

static void run_hold(void)
{
    unsigned asked = atomic_load(&s_hold_asked);
    if (asked == atomic_load(&s_hold_done)) {
        return;
    }
    update_warm();
    if (atomic_load(&s_hold) && !pt_appstate_in_convo(s_app) && s_rt_open) {
        pt_shell_log("closing the kept OpenAI connection for the update");
        pt_convo_close_idle(s_convo, now_ms());
        after_core();
    }
    atomic_store(&s_hold_done, asked);
}

static void run_oneshot(void)
{
    uint32_t now = now_ms();
    if (s_oneshot_on && pt_time_due(s_oneshot_end, now)) {
        s_oneshot_on = false;
        show_ring(pt_appstate_ring(s_app), false, now);
    }
}

static TickType_t wait_ticks(void)
{
    if (!inbox_empty()) {
        return 0;
    }
    uint32_t now = now_ms();
    uint32_t ms = WIFI_POLL_MS;
    uint32_t d = pt_convo_next_deadline(s_convo);
    if (d != PT_DEADLINE_NONE) {
        uint32_t left = pt_time_due(d, now) ? 0 : d - now;
        if (left < ms) {
            ms = left;
        }
    }
    if ((s_start_pending || s_held_on || s_oneshot_on) && ms > BUSY_POLL_MS) {
        ms = BUSY_POLL_MS;
    }
    return pdMS_TO_TICKS(ms);
}

static void convo_task(void *arg)
{
    (void)arg;
    pt_convo_settings(s_convo, s_snap, now_ms());
    after_core();
    s_wifi = pt_wifi_connected();
    simple_event(s_wifi ? PT_APP_EV_WIFI_UP : PT_APP_EV_SETTINGS, "start");
    for (;;) {
        ulTaskNotifyTake(pdTRUE, wait_ticks());
        poll_wifi();
        update_warm();
        take_settings();
        take_msgs();
        take_inbox();
        take_mic();
        run_tick();
        run_held();
        run_start();
        run_hold();
        run_oneshot();
        run_wake_retry();
    }
}

static void keep_peak(atomic_int *peak, int level)
{
    int cur = atomic_load(peak);
    while (level > cur && !atomic_compare_exchange_weak(peak, &cur, level)) {
    }
}

static void mic_task(void *arg)
{
    (void)arg;
    mic_mode_t last = MIC_OFF;
    bool paused = false;
    int gain_cdb = 0;
    float gain = 1.0f;
    uint32_t overflows = pt_audio_mic_overflows();
    uint32_t overflow_logged_at = 0;
    bool overflow_logged = false;
    for (;;) {
        size_t n = pt_audio_read_mics(s_left, s_right, MIC_FRAMES, MIC_READ_MS);
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        uint32_t now_overflows = pt_audio_mic_overflows();
        if (now_overflows != overflows &&
            (!overflow_logged || pt_time_due(pt_time_after(overflow_logged_at, OVERFLOW_LOG_MS), now_ms()))) {
            pt_shell_log("microphone data was lost %u times (%u in total); the microphone task was late",
                         (unsigned)(now_overflows - overflows), (unsigned)now_overflows);
            overflows = now_overflows;
            overflow_logged = true;
            overflow_logged_at = now_ms();
        }
        if ((mic_mode_t)atomic_load(&s_mic_mode) == MIC_CONVO && !pt_audio_playing()) {
            keep_peak(&s_peak_left, pt_peak_abs(s_left, n));
            keep_peak(&s_peak_right, pt_peak_abs(s_right, n));
        }
        int cdb = atomic_load(&s_mic_gain_cdb);
        if (cdb != gain_cdb) {
            gain_cdb = cdb;
            gain = pt_db_to_gain((float)cdb / 100.0f);
        }
        pt_gain_apply(s_left, n, gain);
        pt_gain_apply(s_right, n, gain);
        size_t m;
        if (s_afe_ok) {
            m = pt_afe_process(s_left, s_right, n, s_out, MIC_OUT_CAP);
        } else {
            memcpy(s_out, s_left, n * sizeof(int16_t));
            m = n;
        }
        mic_mode_t mode = (mic_mode_t)atomic_load(&s_mic_mode);
        if (mode == MIC_WAKE) {
            if (pt_audio_playing()) {
                paused = true;
            } else {
                if (last != MIC_WAKE || paused) {
                    pt_wake_reset();
                    paused = false;
                }
                if (m > 0 && pt_wake_feed(s_out, m)) {
                    msg_t msg = {.kind = MSG_WAKE, .probability = pt_wake_last_probability()};
                    xQueueSend(s_msgs, &msg, 0);
                    notify();
                }
            }
        } else if (mode == MIC_CONVO && m > 0) {
            xStreamBufferSend(s_mic, s_out, m * sizeof(int16_t), 0);
            notify();
        }
        last = mode;
    }
}

static void (*s_reset_hook)(void *ctx);
static void *s_reset_ctx;

void pt_app_on_password_reset(void (*hook)(void *ctx), void *ctx)
{
    s_reset_ctx = ctx;
    s_reset_hook = hook;
}

static void work_task(void *arg)
{
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        pt_settings_t *s = pt_shell_settings();
        pt_shell_settings_lock();
        if (j.kind == JOB_VOLUME) {
            s->sound.volume = j.volume;
        } else {
            memset(s->password_hash, 0, sizeof(s->password_hash));
            if (s_reset_hook != NULL) {
                s_reset_hook(s_reset_ctx);
            }
        }
        pt_err_t err = pt_store_save(s);
        pt_shell_settings_unlock();
        if (j.kind == JOB_RESET_PASSWORD) {
            if (err == PT_OK) {
                pt_shell_log("password reset with the button; create a new one on the settings page");
            } else {
                pt_shell_log("password reset with the button could not be saved");
            }
        }
    }
}

static void on_button(int which, bool long_press)
{
    (void)which;
    if (s_msgs == NULL) {
        return;
    }
    msg_t m = {.kind = long_press ? MSG_BUTTON_LONG : MSG_BUTTON};
    bool was = long_press ? false : atomic_exchange(&s_interrupt, true);
    if (xQueueSend(s_msgs, &m, 0) != pdTRUE) {
        if (!long_press) {
            atomic_store(&s_interrupt, was);
        }
        return;
    }
    notify();
}

static const pt_ws_events_t WS_EVENTS = {
    .opened = ws_opened,
    .message = ws_message,
    .closed = ws_closed,
    .ctx = NULL,
};

static void *psram(size_t bytes)
{
    void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : calloc(1, bytes);
}

static void fallback_task(void *arg)
{
    (void)arg;
    pt_ui_ring(PT_RING_ERROR);
    vTaskDelay(pdMS_TO_TICKS(ONESHOT_MS));
    pt_ui_ring(PT_RING_NOT_READY);
    for (;;) {
        msg_t m;
        if (s_msgs == NULL || xQueueReceive(s_msgs, &m, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(portMAX_DELAY);
            continue;
        }
        if (m.kind == MSG_BUTTON_LONG) {
            pt_ui_ring(PT_RING_RESET);
            post_job(JOB_RESET_PASSWORD, 0.0f);
        } else if (m.kind == MSG_BUTTON) {
            pt_ui_ring(PT_RING_ERROR);
        } else {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(ONESHOT_MS));
        pt_ui_ring(PT_RING_NOT_READY);
    }
}

static pt_err_t start_failed(const char *part, pt_err_t err)
{
    pt_shell_log("the speaker could not start: %s; the ring shows the error and the button can still reset the password",
                 part);
    if (xTaskCreate(fallback_task, "pt_fallback", FALLBACK_TASK_STACK, NULL, WORK_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "fallback task not created");
    }
    return err;
}

static pt_err_t start_parts(void)
{
    if (s_snap == NULL || s_pending == NULL || s_pending_lock == NULL || s_mic == NULL || s_app == NULL) {
        return start_failed("out of memory", PT_ERR_NOMEM);
    }
    pt_err_t err = pt_audio_start();
    if (err != PT_OK) {
        return start_failed("sound", err);
    }
    s_audio_ok = true;
    pt_audio_set_volume(s_snap->sound.volume);
    s_afe_ok = pt_afe_init() == PT_OK;
    if (s_afe_ok) {
        pt_shell_log("microphones: %s", pt_afe_mode());
    } else {
        pt_shell_log("microphones: the sound front end did not start; using the left microphone as is");
    }
    wake_apply(false);
    set_mic_gain(s_snap->sound.mic_gain_db);
    pt_appstate_settings(s_app, has_key(s_snap));
    s_convo = pt_convo_new(&PORT, pt_shell_log_get());
    if (s_convo == NULL) {
        return start_failed("conversation (out of memory)", PT_ERR_NOMEM);
    }
    err = pt_ws_start(&WS_EVENTS);
    if (err != PT_OK) {
        return start_failed("connection to OpenAI", err);
    }
    if (xTaskCreatePinnedToCore(mic_task, "pt_mic", MIC_TASK_STACK, NULL, MIC_TASK_PRIO, &s_mic_task,
                                MIC_TASK_CORE) != pdPASS) {
        return start_failed("microphone task", PT_ERR_NOMEM);
    }
    if (xTaskCreatePinnedToCore(convo_task, "pt_convo", CONVO_TASK_STACK, NULL, CONVO_TASK_PRIO, &s_convo_task,
                                CONVO_TASK_CORE) != pdPASS) {
        return start_failed("conversation task", PT_ERR_NOMEM);
    }
    return PT_OK;
}

pt_err_t pt_app_start(const pt_settings_t *snapshot)
{
    if (snapshot == NULL || s_started) {
        return PT_ERR_INVALID;
    }
    s_msgs = xQueueCreate(MSG_QUEUE_LEN, sizeof(msg_t));
    s_jobs = xQueueCreate(JOB_QUEUE_LEN, sizeof(job_t));
    if (pt_ui_start(on_button) != PT_OK) {
        pt_shell_log("the ring and the buttons could not start");
    }
    if (s_jobs == NULL || xTaskCreate(work_task, "pt_work", WORK_TASK_STACK, NULL, WORK_TASK_PRIO, NULL) != pdPASS) {
        pt_shell_log("the settings task could not start; volume and password changes from the speaker are not saved");
    }
    s_snap = psram(sizeof(*s_snap));
    s_pending = psram(sizeof(*s_pending));
    s_pending_lock = xSemaphoreCreateMutex();
    s_mic = xStreamBufferCreateWithCaps(MIC_STREAM_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_app = pt_appstate_new();
    if (s_snap != NULL) {
        memcpy(s_snap, snapshot, sizeof(*s_snap));
    }
    pt_err_t err = s_msgs == NULL ? start_failed("out of memory", PT_ERR_NOMEM) : start_parts();
    if (err != PT_OK) {
        return err;
    }
    s_started = true;
    return PT_OK;
}

void pt_app_apply(const pt_settings_t *snapshot)
{
    if (!s_started || snapshot == NULL) {
        return;
    }
    xSemaphoreTake(s_pending_lock, portMAX_DELAY);
    memcpy(s_pending, snapshot, sizeof(*s_pending));
    s_pending_fresh = true;
    s_volume_fresh = false;
    xSemaphoreGive(s_pending_lock);
    notify();
}

void pt_app_sound_test(void)
{
    if (!s_started) {
        return;
    }
    pt_audio_cue(PT_CUE_TEST);
}

void pt_app_volume(float v)
{
    if (!s_started) {
        return;
    }
    pt_audio_set_volume(v);
    xSemaphoreTake(s_pending_lock, portMAX_DELAY);
    s_pending_volume = v;
    s_volume_fresh = true;
    xSemaphoreGive(s_pending_lock);
    notify();
}

const char *pt_app_mic_mode(void)
{
    if (s_afe_ok) {
        return pt_afe_mode();
    }
    return s_audio_ok ? "left microphone, no front end" : "off";
}

bool pt_app_turn_active(void)
{
    return atomic_load(&s_turn);
}

void pt_app_hold(bool on)
{
    atomic_store(&s_hold, on);
}

pt_err_t pt_app_quiet_for_update(void)
{
    atomic_store(&s_hold, true);
    if (!s_started) {
        return PT_OK;
    }
    unsigned asked = atomic_fetch_add(&s_hold_asked, 1) + 1;
    notify();
    for (uint32_t waited = 0; atomic_load(&s_hold_done) != asked && waited < HOLD_WAIT_MS; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (atomic_load(&s_hold_done) != asked || atomic_load(&s_turn)) {
        atomic_store(&s_hold, false);
        notify();
        return PT_ERR_TALKING;
    }
    return PT_OK;
}
