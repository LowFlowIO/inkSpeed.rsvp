#include "ui.h"
#include "board_input.h"
#include "board_power.h"
#include "board_sd.h"
#include "display_service.h"
#include "draw.h"
#include "reader.h"
#include "settings_store.h"
#include "wifi_upload.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

static esp_err_t ensure_sd(void);

void ui_show_message(const char *title, const char *line1,
                     const char *line2, const char *footer)
{
    uint8_t frame[FB_BYTES];
    draw_message(frame, title, line1, line2, footer);
    (void)display_present(frame);
    (void)display_pump_until_idle(1500);
}

void ui_clear_ghosting(void)
{
    uint8_t frame[FB_BYTES];
    draw_message(frame, "Refreshing", "Clearing the panel", NULL, NULL);
    (void)display_show_full(frame);
    fb_clear(frame, true);
    (void)display_show_full(frame);
    board_input_clear();
}

void ui_present_full(const uint8_t *frame)
{
    if (!frame) return;
    uint8_t blank[FB_BYTES];
    fb_clear(blank, true);
    (void)display_show_full(blank);
    (void)display_show_full(frame);
    board_input_clear();
}

/* One vendor full of a blank panel. Faster than the double clear, and it
 * gives the book a clean glass to start on. */
static void ui_refresh_quick(void)
{
    uint8_t frame[FB_BYTES];
    fb_clear(frame, true);
    (void)display_show_full(frame);
    board_input_clear();
}

static void paint_menu(const char *title, const char *subtitle,
                       const char *const *items, int count, int selected,
                       const char *hint1, const char *hint2)
{
    uint8_t frame[FB_BYTES];
    draw_menu(frame, title, subtitle, items, (unsigned)count,
              selected < 0 ? 0 : (unsigned)selected, hint1, hint2);
    (void)display_present(frame);
    (void)display_pump_until_idle(800);
}

