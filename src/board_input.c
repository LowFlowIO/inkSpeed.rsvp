#include "board_input.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

#define INPUT_DEBOUNCE_US  30000
#define INPUT_HOLD_US    1500000  /* long enough that waiting on e-paper is still a tap */
#define INPUT_CHORD_US   2000000
#define INPUT_DOUBLE_US   400000  /* Down double-tap window */

typedef struct {
    bool raw, stable, handled;
    int64_t changed, pressed;
} button_state_t;

static button_state_t buttons[2];
static bool chord, chord_reported;
static int64_t chord_started;
static bool down_tap_pending, down_double_armed;
static int64_t down_released_at;
static unsigned events[32];
static atomic_uint ev_r, ev_w;

static void enqueue(unsigned event)
{
    unsigned w = atomic_load(&ev_w), r = atomic_load(&ev_r);
    if (w - r < 32) {
        events[w % 32] = event;
        atomic_store(&ev_w, w + 1);
    }
}

unsigned board_input_pop(void)
{
    unsigned r = atomic_load(&ev_r);
    if (r == atomic_load(&ev_w)) return INPUT_NONE;
    unsigned ev = events[r % 32];
    atomic_store(&ev_r, r + 1);
    return ev;
}

void board_input_clear(void)
{
    while (board_input_pop()) {}
    down_tap_pending = false;
    down_double_armed = false;
}

static void sample(void)
{
    int64_t now = esp_timer_get_time();
    if (down_tap_pending && !down_double_armed &&
        now - down_released_at >= INPUT_DOUBLE_US) {
        enqueue(INPUT_NEXT);
        down_tap_pending = false;
    }
    bool down[2] = {
        gpio_get_level(PIN_BOOT) == 0,
        gpio_get_level(PIN_PWR_BTN) == 0,
    };
    if (down[0] && down[1]) {
        if (!chord) {
            chord = true;
            chord_reported = false;
        }
        if (!chord_started) chord_started = now;
        if (!chord_reported && now - chord_started >= INPUT_CHORD_US) {
            enqueue(INPUT_POWER);
            chord_reported = true;
        }
    } else {
        chord_started = 0;
    }

    for (unsigned i = 0; i < 2; ++i) {
        button_state_t *b = &buttons[i];
        bool raw = down[i];
        if (raw != b->raw) {
            b->raw = raw;
            b->changed = now;
        }
        if (now - b->changed >= INPUT_DEBOUNCE_US && b->stable != raw) {
            b->stable = raw;
            if (raw) {
                b->pressed = now;
                b->handled = chord;
            } else if (b->pressed && now - b->pressed < INPUT_DOUBLE_US &&
                       i == 1 && down_tap_pending &&
                       now - down_released_at < INPUT_DOUBLE_US) {
                down_double_armed = true;
                down_tap_pending = false;
            } else if (!b->handled) {
                if (i == 1 && down_double_armed) {
                    down_double_armed = false;
                    enqueue(INPUT_REFRESH);
                } else if (i == 1) {
                    down_tap_pending = true;
                    down_released_at = now;
                } else {
                    enqueue(INPUT_SELECT);
                }
                b->handled = true;
            }
        }
        if (chord) b->handled = true;
        if (!b->stable || b->handled) continue;
        if (now - b->pressed >= INPUT_HOLD_US) {
            enqueue(i ? INPUT_BACK : INPUT_PREVIOUS);
            b->handled = true;
        }
    }
    if (!down[0] && !down[1] && !buttons[0].stable && !buttons[1].stable) chord = false;
}

static void watch_task(void *unused)
{
    (void)unused;
    for (;;) {
        sample();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t board_input_start(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BOOT) | (1ULL << PIN_PWR_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err) return err;
    for (unsigned i = 0; i < 2; ++i) {
        bool down = gpio_get_level(i ? PIN_PWR_BTN : PIN_BOOT) == 0;
        buttons[i] = (button_state_t){ .raw = down, .stable = down, .handled = true };
    }
    return xTaskCreate(watch_task, "buttons", 2048, NULL, 2, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

bool board_input_held(void)
{
    return gpio_get_level(PIN_BOOT) == 0 || gpio_get_level(PIN_PWR_BTN) == 0;
}

void board_input_wait_release(void)
{
    while (board_input_held()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(40));
    board_input_clear();
}
