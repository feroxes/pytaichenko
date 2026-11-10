#include "pt_ws.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pt_rt.h"

#define WS_TASK_STACK 8192
#define WS_TASK_PRIO 5
#define WS_IO_STACK 8192
#define WS_IO_PRIO 5
#define WS_IO_BUFFER 4096
#define WS_PING_S 15
#define WS_PONG_TIMEOUT_S 8
#define WS_NET_TIMEOUT_MS 10000
#define WS_PROBE_NET_TIMEOUT_MS 3000
#define WS_CONTROL_SLOTS 16
#define WS_SEND_TIMEOUT_MS 5000
#define WS_URL_MAX 192
#define WS_HEADER_MAX 400
#define WS_OP_CONT 0x0
#define WS_OP_TEXT 0x1
#define WS_OP_BINARY 0x2

static const char *TAG = "ws";

typedef struct conn {
    uint32_t gen;
    pt_ws_events_t ev;
    SemaphoreHandle_t lock;
    esp_websocket_client_handle_t client;
    char *key;
    char *model;
    int net_timeout_ms;
    int64_t started_us;
    volatile bool dead;
    volatile bool opened;
    int http_status;
    char *msg;
    size_t msg_len;
    size_t msg_cap;
} conn_t;

typedef enum { CMD_OPEN, CMD_CLOSE, CMD_STOP, CMD_SEND } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    uint32_t gen;
    conn_t *conn;
    char *json;
} cmd_t;

static pt_ws_events_t s_events;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static conn_t *s_current;
static conn_t *s_active;
static uint32_t s_next_gen;
static atomic_int s_pending_sends;

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    for (size_t i = 0; i < n; i++) {
        v[i] = 0;
    }
}

static void wipe_free(char *p)
{
    if (p != NULL) {
        wipe(p, strlen(p));
        free(p);
    }
}

static void *big_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(n);
}

