#include "board_input.h"
#include "board_power.h"
#include "display_service.h"
#include "ui.h"
#include "settings_store.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "inkspeed";

static void run_reader(void)
{
    esp_err_t err = board_power_enable_battery();
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(PIN_EPD_PWR);
    rtc_gpio_deinit(GPIO_NUM_0);
    const gpio_config_t boot = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (!err) err = gpio_config(&boot);
    while (!err && gpio_get_level(GPIO_NUM_0) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    if (err == ESP_OK) err = board_input_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "startup: %s", esp_err_to_name(err));
        return;
    }
    (void)settings_store_open();
    ui_run();
}

void app_main(void)
{
    ESP_LOGI(TAG, "inkSpeed v%s", esp_app_get_description()->version);
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
        reason == ESP_RST_BROWNOUT) {
        ESP_LOGE(TAG, "Reset reason %d: refusing automatic start after a crash; power-cycle to retry",
                 (int)reason);
        /* The e-paper keeps its last image, which looks like a frozen menu.
         * Paint one status frame so a crash is visible, then stop. */
        if (board_power_enable_battery() == ESP_OK && display_init() == ESP_OK) {
            ui_show_message("Stopped", "Crashed last run", "Power-cycle to retry", "");
            (void)display_close();
        }
        return;
    }
    run_reader();
}
