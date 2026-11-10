#include "pt_ui.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RING_GPIO GPIO_NUM_18
#define BUTTON_BOOT_GPIO GPIO_NUM_0
#define BUTTON_TOP_GPIO GPIO_NUM_8

#define RING_FREQ_HZ 25000
#define RING_RESOLUTION LEDC_TIMER_10_BIT
#define RING_MAX_DUTY ((1u << 10) - 1u)

#define TICK_MS 10
#define RING_EVERY_TICKS 2
#define DEBOUNCE_TICKS 3
#define LONG_PRESS_MS 10000

#define TASK_STACK 3072
#define TASK_PRIORITY 5
#define TASK_CORE 0

typedef struct {
    gpio_num_t gpio;
    int id;
    bool stable_down;
    bool raw_down;
    int raw_ticks;
    uint32_t down_since_ms;
    bool long_fired;
    bool allow_long;
} button_t;

static const char *TAG = "ui";

static button_t buttons[2] = {
    {.gpio = BUTTON_BOOT_GPIO, .id = PT_UI_BUTTON_BOOT, .allow_long = false},
    {.gpio = BUTTON_TOP_GPIO, .id = PT_UI_BUTTON_TOP, .allow_long = true},
};

static void (*button_cb)(int which, bool long_press);
static SemaphoreHandle_t lock;
static pt_ring_t ring;
static float ring_current;
static bool started;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void emit(int id, bool long_press)
{
    if (button_cb)
        button_cb(id, long_press);
}

static void button_tick(button_t *b, uint32_t now)
{
    bool down = gpio_get_level(b->gpio) == 0;
    if (down == b->raw_down) {
        if (b->raw_ticks < DEBOUNCE_TICKS)
            b->raw_ticks++;
    } else {
        b->raw_down = down;
        b->raw_ticks = 1;
    }
    if (b->raw_ticks >= DEBOUNCE_TICKS && b->raw_down != b->stable_down) {
        b->stable_down = b->raw_down;
        if (b->stable_down) {
            b->down_since_ms = now;
            b->long_fired = false;
        } else {
            ESP_LOGI(TAG, "button %d released after %u ms", b->id, (unsigned)(now - b->down_since_ms));
            if (!b->long_fired)
                emit(b->id, false);
        }
    }
    if (b->stable_down && b->allow_long && !b->long_fired && now - b->down_since_ms >= LONG_PRESS_MS) {
        b->long_fired = true;
        emit(b->id, true);
    }
}

static void write_duty(float level)
{
    float duty = pt_ring_duty(level);
    uint32_t raw = (uint32_t)(duty * (float)RING_MAX_DUTY + 0.5f);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, raw);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void ring_tick(uint32_t now)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    ring_current = pt_ring_level(&ring, now);
    float level = ring_current;
    xSemaphoreGive(lock);
    write_duty(level);
}

static void ui_task(void *arg)
{
    (void)arg;
    int ticks = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
        uint32_t now = now_ms();
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
            button_tick(&buttons[i], now);
        if (++ticks >= RING_EVERY_TICKS) {
            ticks = 0;
            ring_tick(now);
        }
    }
}

static pt_err_t init_ring(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = RING_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = RING_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer) != ESP_OK)
        return PT_ERR_INVALID;
    ledc_channel_config_t channel = {
        .gpio_num = RING_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_channel_config(&channel) != ESP_OK)
        return PT_ERR_INVALID;
    gpio_set_drive_capability(RING_GPIO, GPIO_DRIVE_CAP_3);
    return PT_OK;
}

static pt_err_t init_buttons(void)
{
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << buttons[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&cfg) != ESP_OK)
            return PT_ERR_INVALID;
        buttons[i].raw_down = gpio_get_level(buttons[i].gpio) == 0;
        buttons[i].stable_down = buttons[i].raw_down;
        buttons[i].raw_ticks = DEBOUNCE_TICKS;
        buttons[i].long_fired = buttons[i].stable_down;
    }
    return PT_OK;
}

pt_err_t pt_ui_start(void (*on_button)(int which, bool long_press))
{
    if (started)
        return PT_OK;
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return PT_ERR_NOMEM;
    button_cb = on_button;
    pt_err_t err = init_ring();
    if (err != PT_OK)
        return err;
    err = init_buttons();
    if (err != PT_OK)
        return err;
    pt_ring_set(&ring, PT_RING_OFF, now_ms(), 0.0f);
    ring_current = 0.0f;
    if (xTaskCreatePinnedToCore(ui_task, "pt_ui", TASK_STACK, NULL, TASK_PRIORITY, NULL, TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "ui task not created");
        return PT_ERR_NOMEM;
    }
    started = true;
    return PT_OK;
}

void pt_ui_ring(pt_ring_state_t s)
{
    if (!started)
        return;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool one_shot = s == PT_RING_ERROR || s == PT_RING_RESET;
    if (s != ring.state || one_shot)
        pt_ring_set(&ring, s, now_ms(), ring_current);
    xSemaphoreGive(lock);
}
