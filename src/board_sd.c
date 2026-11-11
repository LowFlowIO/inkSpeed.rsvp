#include "board_sd.h"
#include "rsvp_bookmark.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "sd";
static sdmmc_card_t *s_card;

esp_err_t board_sd_mount(void)
{
    if (s_card) return ESP_OK;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_39;
    slot.cmd = GPIO_NUM_41;
    slot.d0 = GPIO_NUM_40;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 8,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
    } else {
        ESP_LOGI(TAG, "mounted FAT at %s (1-bit SDMMC 39/41/40)", SD_MOUNT_POINT);
    }
    return err;
}

esp_err_t board_sd_unmount(void)
{
    if (!s_card) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (err == ESP_OK) s_card = NULL;
    return err;
}

bool board_sd_ready(void)
{
    return s_card != NULL;
}

bool board_sd_make_path(const char *rel, char *out, size_t out_sz)
{
    if (!rel || !out || !out_sz || rel[0] == '/' || strstr(rel, "..")) return false;
    int n = snprintf(out, out_sz, "%s/%s", BOOKS_DIR, rel);
    return n > 0 && (size_t)n < out_sz;
}

bool board_sd_book_exists(const char *rel)
{
    char path[BOOK_PATH_MAX];
    struct stat st;
    return board_sd_make_path(rel, path, sizeof path) && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

uint64_t board_sd_file_size(const char *rel)
{
    char path[BOOK_PATH_MAX];
    struct stat st;
    if (!board_sd_make_path(rel, path, sizeof path) || stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

static bool ends_txt(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && strcasecmp(name + n - 4, ".txt") == 0;
}

static int cmp_book(const void *a, const void *b)
{
    const book_entry_t *x = a, *y = b;
    int c = strcasecmp(x->name, y->name);
    return c ? c : strcasecmp(x->rel, y->rel);
}

static void scan_dir(const char *abs, const char *rel, book_entry_t *entries,
                     size_t cap, size_t *count, bool *truncated, int depth)
{
    if (depth > 4) return;
    DIR *dir = opendir(abs);
    if (!dir) return;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char child_abs[BOOK_PATH_MAX];
        char child_rel[BOOK_REL_MAX];
        if (rel[0]) {
            if (snprintf(child_rel, sizeof child_rel, "%s/%s", rel, de->d_name) >= (int)sizeof child_rel)
                continue;
        } else if (snprintf(child_rel, sizeof child_rel, "%s", de->d_name) >= (int)sizeof child_rel) {
            continue;
        }
        if (snprintf(child_abs, sizeof child_abs, "%s/%s", abs, de->d_name) >= (int)sizeof child_abs)
            continue;
        struct stat st;
        if (stat(child_abs, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(child_abs, child_rel, entries, cap, count, truncated, depth + 1);
        } else if (S_ISREG(st.st_mode) && ends_txt(de->d_name)) {
            if (*count >= cap) {
                *truncated = true;
                continue;
            }
            book_entry_t *e = &entries[*count];
            memset(e, 0, sizeof *e);
            strncpy(e->rel, child_rel, sizeof e->rel - 1);
            strncpy(e->name, de->d_name, sizeof e->name - 1);
            (*count)++;
        }
    }
    closedir(dir);
}

esp_err_t board_sd_list_books(book_entry_t *entries, size_t cap, size_t *count, bool *truncated)
{
    if (!entries || !count || !truncated || !cap) return ESP_ERR_INVALID_ARG;
    *count = 0;
    *truncated = false;
    esp_err_t err = board_sd_mount();
    if (err != ESP_OK) return err;
    scan_dir(BOOKS_DIR, "", entries, cap, count, truncated, 0);
    if (*count) qsort(entries, *count, sizeof entries[0], cmp_book);
    return ESP_OK;
}

static bool bookmark_path(const char *rel, char *out, size_t out_sz)
{
    if (!rel || !rel[0]) return false;
    uint32_t crc = rsvp_crc32(rel, strlen(rel));
    return snprintf(out, out_sz, "%s/%08x.bmk", STATE_DIR, (unsigned)crc) < (int)out_sz;
}

static bool save_bookmark_state(const char *rel, uint64_t file_size,
                                uint64_t offset, uint16_t flags)
{
    if (!s_card || !rel) return false;
    (void)mkdir(BOOKS_DIR, 0777);
    if (mkdir(STATE_DIR, 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir state: %d", errno);
        return false;
    }
    char path[BOOK_PATH_MAX], tmp[BOOK_PATH_MAX + 4];
    if (!bookmark_path(rel, path, sizeof path)) return false;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    rsvp_bookmark_t b = {
        .version = RSVP_BMK_VERSION,
        .flags = flags,
        .file_size = file_size,
        .byte_offset = offset,
        .path_crc32 = rsvp_crc32(rel, strlen(rel)),
    };
    uint8_t raw[RSVP_BMK_SIZE];
    if (!rsvp_bookmark_encode(raw, &b)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(raw, 1, sizeof raw, f) == sizeof raw;
    if (fclose(f) != 0) ok = false;
    if (ok) {
        if (rename(tmp, path) != 0) {
            unlink(path);
            ok = rename(tmp, path) == 0;
        }
    }
    if (!ok) unlink(tmp);
    return ok;
}

bool board_sd_save_bookmark(const char *rel, uint64_t file_size, uint64_t offset)
{
    return save_bookmark_state(rel, file_size, offset, 0);
}

bool board_sd_mark_completed(const char *rel, uint64_t file_size)
{
    return save_bookmark_state(rel, file_size, 0, RSVP_BMK_F_COMPLETED);
}

bool board_sd_load_bookmark_state(const char *rel, uint64_t file_size,
                                  uint64_t *offset, bool *completed)
{
    if (offset) *offset = 0;
    if (completed) *completed = false;
    if (!s_card || !rel || !offset) return false;
    char path[BOOK_PATH_MAX];
    if (!bookmark_path(rel, path, sizeof path)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t raw[RSVP_BMK_SIZE];
    bool ok = fread(raw, 1, sizeof raw, f) == sizeof raw;
    fclose(f);
    rsvp_bookmark_t b;
    if (!ok || !rsvp_bookmark_decode(raw, sizeof raw, &b)) return false;
    if (b.path_crc32 != rsvp_crc32(rel, strlen(rel))) return false;
    if (b.file_size != file_size) {
        ESP_LOGI(TAG, "bookmark ignored; file size changed");
        return false;
    }
    bool done = rsvp_bookmark_is_complete(&b);
    *offset = rsvp_bookmark_resume_offset(&b);
    if (completed) *completed = done;
    return true;
}

bool board_sd_load_bookmark(const char *rel, uint64_t file_size, uint64_t *offset)
{
    return board_sd_load_bookmark_state(rel, file_size, offset, NULL);
}

bool board_sd_delete_book(const char *rel)
{
    if (!rel || !rel[0] || strchr(rel, '/') || strstr(rel, "..")) return false;
    char path[BOOK_PATH_MAX];
    if (!board_sd_make_path(rel, path, sizeof path)) return false;
    char bmk[BOOK_PATH_MAX];
    if (bookmark_path(rel, bmk, sizeof bmk)) unlink(bmk);
    return unlink(path) == 0;
}
