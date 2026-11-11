#include "board_power.h"
#include "board_input.h"
#include "board_sd.h"
#include "display_service.h"
#include "draw.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

esp_err_t board_power_enable_battery(void)
{
    esp_err_t err = gpio_set_level(PIN_BATTERY_LATCH, 1);
    const gpio_config_t battery = {
        .pin_bit_mask = 1ULL << PIN_BATTERY_LATCH,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (!err) err = gpio_config(&battery);
    if (!err) err = gpio_hold_dis(PIN_BATTERY_LATCH);
    if (!err) ESP_LOGI(TAG, "battery latch GPIO17 high");
    return err;
}

void board_power_sleep(void)
{
    /* Mixed-mode present + drain, not another vendor full. The custom driver
     * only moves pixels through the eight-pulse counters and a final HOLD
     * scan; a vendor full here would reset that path. Paint and drain first
     * so the branding is settled on the glass before panel power is cut. */
    uint8_t frame[FB_BYTES];
    draw_sleep(frame);
    if (!display_failed()) {
        (void)display_present(frame);
        (void)display_pump_until_idle(4000);
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    esp_err_t err = esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    if (!err) err = rtc_gpio_pullup_en(GPIO_NUM_0);
    if (!err) err = rtc_gpio_pulldown_dis(GPIO_NUM_0);
    if (!err) err = board_sd_unmount();
    if (err) ESP_LOGW(TAG, "sleep prepare: %s", esp_err_to_name(err));

    if (!display_failed()) {
        (void)display_close();
    }
    gpio_hold_en(PIN_EPD_PWR);
    gpio_hold_en(PIN_BATTERY_LATCH);
    gpio_deep_sleep_hold_en();
    while (gpio_get_level(GPIO_NUM_0) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "deep sleep, wake on Up (GPIO0)");
    esp_deep_sleep_start();
}