static void show_boot_splash(void)
{
    uint8_t frame[FB_BYTES];
    draw_splash(frame);
    (void)display_show_full(frame);
    (void)display_pump_until_idle(1500);
    const int64_t visible_from = esp_timer_get_time();
    (void)ensure_sd();
    while (esp_timer_get_time() - visible_from < 1500000) {
        if (display_service_once() != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int ui_choose(const char *title, const char *subtitle,
              const char *const *items, int count, int *selected)
{
    if (count <= 0) return -1;
    int sel = selected ? *selected : 0;
    if (sel < 0 || sel >= count) sel = 0;
    for (;;) {
        paint_menu(title, subtitle, items, count, sel,
                   DRAW_HINT_MENU_1, DRAW_HINT_MENU_2);
        for (;;) {
            unsigned ev = board_input_pop();
            if (ev == INPUT_NEXT) {
                sel = (sel + 1) % count;
                if (selected) *selected = sel;
                break;
            }
            if (ev == INPUT_PREVIOUS) {
                sel = (sel + count - 1) % count;
                if (selected) *selected = sel;
                break;
            }
            if (ev == INPUT_SELECT) {
                if (selected) *selected = sel;
                return sel;
            }
            if (ev == INPUT_BACK) return -1;
            if (ev == INPUT_POWER) return -2;
            if (ev == INPUT_REFRESH) {
                ui_clear_ghosting();
                break;
            }
            if (display_service_once() != ESP_OK) return -1;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (selected) *selected = sel;
    }
}

static void cycle_setting(rsvp_settings_t *s, int which)
{
    switch (which) {
    case 0:
        s->wpm += RSVP_WPM_STEP;
        if (s->wpm > RSVP_WPM_MAX) s->wpm = RSVP_WPM_MIN;
        break;
    case 1:
        s->font = (s->font + 1) % FONT_COUNT;
        break;
    case 2:
        s->punct_mode = (s->punct_mode + 1) % 3;
        break;
    case 3:
        s->countdown = (s->countdown + 1) % (RSVP_COUNTDOWN_MAX + 1);
        break;
    case 4:
        s->align = s->align == RSVP_ALIGN_ORP ? RSVP_ALIGN_CENTER : RSVP_ALIGN_ORP;
        break;
    case 5:
        s->show_hints = s->show_hints ? 0 : 1;
        break;
    default:
        break;
    }
    rsvp_settings_clamp(s);
}

static int ui_guide(void)
{
    static const char *lines[] = {
        "Buttons",
        "Up: Select",
        "Down: Next",
        "Hold Up: Previous",
        "Hold Down: Back",
        "Up + Down: Power off",
        "Down x2: Refresh",
        "",
        "Reading",
        "Up: Play or Pause",
        "Down: Rewind",
        "Hold Up: Settings",
        "Hold Down: Library",
        "",
        "A stopped book asks",
        "Continue or Start over",
        "before it opens.",
        "",
        "Books are UTF-8 .txt",
        "in /books on the card.",
        "Upload joins inkSpeed.",
        "Password: inkspeed",
        "",
        "ORP marks the letter",
        "your eye rests on.",
        "Center puts the word",
        "in the middle.",
        "",
        "Hints Off hides this",
        "band while reading,",
        "so the word sits in",
        "the middle.",
    };
    const int count = (int)(sizeof lines / sizeof lines[0]);
    const int visible = 6;
    int first = 0;
    for (;;) {
        uint8_t frame[FB_BYTES];
        draw_text_page(frame, "Guide", lines, count, first,
                       "Up: Prev   Down: More", "Hold Down: Back");
        (void)display_present(frame);
        (void)display_pump_until_idle(800);
        for (;;) {
            unsigned ev = board_input_pop();
            if (ev == INPUT_NEXT && first + visible < count) { first++; break; }
            if (ev == INPUT_PREVIOUS && first > 0) { first--; break; }
            if (ev == INPUT_SELECT && first > 0) { first--; break; }
            if (ev == INPUT_BACK) return 0;
            if (ev == INPUT_POWER) return -2;
            if (display_service_once() != ESP_OK) return -1;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

int ui_edit_settings(rsvp_settings_t *settings)
{
    int selected = 0;
    const int n = 7;
    for (;;) {
        char lines[7][40];
        snprintf(lines[0], sizeof lines[0], "Speed  %u WPM", settings->wpm);
        snprintf(lines[1], sizeof lines[1], "Size   %s", font_id_name((font_id_t)settings->font));
        snprintf(lines[2], sizeof lines[2], "Pauses %s", rsvp_punct_name(settings->punct_mode));
        snprintf(lines[3], sizeof lines[3], "Count  %u s", settings->countdown);
        snprintf(lines[4], sizeof lines[4], "Align  %s", rsvp_align_name(settings->align));
        snprintf(lines[5], sizeof lines[5], "Hints  %s", settings->show_hints ? "On" : "Off");
        snprintf(lines[6], sizeof lines[6], "Guide");
        const char *items[7] = {
            lines[0], lines[1], lines[2], lines[3], lines[4], lines[5], lines[6]
        };
        paint_menu("Settings", "Down x2: Refresh", items, n, selected,
                   DRAW_HINT_SETTINGS_1, DRAW_HINT_SETTINGS_2);
        for (;;) {
            unsigned ev = board_input_pop();
            if (ev == INPUT_NEXT) { selected = (selected + 1) % n; break; }
            if (ev == INPUT_PREVIOUS) { selected = (selected + n - 1) % n; break; }
            if (ev == INPUT_SELECT) {
                if (selected == 6) {
                    if (ui_guide() == -2) return -2;
                } else {
                    cycle_setting(settings, selected);
                }
                break;
            }
            if (ev == INPUT_REFRESH) {
                ui_clear_ghosting();
                break;
            }
            if (ev == INPUT_BACK) return 0;
            if (ev == INPUT_POWER) return -2;
            if (display_service_once() != ESP_OK) return -1;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static esp_err_t ensure_sd(void)
{
    if (board_sd_ready()) return ESP_OK;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = board_sd_mount();
        if (err == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return err;
}

static bool book_can_continue(const char *rel)
{
    if (!rel || !rel[0] || !board_sd_book_exists(rel)) return false;
    uint64_t sz = board_sd_file_size(rel);
    uint64_t off = 0;
    bool complete = false;
    if (!board_sd_load_bookmark_state(rel, sz, &off, &complete)) return true;
    return !complete;
}

static void handle_reader_result(reader_result_t rr, rsvp_settings_t *settings,
                                 const char *name)
{
    if (rr == READER_HOME) return;
    if (rr == READER_POWER) {
        settings_store_save(settings);
        board_power_sleep();
    } else if (rr == READER_FAIL) {
        ui_show_message("Open error", "Reading ended unexpectedly",
                        name ? name : "", "Hold Down: Back");
    }
}

static void ui_books(rsvp_settings_t *settings)
{
    /* 128 * (192+96) = 36 KiB — far too large for the main task stack. */
    static book_entry_t books[BOOK_LIST_MAX];
    size_t count = 0;
    bool truncated = false;
    if (ensure_sd() != ESP_OK) {
        ui_show_message("No SD card", "Format FAT32", "Create /books", "Hold Down: Back");
        return;
    }
    if (board_sd_list_books(books, BOOK_LIST_MAX, &count, &truncated) != ESP_OK) {
        ui_show_message("SD error", "Cannot list /books", "", "Hold Down: Back");
        return;
    }
    if (count == 0) {
        ui_show_message("No books", "Copy .txt files", "into /books", "Hold Down: Back");
        return;
    }
    const char *names[BOOK_LIST_MAX];
    for (size_t i = 0; i < count; ++i) names[i] = books[i].name;
    int selected = 0;
    int choice = ui_choose("Books", truncated ? "First 128" : NULL,
                           names, (int)count, &selected);
    if (choice == -2) {
        settings_store_save(settings);
        board_power_sleep();
    }
    if (choice < 0) return;
    if ((unsigned)choice >= count) {
        ui_show_message("Open error", "Invalid selection", "", "Hold Down: Back");
        return;
    }
    bool resume = true;
    if (book_can_continue(books[choice].rel)) {
        uint64_t sz = board_sd_file_size(books[choice].rel);
        uint64_t off = 0;
        bool complete = false;
        unsigned pct = 0;
        if (sz && board_sd_load_bookmark_state(books[choice].rel, sz, &off, &complete)) {
            pct = (unsigned)((off * 100ull) / sz);
            if (pct > 100) pct = 100;
        }
        char stopped[32];
        snprintf(stopped, sizeof stopped, "Stopped at %u%%", pct);
        const char *ask[] = { "Continue", "Start over" };
        int pick = 0;
        int ans = ui_choose(books[choice].name, stopped, ask, 2, &pick);
        if (ans == -2) {
            settings_store_save(settings);
            board_power_sleep();
        }
        if (ans < 0) return;
        resume = ans == 0;
    }
    ui_refresh_quick();
    reader_result_t rr = reader_open(books[choice].rel, settings, resume);
    /* Finished already did a vendor-full cycle; Home is a quiet update. */
    if (rr == READER_BACK) ui_clear_ghosting();
    if (rr == READER_HOME) return;
    handle_reader_result(rr, settings, books[choice].name);
}

static int ui_wifi_upload(void)
{
    if (ensure_sd() != ESP_OK) {
        ui_show_message("No SD card", "Format FAT32", "Create /books", "Hold Down: Back");
        return 0;
    }
    esp_err_t err = wifi_upload_start();
    if (err != ESP_OK) {
        ui_show_message("WiFi", "Could not start", "the hotspot", "Hold Down: Back");
        return 0;
    }
    char line1[48];
    char line2[96];
    snprintf(line1, sizeof line1, "%s / %s", WIFI_UPLOAD_SSID, WIFI_UPLOAD_PASS);
    snprintf(line2, sizeof line2, "Open %s", WIFI_UPLOAD_URL);
    bool dirty = true;
    int rc = 0;
    for (;;) {
        char saved[80];
        if (wifi_upload_take_saved(saved, sizeof saved)) {
            snprintf(line2, sizeof line2, "Saved %.70s", saved);
            dirty = true;
        }
        if (dirty) {
            uint8_t frame[FB_BYTES];
            draw_message(frame, "Upload", line1, line2, "Hold Down: Stop");
            (void)display_present(frame);
            (void)display_pump_until_idle(1500);
            dirty = false;
        }
        unsigned ev = board_input_pop();
        if (ev == INPUT_POWER) { rc = -2; break; }
        if (ev == INPUT_BACK) break;
        if (display_service_once() != ESP_OK) { rc = -1; break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    wifi_upload_stop();
    return rc;
}

void ui_run(void)
{
    rsvp_settings_t settings;
    rsvp_settings_defaults(&settings);
    settings_store_load(&settings);

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    /* One vendor full refresh: panel state is unknown on every boot, including
     * wake from deep sleep. The splash is that first settled image; SD mounts
     * while it stays on screen. No button acknowledgement. */
    show_boot_splash();
    board_input_wait_release();

    int selected = 0;
    while (!display_failed()) {
        const char *items[4];
        int map[4];
        int n = 0;
        items[n] = "Books";
        map[n++] = 1;
        items[n] = "Settings";
        map[n++] = 2;
        items[n] = "Upload";
        map[n++] = 4;
        items[n] = "Power off";
        map[n++] = 3;
        int choice = ui_choose("Home", NULL, items, n, &selected);
        if (choice == -2) {
            settings_store_save(&settings);
            board_power_sleep();
        }
        if (choice < 0) continue;
        switch (map[choice]) {
        case 1:
            ui_books(&settings);
            break;
        case 2:
            if (ui_edit_settings(&settings) == -2) {
                settings_store_save(&settings);
                board_power_sleep();
            } else {
                settings_store_save(&settings);
            }
            break;
        case 3:
            settings_store_save(&settings);
            board_power_sleep();
            break;
        case 4:
            if (ui_wifi_upload() == -2) {
                settings_store_save(&settings);
                board_power_sleep();
            }
            break;
        default:
            break;
        }
        selected = 0;
    }
    ESP_LOGE(TAG, "UI stopped; panel off. Reset required.");
}
