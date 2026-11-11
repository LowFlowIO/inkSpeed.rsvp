#include "settings_store.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "settings";
static nvs_handle_t s_nvs;
static bool s_open;

esp_err_t settings_store_open(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init: %s (keeping defaults, not erasing)", esp_err_to_name(err));
        return err;
    }
    /* "inkpulse" is the pre-rename project name; kept so saved settings survive updates. */
    err = nvs_open("inkpulse", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    return ESP_OK;
}

void settings_store_load(rsvp_settings_t *s)
{
    rsvp_settings_defaults(s);
    if (!s_open) return;
    uint16_t wpm = 0;
    uint8_t u8 = 0;
    if (nvs_get_u16(s_nvs, "wpm", &wpm) == ESP_OK) s->wpm = wpm;
    if (nvs_get_u8(s_nvs, "font", &u8) == ESP_OK) s->font = u8;
    if (nvs_get_u8(s_nvs, "punct", &u8) == ESP_OK) s->punct_mode = u8;
    if (nvs_get_u8(s_nvs, "count", &u8) == ESP_OK) s->countdown = u8;
    if (nvs_get_u8(s_nvs, "align", &u8) == ESP_OK) s->align = u8;
    if (nvs_get_u8(s_nvs, "hints", &u8) == ESP_OK) s->show_hints = u8;
    size_t n = sizeof s->last_rel;
    if (nvs_get_str(s_nvs, "last", s->last_rel, &n) != ESP_OK) s->last_rel[0] = 0;
    rsvp_settings_clamp(s);
}

void settings_store_save(const rsvp_settings_t *s)
{
    if (!s_open || !s) return;
    rsvp_settings_t tmp = *s;
    rsvp_settings_clamp(&tmp);
    (void)nvs_set_u16(s_nvs, "wpm", (uint16_t)tmp.wpm);
    (void)nvs_set_u8(s_nvs, "font", (uint8_t)tmp.font);
    (void)nvs_set_u8(s_nvs, "punct", (uint8_t)tmp.punct_mode);
    (void)nvs_set_u8(s_nvs, "count", (uint8_t)tmp.countdown);
    (void)nvs_set_u8(s_nvs, "align", (uint8_t)tmp.align);
    (void)nvs_set_u8(s_nvs, "hints", (uint8_t)tmp.show_hints);
    (void)nvs_set_str(s_nvs, "last", tmp.last_rel);
    (void)nvs_commit(s_nvs);
}

