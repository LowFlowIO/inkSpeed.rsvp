#include "display_service.h"
#include "drive_counters.h"
#include "ssd1681_fast.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define DITHER_PULSES 8u
#define ANALOG_IDLE_US 750000
#define CLEAN_SCAN_HINT 160u

static const char *TAG = "display";

static ssd1681_handle_t s_dev;
static drive_counters_t s_counters;
static DMA_ATTR uint8_t s_black[SSD1681_FRAME_BYTES];
static DMA_ATTR uint8_t s_white[SSD1681_FRAME_BYTES];
static DMA_ATTR uint8_t s_target[SSD1681_FRAME_BYTES];
static bool s_failed;
static bool s_mixed;
static bool s_hold_needed;
static bool s_need_resume;
static bool s_analog_on;
static unsigned s_scans_since_full;
static int64_t s_last_drive_us;

bool display_failed(void) { return s_failed; }
bool display_ready(void) { return s_dev && s_mixed && !s_failed; }
unsigned display_pending_pixels(void) { return s_counters.pending_pixels; }
unsigned display_scans_since_full(void) { return s_scans_since_full; }

static void fail_now(const char *where, esp_err_t err)
{
    ESP_LOGE(TAG, "%s: %s", where, esp_err_to_name(err));
    s_failed = true;
    s_mixed = false;
    if (s_dev) {
        (void)ssd1681_destroy(s_dev);
        s_dev = NULL;
    }
}

esp_err_t display_init(void)
{
    memset(s_target, 0xFF, sizeof s_target);
    esp_err_t err = ssd1681_create(&s_dev);
    if (err == ESP_OK) err = ssd1681_enable_continuous_play(s_dev);
    if (err == ESP_OK) err = ssd1681_power_on(s_dev);
    if (err != ESP_OK) {
        fail_now("init", err);
        return err;
    }
    ESP_LOGI(TAG, "ssd1681 ready (continuous play, custom waveforms)");
    return ESP_OK;
}

esp_err_t display_close(void)
{
    s_mixed = false;
    s_hold_needed = false;
    if (!s_dev) return ESP_OK;
    esp_err_t err = ssd1681_destroy(s_dev);
    s_dev = NULL;
    if (err) s_failed = true;
    return err;
}

static esp_err_t enter_mixed(const uint8_t *frame)
{
    ssd1681_timing_t timing;
    esp_err_t err = ssd1681_vendor_full(s_dev, frame, SSD1681_FRAME_BYTES, &timing);
    if (err == ESP_OK) err = ssd1681_vendor_enable_mixed(s_dev);
    if (err == ESP_OK) err = ssd1681_vendor_set_mixed_frames(s_dev, 1);
    if (err == ESP_OK) err = ssd1681_vendor_set_fast_transport(s_dev, true);
    if (err != ESP_OK) return err;
    drive_counters_seed(&s_counters, frame);
    memcpy(s_target, frame, sizeof s_target);
    s_mixed = true;
    s_hold_needed = false;
    s_need_resume = false;
    s_analog_on = false;
    s_scans_since_full = 0;
    s_last_drive_us = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t display_show_full(const uint8_t *frame)
{
    if (s_failed || !s_dev || !frame) return ESP_ERR_INVALID_STATE;
    memcpy(s_target, frame, sizeof s_target);
    esp_err_t err = enter_mixed(frame);
    if (err) fail_now("full", err);
    return err;
}

void display_set_target(const uint8_t *frame)
{
    if (!frame) return;
    memcpy(s_target, frame, sizeof s_target);
}

esp_err_t display_present(const uint8_t *frame)
{
    if (!frame) return ESP_ERR_INVALID_ARG;
    if (s_failed || !s_dev) return ESP_ERR_INVALID_STATE;
    if (!s_mixed) return display_show_full(frame);
    display_set_target(frame);
    return ESP_OK;
}

static esp_err_t ensure_analog(void)
{
    if (s_need_resume) {
        ssd1681_timing_t timing;
        esp_err_t err = ssd1681_vendor_mixed_resume(s_dev, &timing);
        if (err != ESP_OK) return err;
        s_need_resume = false;
        s_analog_on = true;
    }
    return ESP_OK;
}

esp_err_t display_service_once(void)
{
    if (s_failed || !s_dev || !s_mixed) return s_failed ? ESP_FAIL : ESP_OK;

    unsigned driven = drive_counters_prepare(&s_counters, s_target, DITHER_PULSES,
                                             DITHER_PULSES, s_black, s_white);
    if (driven == 0 && !s_hold_needed) {
        if (s_analog_on && esp_timer_get_time() - s_last_drive_us >= ANALOG_IDLE_US) {
            ssd1681_timing_t timing;
            esp_err_t err = ssd1681_analog_power(s_dev, false, &timing);
            s_analog_on = false;
            s_need_resume = true;
            if (err) {
                fail_now("analog off", err);
                return err;
            }
        }
        return ESP_OK;
    }

    esp_err_t err = ensure_analog();
    if (err) {
        fail_now("resume", err);
        return err;
    }

    ssd1681_timing_t timing;
    if (driven == 0 && s_hold_needed) {
        memset(s_black, 0, sizeof s_black);
        memset(s_white, 0, sizeof s_white);
        err = ssd1681_vendor_mixed_scan(s_dev, s_black, s_white, SSD1681_FRAME_BYTES, &timing);
        if (err) {
            fail_now("hold scan", err);
            return err;
        }
        s_hold_needed = false;
    } else {
        err = ssd1681_vendor_mixed_scan(s_dev, s_black, s_white, SSD1681_FRAME_BYTES, &timing);
        if (err) {
            fail_now("mixed scan", err);
            return err;
        }
        drive_counters_complete(&s_counters, s_black, s_white);
        s_hold_needed = true;
    }
    s_analog_on = true;
    s_scans_since_full++;
    s_last_drive_us = esp_timer_get_time();
    (void)CLEAN_SCAN_HINT;
    return ESP_OK;
}

esp_err_t display_pump_until_idle(int timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    /* Always service once. display_present only copies s_target; the counters
     * stay at the previous settled image (pending==0, no HOLD) until
     * drive_counters_prepare runs inside display_service_once. Checking the
     * counters first made a present+pump of a new frame a no-op, so sleep
     * cut panel power while the menu was still on the glass. */
    do {
        if (s_failed || !s_mixed) break;
        esp_err_t err = display_service_once();
        if (err) return err;
        if (!(s_counters.pending_pixels || s_hold_needed)) break;
    } while (timeout_ms <= 0 || esp_timer_get_time() < deadline);
    return s_failed ? ESP_FAIL : ESP_OK;
}
