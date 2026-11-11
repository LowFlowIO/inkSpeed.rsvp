#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

enum {
    INPUT_NONE = 0,
    INPUT_NEXT,      /* Down tap  (PWR / GPIO18) */
    INPUT_SELECT,    /* Up tap    (BOOT / GPIO0) */
    INPUT_PREVIOUS,  /* Up hold 1.5 s (BOOT / GPIO0) */
    INPUT_BACK,      /* Down hold 1.5 s (PWR / GPIO18) */
    INPUT_POWER,     /* Up+Down hold 2.0 s */
    INPUT_REFRESH,   /* Down double-tap, 400 ms */
};

#define PIN_BOOT GPIO_NUM_0
#define PIN_PWR_BTN GPIO_NUM_18

esp_err_t board_input_start(void);
unsigned board_input_pop(void);
void board_input_clear(void);
void board_input_wait_release(void);
bool board_input_held(void);
