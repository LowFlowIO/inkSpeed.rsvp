#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t display_init(void);
esp_err_t display_close(void);
bool display_failed(void);

/* Vendor full refresh, then re-enter mixed 1-frame mode and seed counters. */
esp_err_t display_show_full(const uint8_t *frame);

/* Change the drive target without waiting. The next service pass retargets
 * pixels; words are never dropped here. */
void display_set_target(const uint8_t *frame);

/* First frame after init (or after a fault) uses a vendor full refresh to
 * establish unknown panel state. Once mixed mode is running, this only
 * updates the drive target — it does not start another full clean. */
esp_err_t display_present(const uint8_t *frame);

/* Perform at most one mixed scan, HOLD ending, or analog-off. */
esp_err_t display_service_once(void);

/* Pump scans until the image is settled or the timeout expires. */
esp_err_t display_pump_until_idle(int timeout_ms);

unsigned display_pending_pixels(void);
unsigned display_scans_since_full(void);
bool display_ready(void);
