#include "reader.h"
#include "board_input.h"
#include "board_sd.h"
#include "display_service.h"
#include "draw.h"
#include "rsvp_fit.h"
#include "rsvp_sched.h"
#include "rsvp_session.h"
#include "rsvp_timing.h"
#include "rsvp_tokenizer.h"
#include "settings_store.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "reader";

#define LOOKAHEAD 8
#define HIST_MAX 256

typedef struct {
    uint64_t offset;
    uint16_t flags;
} hist_item_t;

typedef struct {
    rsvp_settings_t *settings;
    char rel[BOOK_REL_MAX];
    char path[BOOK_PATH_MAX];
    FILE *file;
    uint64_t file_size;
    rsvp_stream_t stream;
    bool stream_eof;
    rsvp_token_t queue[LOOKAHEAD];
    int qh, qc;
    rsvp_token_t current;
    bool have_current;
    hist_item_t hist[HIST_MAX];
    int hist_n;
    uint8_t now_frame[FB_BYTES];
    draw_rect_t word_dirty;
    bool have_word_dirty;
    unsigned word_ms;
    rsvp_sched_t sched;
    unsigned scans_this_word;
    unsigned words_since_save;
    bool playing;
    bool finished;
    bool opened_complete;
    bool progress_live;
    bool session_resumed;
    rsvp_session_t session;
} reader_t;

static bool q_push(reader_t *r, const rsvp_token_t *t)
{
    if (r->qc >= LOOKAHEAD) return false;
    r->queue[(r->qh + r->qc) % LOOKAHEAD] = *t;
    r->qc++;
    return true;
}

static bool q_push_front(reader_t *r, const rsvp_token_t *t)
{
    if (r->qc >= LOOKAHEAD) return false;
    r->qh = (r->qh + LOOKAHEAD - 1) % LOOKAHEAD;
    r->queue[r->qh] = *t;
    r->qc++;
    return true;
}

static bool q_pop(reader_t *r, rsvp_token_t *t)
{
    if (!r->qc) return false;
    *t = r->queue[r->qh];
    r->qh = (r->qh + 1) % LOOKAHEAD;
    r->qc--;
    return true;
}

static void hist_push(reader_t *r, uint64_t offset, uint16_t flags)
{
    if (r->hist_n == HIST_MAX) {
        memmove(r->hist, r->hist + 1, sizeof r->hist[0] * (HIST_MAX - 1));
        r->hist_n--;
    }
    r->hist[r->hist_n].offset = offset;
    r->hist[r->hist_n].flags = flags;
    r->hist_n++;
}

static void hist_truncate_before(reader_t *r, uint64_t offset)
{
    int n = 0;
    for (int i = 0; i < r->hist_n; ++i) {
        if (r->hist[i].offset < offset) r->hist[n++] = r->hist[i];
    }
    r->hist_n = n;
}

static void fill_queue(reader_t *r)
{
    while (r->qc < LOOKAHEAD && !r->stream_eof) {
        rsvp_token_t tok;
        if (!rsvp_next_token(&r->stream, &tok)) {
            r->stream_eof = true;
            break;
        }
        q_push(r, &tok);
    }
}

static void remember_last(reader_t *r)
{
    if (!r || !r->settings || !r->rel[0]) return;
    if (strcmp(r->settings->last_rel, r->rel) == 0) return;
    strncpy(r->settings->last_rel, r->rel, sizeof r->settings->last_rel - 1);
    r->settings->last_rel[sizeof r->settings->last_rel - 1] = 0;
    settings_store_save(r->settings);
}

static void clear_last_if_this(reader_t *r)
{
    if (!r || !r->settings) return;
    if (strcmp(r->settings->last_rel, r->rel) != 0) return;
    r->settings->last_rel[0] = 0;
    settings_store_save(r->settings);
}

static void save_progress(reader_t *r)
{
    if (r->finished) {
        (void)board_sd_mark_completed(r->rel, r->file_size);
        r->words_since_save = 0;
        return;
    }
    if (r->opened_complete && !r->progress_live) return;
    uint64_t off = r->have_current ? r->current.byte_offset : 0;
    (void)board_sd_save_bookmark(r->rel, r->file_size, off);
    r->words_since_save = 0;
}