static char *big_copy(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = big_alloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static void *big_grow(void *old, size_t n)
{
    void *p = heap_caps_realloc(old, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : realloc(old, n);
}

static conn_t *conn_new(const pt_ws_events_t *ev, const char *key, const char *model, int net_timeout_ms)
{
    conn_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->ev = *ev;
    c->net_timeout_ms = net_timeout_ms;
    c->lock = xSemaphoreCreateRecursiveMutex();
    c->key = strdup(key);
    c->model = strdup(model);
    if (c->lock == NULL || c->key == NULL || c->model == NULL) {
        wipe_free(c->key);
        free(c->model);
        if (c->lock != NULL) {
            vSemaphoreDelete(c->lock);
        }
        free(c);
        return NULL;
    }
    return c;
}

static void conn_kill(conn_t *c)
{
    if (c == NULL) {
        return;
    }
    xSemaphoreTakeRecursive(c->lock, portMAX_DELAY);
    c->dead = true;
    xSemaphoreGiveRecursive(c->lock);
}

static void conn_free(conn_t *c)
{
    if (c == NULL) {
        return;
    }
    conn_kill(c);
    if (c->client != NULL) {
        esp_websocket_client_destroy(c->client);
    }
    free(c->msg);
    wipe_free(c->key);
    free(c->model);
    vSemaphoreDelete(c->lock);
    free(c);
}

static void finish(conn_t *c, pt_ws_status_t why)
{
    xSemaphoreTakeRecursive(c->lock, portMAX_DELAY);
    if (!c->dead) {
        c->dead = true;
        free(c->msg);
        c->msg = NULL;
        c->msg_len = 0;
        c->msg_cap = 0;
        ESP_LOGI(TAG, "connection %u ended, reason %d", (unsigned)c->gen, (int)why);
        if (c->ev.closed != NULL) {
            c->ev.closed(c->ev.ctx, why);
        }
    }
    xSemaphoreGiveRecursive(c->lock);
}

static pt_ws_status_t lost_reason(const conn_t *c, esp_websocket_error_type_t type)
{
    if (!c->opened) {
        pt_ws_status_t from_http = pt_ws_status_from_http(c->http_status);
        if (c->http_status != 0 && from_http != PT_WS_OK) {
            return from_http;
        }
        int64_t elapsed_ms = (esp_timer_get_time() - c->started_us) / 1000;
        return elapsed_ms + 1000 >= c->net_timeout_ms ? PT_WS_TIMEOUT : PT_WS_NO_NETWORK;
    }
    if (type == WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT) {
        return PT_WS_TIMEOUT;
    }
    if (type == WEBSOCKET_ERROR_TYPE_SERVER_CLOSE) {
        return PT_WS_REFUSED;
    }
    return PT_WS_NO_NETWORK;
}

static void on_connected(conn_t *c)
{
    xSemaphoreTakeRecursive(c->lock, portMAX_DELAY);
    if (!c->dead) {
        c->opened = true;
        esp_websocket_client_set_headers(c->client, "");
        ESP_LOGI(TAG, "connection %u open", (unsigned)c->gen);
        if (c->ev.opened != NULL) {
            c->ev.opened(c->ev.ctx);
        }
    }
    xSemaphoreGiveRecursive(c->lock);
}

static bool reserve(conn_t *c, const esp_websocket_event_data_t *d)
{
    size_t need = c->msg_len + (size_t)d->data_len + 1;
    if (need > (size_t)PT_WS_MAX_MESSAGE + 1) {
        return false;
    }
    if (need <= c->msg_cap) {
        return true;
    }
    size_t cap = c->msg_cap == 0 ? (d->payload_len > 0 ? (size_t)d->payload_len + 1 : 4096) : c->msg_cap * 2;
    if (cap < need) {
        cap = need;
    }
    if (cap > (size_t)PT_WS_MAX_MESSAGE + 1) {
        cap = (size_t)PT_WS_MAX_MESSAGE + 1;
    }
    char *grown = big_grow(c->msg, cap);
    if (grown == NULL) {
        return false;
    }
    c->msg = grown;
    c->msg_cap = cap;
    return true;
}

static void on_data(conn_t *c, const esp_websocket_event_data_t *d)
{
    if (d->op_code != WS_OP_TEXT && d->op_code != WS_OP_BINARY && d->op_code != WS_OP_CONT) {
        return;
    }
    xSemaphoreTakeRecursive(c->lock, portMAX_DELAY);
    if (c->dead) {
        xSemaphoreGiveRecursive(c->lock);
        return;
    }
    if (d->op_code != WS_OP_CONT && d->payload_offset == 0) {
        c->msg_len = 0;
    }
    if (d->data_len > 0) {
        if (!reserve(c, d)) {
            ESP_LOGW(TAG, "message too large or out of memory, closing");
            xSemaphoreGiveRecursive(c->lock);
            finish(c, PT_WS_REFUSED);
            if (s_queue != NULL && c->gen != 0) {
                cmd_t cmd = {.kind = CMD_STOP, .gen = c->gen};
                xQueueSend(s_queue, &cmd, 0);
            }
            return;
        }
        memcpy(c->msg + c->msg_len, d->data_ptr, (size_t)d->data_len);
        c->msg_len += (size_t)d->data_len;
    }
    bool frame_done = d->payload_offset + d->data_len >= d->payload_len;
    if (d->fin && frame_done && c->msg_len > 0 && c->msg != NULL) {
        char *json = c->msg;
        size_t n = c->msg_len;
        json[n] = '\0';
        c->msg = NULL;
        c->msg_len = 0;
        c->msg_cap = 0;
        if (c->ev.message != NULL) {
            c->ev.message(c->ev.ctx, json, n);
        } else {
            free(json);
        }
    } else if (d->fin && frame_done) {
        c->msg_len = 0;
    }
    xSemaphoreGiveRecursive(c->lock);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    conn_t *c = arg;
    const esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        on_connected(c);
        break;
    case WEBSOCKET_EVENT_DATA:
        on_data(c, d);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (d->error_handle.esp_ws_handshake_status_code != 0) {
            c->http_status = d->error_handle.esp_ws_handshake_status_code;
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        if (d->error_handle.esp_ws_handshake_status_code != 0) {
            c->http_status = d->error_handle.esp_ws_handshake_status_code;
        }
        finish(c, lost_reason(c, d->error_handle.error_type));
        break;
    case WEBSOCKET_EVENT_CLOSED:
        finish(c, PT_WS_REFUSED);
        break;
    default:
        break;
    }
}

static bool conn_start(conn_t *c)
{
    char url[WS_URL_MAX];
    if (pt_ws_url(c->model, url, sizeof(url)) != PT_OK) {
        return false;
    }
    char *header = malloc(WS_HEADER_MAX);
    if (header == NULL) {
        return false;
    }
    int n = snprintf(header, WS_HEADER_MAX, "Authorization: Bearer %s\r\n", c->key);
    bool ok = n > 0 && n < WS_HEADER_MAX;
    if (ok) {
        const esp_websocket_client_config_t cfg = {
            .uri = url,
            .headers = header,
            .disable_auto_reconnect = true,
            .task_name = "pt_ws_io",
            .task_prio = WS_IO_PRIO,
            .task_stack = WS_IO_STACK,
            .buffer_size = WS_IO_BUFFER,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .network_timeout_ms = c->net_timeout_ms,
            .ping_interval_sec = WS_PING_S,
            .pingpong_timeout_sec = WS_PONG_TIMEOUT_S,
        };
        c->client = esp_websocket_client_init(&cfg);
    }
    wipe(header, WS_HEADER_MAX);
    free(header);
    wipe_free(c->key);
    c->key = NULL;
    if (c->client == NULL) {
        return false;
    }
    c->started_us = esp_timer_get_time();
    if (esp_websocket_register_events(c->client, WEBSOCKET_EVENT_ANY, on_event, c) != ESP_OK ||
        esp_websocket_client_start(c->client) != ESP_OK) {
        return false;
    }
    return true;
}

static bool conn_send(conn_t *c, const char *json)
{
    int sent = esp_websocket_client_send_text(c->client, json, (int)strlen(json), pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    return sent >= 0;
}

static void drop_active(void)
{
    conn_free(s_active);
    s_active = NULL;
}

static void run_open(conn_t *c)
{
    drop_active();
    s_active = c;
    if (c->dead) {
        drop_active();
        return;
    }
    if (!conn_start(c)) {
        ESP_LOGW(TAG, "connection %u could not start", (unsigned)c->gen);
        finish(c, PT_WS_NO_NETWORK);
    }
}

static void run_send(char *json)
{
    atomic_fetch_sub(&s_pending_sends, 1);
    conn_t *c = s_active;
    if (c != NULL && c->opened && !c->dead) {
        if (!conn_send(c, json)) {
            ESP_LOGW(TAG, "send failed on connection %u", (unsigned)c->gen);
            finish(c, PT_WS_NO_NETWORK);
        }
    }
    free(json);
}

static void manager(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t cmd;
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (cmd.kind) {
        case CMD_OPEN:
            run_open(cmd.conn);
            break;
        case CMD_CLOSE:
            if (s_active != NULL && s_active->gen == cmd.gen) {
                drop_active();
            }
            break;
        case CMD_STOP:
            if (s_active != NULL && s_active->gen == cmd.gen && s_active->client != NULL) {
                esp_websocket_client_stop(s_active->client);
            }
            break;
        case CMD_SEND:
            run_send(cmd.json);
            break;
        }
    }
}

pt_err_t pt_ws_start(const pt_ws_events_t *ev)
{
    if (ev == NULL || s_queue != NULL) {
        return PT_ERR_INVALID;
    }
    s_events = *ev;
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(PT_WS_SEND_QUEUE + WS_CONTROL_SLOTS, sizeof(cmd_t));
    if (s_lock == NULL || s_queue == NULL ||
        xTaskCreate(manager, "pt_ws", WS_TASK_STACK, NULL, WS_TASK_PRIO, NULL) != pdPASS) {
        return PT_ERR_NOMEM;
    }
    return PT_OK;
}

void pt_ws_open(const char *api_key, const char *model)
{
    if (s_queue == NULL || api_key == NULL || model == NULL) {
        return;
    }
    conn_t *c = conn_new(&s_events, api_key, model, WS_NET_TIMEOUT_MS);
    if (c == NULL) {
        if (s_events.closed != NULL) {
            s_events.closed(s_events.ctx, PT_WS_NO_NETWORK);
        }
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    c->gen = ++s_next_gen;
    conn_t *old = s_current;
    s_current = c;
    xSemaphoreGive(s_lock);
    conn_kill(old);
    cmd_t cmd = {.kind = CMD_OPEN, .gen = c->gen, .conn = c};
    if (xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_current == c) {
            s_current = NULL;
        }
        xSemaphoreGive(s_lock);
        conn_free(c);
        if (s_events.closed != NULL) {
            s_events.closed(s_events.ctx, PT_WS_NO_NETWORK);
        }
    }
}

bool pt_ws_send(const char *json)
{
    if (s_queue == NULL || json == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    conn_t *c = s_current;
    bool usable = c != NULL && c->opened && !c->dead;
    xSemaphoreGive(s_lock);
    if (!usable) {
        return false;
    }
    if (atomic_fetch_add(&s_pending_sends, 1) >= PT_WS_SEND_QUEUE) {
        atomic_fetch_sub(&s_pending_sends, 1);
        return false;
    }
    char *copy = big_copy(json);
    cmd_t cmd = {.kind = CMD_SEND, .json = copy};
    if (copy == NULL || xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        atomic_fetch_sub(&s_pending_sends, 1);
        free(copy);
        return false;
    }
    return true;
}

void pt_ws_close(void)
{
    if (s_queue == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    conn_t *old = s_current;
    s_current = NULL;
    xSemaphoreGive(s_lock);
    if (old == NULL) {
        return;
    }
    uint32_t gen = old->gen;
    conn_kill(old);
    cmd_t cmd = {.kind = CMD_CLOSE, .gen = gen};
    xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(1000));
}

typedef enum { PROBE_OPENED, PROBE_MESSAGE, PROBE_CLOSED } probe_kind_t;

typedef struct {
    probe_kind_t kind;
    char *json;
    size_t len;
    pt_ws_status_t why;
} probe_item_t;

static void probe_post(QueueHandle_t q, probe_kind_t kind, char *json, size_t len, pt_ws_status_t why)
{
    probe_item_t item = {.kind = kind, .json = json, .len = len, .why = why};
    if (xQueueSend(q, &item, 0) != pdTRUE) {
        free(json);
    }
}

static void probe_opened(void *ctx)
{
    probe_post(ctx, PROBE_OPENED, NULL, 0, PT_WS_OK);
}

static void probe_message(void *ctx, char *json, size_t len)
{
    probe_post(ctx, PROBE_MESSAGE, json, len, PT_WS_OK);
}

static void probe_closed(void *ctx, pt_ws_status_t why)
{
    probe_post(ctx, PROBE_CLOSED, NULL, 0, why);
}

static void copy_text(char *out, size_t len, const char *text)
{
    if (out != NULL && len > 0) {
        snprintf(out, len, "%s", text);
    }
}

static pt_ws_status_t probe_wait(conn_t *c, QueueHandle_t q, const pt_settings_t *s, int64_t deadline_us, char *model,
                                 size_t model_len, char *voice, size_t voice_len, char *error, size_t error_len)
{
    bool update_sent = false;
    for (;;) {
        int64_t left_us = deadline_us - esp_timer_get_time();
        if (left_us <= 0) {
            return PT_WS_TIMEOUT;
        }
        probe_item_t item;
        if (xQueueReceive(q, &item, pdMS_TO_TICKS(left_us / 1000 + 1)) != pdTRUE) {
            return PT_WS_TIMEOUT;
        }
        if (item.kind == PROBE_CLOSED) {
            return item.why;
        }
        if (item.kind != PROBE_MESSAGE) {
            continue;
        }
        pt_rt_event_t e;
        pt_err_t parsed = pt_rt_parse(item.json, item.len, &e);
        free(item.json);
        if (parsed != PT_OK) {
            continue;
        }
        pt_rt_kind_t kind = e.kind;
        bool created = kind == PT_RT_SESSION_READY && strcmp(e.status, "created") == 0;
        bool updated = kind == PT_RT_SESSION_READY && strcmp(e.status, "updated") == 0;
        if (kind == PT_RT_SESSION_READY && e.model[0] != '\0') {
            copy_text(model, model_len, e.model);
        }
        if (kind == PT_RT_SESSION_READY && e.voice[0] != '\0') {
            copy_text(voice, voice_len, e.voice);
        }
        if (kind == PT_RT_ERROR && e.text != NULL) {
            copy_text(error, error_len, e.text);
            ESP_LOGW(TAG, "connection check: OpenAI error %s", e.text);
        }
        pt_rt_event_free(&e);
        if (kind == PT_RT_ERROR) {
            return PT_WS_REJECTED;
        }
        if (created && !update_sent) {
            char *update = pt_rt_session_update(s);
            if (update == NULL || !conn_send(c, update)) {
                free(update);
                return PT_WS_NO_NETWORK;
            }
            free(update);
            update_sent = true;
        } else if (updated) {
            return PT_WS_OK;
        }
    }
}

pt_ws_status_t pt_ws_probe(const pt_settings_t *s, uint32_t timeout_ms, char *model, size_t model_len, char *voice,
                           size_t voice_len, char *error, size_t error_len)
{
    copy_text(error, error_len, "");
    if (s == NULL || s->openai.api_key[0] == '\0') {
        return PT_WS_BAD_KEY;
    }
    if (s->openai.model[0] == '\0') {
        return PT_WS_REJECTED;
    }
    QueueHandle_t q = xQueueCreate(16, sizeof(probe_item_t));
    if (q == NULL) {
        return PT_WS_NO_NETWORK;
    }
    const pt_ws_events_t ev = {probe_opened, probe_message, probe_closed, q};
    conn_t *c = conn_new(&ev, s->openai.api_key, s->openai.model, WS_PROBE_NET_TIMEOUT_MS);
    pt_ws_status_t status = PT_WS_NO_NETWORK;
    if (c != NULL) {
        int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
        if (conn_start(c)) {
            status = probe_wait(c, q, s, deadline_us, model, model_len, voice, voice_len, error, error_len);
        }
        conn_free(c);
    }
    probe_item_t item;
    while (xQueueReceive(q, &item, 0) == pdTRUE) {
        free(item.json);
    }
    vQueueDelete(q);
    return status;
}
