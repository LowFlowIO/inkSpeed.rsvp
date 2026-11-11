#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define WIFI_UPLOAD_SSID "inkSpeed"
#define WIFI_UPLOAD_PASS "inkspeed"
#define WIFI_UPLOAD_URL  "192.168.4.1"

/* SoftAP plus a one-page upload server. Call only from the UI task. */
esp_err_t wifi_upload_start(void);
void wifi_upload_stop(void);
bool wifi_upload_active(void);

/* One saved filename per call, in arrival order. Empty when none pending. */
bool wifi_upload_take_saved(char *name, size_t name_sz);
