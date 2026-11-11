#pragma once

#include "esp_err.h"

#define PIN_BATTERY_LATCH GPIO_NUM_17
#define PIN_EPD_PWR GPIO_NUM_6

esp_err_t board_power_enable_battery(void);
void board_power_sleep(void);