static bool is_ws_byte(unsigned char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

static uint64_t scan_back_sentence(reader_t *r, uint64_t from)
{
    if (from == 0) return 0;
    uint64_t win = from > 1536 ? from - 1536 : 0;
    char buf[1536];
    size_t n = (size_t)(from - win);
    if (n > sizeof buf) n = sizeof buf;
    if (fseek(r->file, (long)win, SEEK_SET) != 0) return 0;
    n = fread(buf, 1, n, r->file);
    (void)fseek(r->file, (long)r->stream.file_pos, SEEK_SET);
    size_t skip = 0;
    if (win > 0) {
        while (skip < n && !is_ws_byte((unsigned char)buf[skip])) skip++;
        while (skip < n && is_ws_byte((unsigned char)buf[skip])) skip++;
    }
    if (skip >= n) return win;
    rsvp_mem_src_t mem;
    rsvp_stream_t s;
    rsvp_stream_init_mem(&s, &mem, buf + skip, n - skip);
    s.offset_bias = win + skip;
    rsvp_token_t tok, last_initial;
    bool have_initial = false, have_prev = false;
    rsvp_token_t prev;
    memset(&last_initial, 0, sizeof last_initial);
    memset(&prev, 0, sizeof prev);
    while (rsvp_next_token(&s, &tok)) {
        if (tok.byte_offset >= from) break;
        if (!have_prev || (prev.flags & RSVP_F_SENTENCE_END) || (tok.flags & RSVP_F_PARAGRAPH)) {
            last_initial = tok;
            have_initial = true;
        }
        prev = tok;
        have_prev = true;
    }
    return have_initial ? last_initial.byte_offset : (win + skip);
}

static uint64_t rewind_offset(reader_t *r)
{
    if (!r->have_current || r->current.byte_offset == 0) return 0;
    /* Walk back from the word before the current one. That lands on the
     * current sentence start, or the previous sentence if we are already there. */
    for (int i = r->hist_n - 2; i >= 0; --i) {
        bool initial = (i == 0) || (r->hist[i - 1].flags & RSVP_F_SENTENCE_END) ||
                       (r->hist[i].flags & RSVP_F_PARAGRAPH);
        if (initial) return r->hist[i].offset;
    }
    return scan_back_sentence(r, r->current.byte_offset);
}

static void paint_current_word(reader_t *r)
{
    if (!r->have_current) return;
    draw_rect_t next = {0};
    draw_play_word_update(r->now_frame, r->current.text,
                          (font_id_t)r->settings->font, (int)r->settings->align,
                          r->have_word_dirty ? &r->word_dirty : NULL,
                          &next, NULL, NULL);
    r->word_dirty = next;
    r->have_word_dirty = true;
    (void)display_present(r->now_frame);
    r->scans_this_word = 0;
    rsvp_sched_begin_word(&r->sched, esp_timer_get_time(), r->word_ms);
}

static void commit_token(reader_t *r, rsvp_token_t tok)
{
    rsvp_fit_result_t fit;
    rsvp_fit_word(&tok, (font_id_t)r->settings->font, true, RSVP_MAX_WORD_PX, &fit);
    if (fit.split_bytes > 0 && fit.split_bytes < tok.byte_length) {
        rsvp_token_t rest = tok;
        rest.byte_offset += fit.split_bytes;
        rest.byte_length = (uint16_t)(tok.byte_length - fit.split_bytes);
        memmove(rest.text, tok.text + fit.split_bytes, rest.byte_length);
        rest.text[rest.byte_length] = 0;
        rest.codepoints = (uint16_t)utf8_count_codepoints(rest.text, rest.byte_length);
        rest.flags |= RSVP_F_CONTINUED;
        rest.flags &= (uint16_t)~RSVP_F_PARAGRAPH;
        tok.byte_length = fit.split_bytes;
        tok.text[tok.byte_length] = 0;
        tok.codepoints = (uint16_t)utf8_count_codepoints(tok.text, tok.byte_length);
        tok.flags &= (uint16_t)~(RSVP_F_SENTENCE_END | RSVP_F_CLAUSE);
        q_push_front(r, &rest);
    }
    r->current = tok;
    r->have_current = true;
    hist_push(r, tok.byte_offset, tok.flags);
    r->word_ms = rsvp_token_duration_ms(&tok, r->settings->wpm, r->settings->punct_mode);
    r->words_since_save++;
    if (r->words_since_save >= 15 || (tok.flags & RSVP_F_SENTENCE_END)) save_progress(r);
}

static void present_token(reader_t *r, rsvp_token_t tok)
{
    commit_token(r, tok);
    paint_current_word(r);
}

static void jump_to(reader_t *r, uint64_t off, bool play_after)
{
    if (off > r->file_size) off = r->file_size;
    hist_truncate_before(r, off);
    r->qc = 0;
    r->qh = 0;
    r->stream_eof = false;
    r->have_current = false;
    r->finished = false;
    rsvp_sched_reset(&r->sched);
    (void)rsvp_stream_seek(&r->stream, off);
    fill_queue(r);
    rsvp_token_t tok;
    if (q_pop(r, &tok)) commit_token(r, tok);
    r->playing = play_after;
    save_progress(r);
}

static void load_excerpt(reader_t *r, char *buf, size_t buf_sz, size_t *len,
                         size_t *hl0, size_t *hl1)
{
    *len = 0;
    *hl0 = *hl1 = 0;
    if (!r->have_current || buf_sz < 8) return;
    const uint64_t word_off = r->current.byte_offset;
    const uint16_t word_len = r->current.byte_length;
    uint64_t from = word_off > 160 ? word_off - 160 : 0;
    uint64_t to = word_off + word_len + 160;
    if (to > r->file_size) to = r->file_size;
    size_t n = (size_t)(to - from);
    if (n >= buf_sz) n = buf_sz - 1;
    if (fseek(r->file, (long)from, SEEK_SET) != 0) return;
    n = fread(buf, 1, n, r->file);
    (void)fseek(r->file, (long)r->stream.file_pos, SEEK_SET);
    size_t skip = 0;
    if (from > 0) {
        while (skip < n && !utf8_is_boundary((unsigned char)buf[skip])) skip++;
    }
    if (skip) {
        memmove(buf, buf + skip, n - skip);
        n -= skip;
        from += skip;
    }
    buf[n] = 0;
    *len = n;
    if (word_off >= from && word_off + word_len <= from + n) {
        *hl0 = (size_t)(word_off - from);
        *hl1 = *hl0 + word_len;
    }
}

static void show_paused(reader_t *r)
{
    char excerpt[400];
    size_t n = 0, hl0 = 0, hl1 = 0;
    load_excerpt(r, excerpt, sizeof excerpt, &n, &hl0, &hl1);
    unsigned percent = 0;
    if (r->file_size) {
        uint64_t off = r->have_current ? r->current.byte_offset : 0;
        percent = (unsigned)((off * 100ull) / r->file_size);
        if (percent > 100) percent = 100;
    }
    uint8_t frame[FB_BYTES];
    draw_paused(frame, excerpt, n, hl0, hl1, percent, r->settings->wpm);
    (void)display_present(frame);
    (void)display_pump_until_idle(1500);
}

static reader_result_t finish_and_wait_home(reader_t *r)
{
    r->finished = true;
    r->playing = false;
    rsvp_session_pause(&r->session, esp_timer_get_time());
    save_progress(r);
    clear_last_if_this(r);

    char took[32];
    rsvp_format_session_line(took, sizeof took,
                             rsvp_session_seconds(&r->session, 0),
                             r->session_resumed);
    uint8_t frame[FB_BYTES];
    draw_message(frame, "Finished", took, NULL, DRAW_HINT_DONE);
    (void)ui_present_full(frame);

    /* Do not treat a key that was already down as "any button". Clear the
     * queue and wait for release while the finished image drains. */
    board_input_clear();
    int64_t released_at = 0;
    while (board_input_held() || released_at == 0 ||
           esp_timer_get_time() - released_at < 40000) {
        unsigned ev = board_input_pop();
        if (ev == INPUT_POWER) return READER_POWER;
        if (!board_input_held() && released_at == 0)
            released_at = esp_timer_get_time();
        if (board_input_held()) released_at = 0;
        if (display_service_once() != ESP_OK) return READER_FAIL;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (;;) {
        unsigned ev = board_input_pop();
        if (ev == INPUT_NONE) break;
        if (ev == INPUT_POWER) return READER_POWER;
    }

    for (;;) {
        unsigned ev = board_input_pop();
        if (ev == INPUT_POWER) return READER_POWER;
        if (ev == INPUT_SELECT || ev == INPUT_NEXT ||
            ev == INPUT_PREVIOUS || ev == INPUT_BACK) {
            return READER_HOME;
        }
        if (display_service_once() != ESP_OK) return READER_FAIL;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static int countdown(reader_t *r)
{
    unsigned n = r->settings->countdown;
    if (!n) return 1;
    for (unsigned i = n; i >= 1; --i) {
        char num[12];
        snprintf(num, sizeof num, "%u", i);
        uint8_t frame[FB_BYTES];
        draw_message(frame, num, "Starting", r->current.text, "Up: skip");
        (void)display_present(frame);
        int64_t until = esp_timer_get_time() + 1000000;
        while (esp_timer_get_time() < until) {
            unsigned ev = board_input_pop();
            if (ev == INPUT_SELECT) return 1;
            if (ev == INPUT_BACK) return 0;
            if (ev == INPUT_POWER) return -2;
            if (display_service_once() != ESP_OK) return -1;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return 1;
}

reader_result_t reader_open(const char *rel, rsvp_settings_t *settings, bool resume)
{
    if (!rel || !rel[0] || !settings) {
        ui_show_message("Open error", "Missing book path", "", "Hold Down: back");
        return READER_BACK;
    }
    static reader_t r;
    memset(&r, 0, sizeof r);
    r.settings = settings;
    draw_set_reader_hints(settings->show_hints);
    strncpy(r.rel, rel, sizeof r.rel - 1);
    if (!board_sd_make_path(rel, r.path, sizeof r.path)) {
        ui_show_message("Open error", "Bad book path", "", "Hold Down: back");
        return READER_BACK;
    }
    r.file = fopen(r.path, "rb");
    if (!r.file) {
        ui_show_message("Open error", "Cannot open book", rel, "Hold Down: back");
        return READER_BACK;
    }
    if (fseek(r.file, 0, SEEK_END) != 0) {
        fclose(r.file);
        ui_show_message("Open error", "Cannot seek in file", rel, "Hold Down: back");
        return READER_BACK;
    }
    long sz = ftell(r.file);
    if (sz < 0) {
        fclose(r.file);
        ui_show_message("Open error", "Cannot measure file", rel, "Hold Down: back");
        return READER_BACK;
    }
    r.file_size = (uint64_t)sz;
    rewind(r.file);

    uint64_t start = 0;
    bool opened_complete = false;
    if (resume) {
        (void)board_sd_load_bookmark_state(rel, r.file_size, &start, &opened_complete);
    }
    r.opened_complete = opened_complete;
    if (!resume || start > r.file_size) start = 0;
    if (start > 0 && start < r.file_size) {
        if (fseek(r.file, (long)start, SEEK_SET) == 0) {
            unsigned char b = 0;
            while (start < r.file_size && fread(&b, 1, 1, r.file) == 1) {
                if (utf8_is_boundary(b)) {
                    (void)fseek(r.file, (long)start, SEEK_SET);
                    break;
                }
                start++;
            }
        }
    }

    rsvp_stream_init_file(&r.stream, r.file);
    if (rsvp_stream_seek(&r.stream, start) != 0) {
        fclose(r.file);
        ui_show_message("Open error", "Cannot read file", rel, "Hold Down: back");
        return READER_BACK;
    }

    fill_queue(&r);
    rsvp_token_t first;
    if (!q_pop(&r, &first)) {
        fclose(r.file);
        ui_show_message("Empty", "No words in this file", rel, "Hold Down: back");
        return READER_BACK;
    }
    commit_token(&r, first);
    r.session_resumed = start > 0;
    if (!r.opened_complete) remember_last(&r);
    ESP_LOGI(TAG, "opened %s size=%llu offset=%llu complete=%d", rel,
             (unsigned long long)r.file_size, (unsigned long long)start,
             r.opened_complete ? 1 : 0);

    /* Do not paint the word, then the countdown, then the word again.
     * That used to submit a vendor full for each step. Commit first so
     * the countdown can show the upcoming word, then paint it once. */
    int cd = countdown(&r);
    if (cd <= 0) {
        if (r.progress_live || !r.opened_complete) save_progress(&r);
        fclose(r.file);
        if (cd == -2) return READER_POWER;
        if (cd < 0) {
            ui_show_message("Stopped", "Display error", rel, "Hold Down: back");
            return READER_BACK;
        }
        return READER_BACK;
    }
    r.have_word_dirty = false;
    r.progress_live = true;
    if (r.opened_complete) remember_last(&r);
    save_progress(&r);
    paint_current_word(&r);
    r.playing = true;
    rsvp_session_resume(&r.session, esp_timer_get_time());
    board_input_clear();

    reader_result_t result = READER_BACK;
    while (!display_failed()) {
        unsigned ev = board_input_pop();
        if (ev == INPUT_POWER) {
            result = READER_POWER;
            break;
        }
        if (ev == INPUT_BACK) {
            result = READER_BACK;
            break;
        }
        if (ev == INPUT_PREVIOUS) {
            r.playing = false;
            int se = ui_edit_settings(r.settings);
            settings_store_save(r.settings);
            if (se == -2) {
                result = READER_POWER;
                break;
            }
            draw_set_reader_hints(r.settings->show_hints);
            if (r.have_current && !r.finished) {
                r.playing = false;
                rsvp_session_pause(&r.session, esp_timer_get_time());
                rsvp_sched_reset(&r.sched);
                show_paused(&r);
            }
            continue;
        }
        if (ev == INPUT_SELECT) {
            r.playing = !r.playing;
            if (!r.playing) {
                rsvp_session_pause(&r.session, esp_timer_get_time());
                rsvp_sched_reset(&r.sched);
                show_paused(&r);
            } else if (r.have_current) {
                r.word_ms = rsvp_token_duration_ms(&r.current, r.settings->wpm,
                                                   r.settings->punct_mode);
                rsvp_sched_reset(&r.sched);
                rsvp_session_resume(&r.session, esp_timer_get_time());
                r.have_word_dirty = false;
                paint_current_word(&r);
            }
            continue;
        }
        if (ev == INPUT_REFRESH) {
            ui_clear_ghosting();
            rsvp_sched_reset(&r.sched);
            if (r.playing && r.have_current) paint_current_word(&r);
            else if (r.have_current) show_paused(&r);
            continue;
        }
        if (ev == INPUT_NEXT) {
            if (r.finished) continue;
            rsvp_session_pause(&r.session, esp_timer_get_time());
            uint64_t off = rewind_offset(&r);
            jump_to(&r, off, false);
            show_paused(&r);
            continue;
        }

        if (r.playing && r.have_current && !r.finished) {
            int64_t now = esp_timer_get_time();
            if (r.scans_this_word >= 1 && rsvp_sched_due(&r.sched, now)) {
                fill_queue(&r);
                rsvp_token_t next;
                if (q_pop(&r, &next)) {
                    present_token(&r, next);
                } else if (r.stream_eof) {
                    result = finish_and_wait_home(&r);
                    break;
                }
                /* else: next token not ready; do not skip, try again next loop */
            }
        }

        unsigned scans0 = display_scans_since_full();
        if (display_service_once() != ESP_OK) {
            ui_show_message("Stopped", "Display error", rel, "Hold Down: back");
            result = READER_BACK;
            break;
        }
        /* Only a real mixed/hold scan counts toward "at least one scan". */
        if (display_scans_since_full() != scans0 &&
            r.playing && r.have_current && !r.finished)
            r.scans_this_word++;
        if (!display_pending_pixels()) vTaskDelay(pdMS_TO_TICKS(5));
    }

    save_progress(&r);
    fclose(r.file);
    return result;
}
