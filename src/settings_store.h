#pragma once

#include "esp_err.h"
#include "rsvp_settings.h"
#include <stdbool.h>

esp_err_t settings_store_open(void);
void settings_store_load(rsvp_settings_t *s);
void settings_store_save(const rsvp_settings_t *s);
