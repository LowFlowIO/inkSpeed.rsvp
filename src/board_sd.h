#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_MOUNT_POINT "/sdcard"
#define BOOKS_DIR "/sdcard/books"
/* ".inkpulse" is the pre-rename project name; kept so existing bookmarks still load. */
#define STATE_DIR "/sdcard/books/.inkpulse"
#define BOOK_PATH_MAX 256
#define BOOK_NAME_MAX 96
#define BOOK_REL_MAX 192
#define BOOK_LIST_MAX 128

typedef struct {
    char rel[BOOK_REL_MAX];
    char name[BOOK_NAME_MAX];
} book_entry_t;

esp_err_t board_sd_mount(void);
esp_err_t board_sd_unmount(void);
bool board_sd_ready(void);
bool board_sd_make_path(const char *rel, char *out, size_t out_sz);
bool board_sd_book_exists(const char *rel);
uint64_t board_sd_file_size(const char *rel);
esp_err_t board_sd_list_books(book_entry_t *entries, size_t cap, size_t *count, bool *truncated);
/* Removes the .txt and its bookmark. rel is a books-relative path. */
bool board_sd_delete_book(const char *rel);
bool board_sd_save_bookmark(const char *rel, uint64_t file_size, uint64_t offset);
bool board_sd_mark_completed(const char *rel, uint64_t file_size);
bool board_sd_load_bookmark(const char *rel, uint64_t file_size, uint64_t *offset);
bool board_sd_load_bookmark_state(const char *rel, uint64_t file_size,
                                  uint64_t *offset, bool *completed);
