#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "font.h"

#define FB_WIDTH 200
#define FB_HEIGHT 200
#define FB_STRIDE 25
#define FB_BYTES 5000

typedef struct {
    int x0, y0, x1, y1; /* half-open; empty when x1<=x0 or y1<=y0 */
} draw_rect_t;

bool draw_rect_empty(const draw_rect_t *r);
void draw_rect_clamp(draw_rect_t *r);
void draw_rect_union(draw_rect_t *dst, const draw_rect_t *src);

void fb_clear(uint8_t *frame, bool white);
void fb_fill_rect(uint8_t *frame, int x0, int y0, int x1, int y1, bool white);
void fb_pixel_black(uint8_t *frame, int x, int y);
void fb_invert_rect(uint8_t *frame, int x0, int y0, int x1, int y1);
void fb_hline(uint8_t *frame, int x0, int x1, int y);
void fb_vline(uint8_t *frame, int x, int y0, int y1);

/* Clipped rounded rectangle. Radius shrinks to fit; empty or inverted
 * (x1<=x0 / y1<=y0) ranges are no-ops. `white` fills with paper (1) or ink (0). */
void fb_fill_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1,
                        int radius, bool white);
void fb_invert_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1, int radius);
void fb_stroke_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1, int radius);

void draw_text_centered(uint8_t *frame, int baseline, font_id_t id, const char *text);
void draw_message(uint8_t *frame, const char *title, const char *line1,
                  const char *line2, const char *footer);
void draw_text_page(uint8_t *frame, const char *title,
                     const char *const *lines, int count, int first,
                     const char *hint1, const char *hint2);
void draw_menu(uint8_t *frame, const char *title, const char *subtitle,
               const char *const *items, unsigned count, unsigned selected,
               const char *hint1, const char *hint2);

/* Top of the reserved 2-line hint band. Content, word boxes, and ORP
 * marks stay strictly above this line so a static play footer survives
 * dirty word updates. */
int draw_footer_top(void);
void draw_footer_hints(uint8_t *frame, const char *line1, const char *line2);

/* Reader footer. Off recenters the word in the full panel. */
void draw_set_reader_hints(bool on);
bool draw_reader_hints(void);

#define DRAW_HINT_MENU_1      "Up: Select   Down: Next"
#define DRAW_HINT_MENU_2      "Hold Down: Back"
#define DRAW_HINT_SETTINGS_1  "Up: Change   Down: Next"
#define DRAW_HINT_SETTINGS_2  "Hold Down: Back"
#define DRAW_HINT_PAUSE_1     "Up: Play   Down: Rewind"
#define DRAW_HINT_PAUSE_2     "Hold Up: Settings"
#define DRAW_HINT_PLAY_1      "Up: Pause  Down: Rewind"
#define DRAW_HINT_PLAY_2      "Hold Up: Settings"
#define DRAW_HINT_DONE        "Any Button: Home"

/* Word-wrap text into a box. Optional highlight of a byte range within `text`. */
void draw_wrapped(uint8_t *frame, const char *text, size_t nbytes, font_id_t id,
                  int x0, int top, int x1, int bottom,
                  size_t highlight_start, size_t highlight_end);

enum { DRAW_ALIGN_CENTER = 0, DRAW_ALIGN_ORP = 1 };
#define DRAW_ORP_COLUMN 80

/* Rasterize one RSVP word onto a freshly cleared white frame. */
void draw_play_word(uint8_t *frame, const char *word, font_id_t preferred,
                    int align, font_id_t *used, int *width);

/* Ink bounds of the word plus ORP focus marks when align is ORP.
 * Includes padding so a later clear erases glyph side-bearings. */
draw_rect_t draw_play_word_bounds(const char *word, font_id_t preferred, int align);

/* Update only the union of prev_dirty and the new word bounds (or the whole
 * frame when prev_dirty is NULL/empty). Pixels outside that union are left
 * unchanged so chrome/status outside the reading band is not redrawn. */
void draw_play_word_update(uint8_t *frame, const char *word, font_id_t preferred,
                           int align, const draw_rect_t *prev_dirty,
                           draw_rect_t *out_dirty, font_id_t *used, int *width);

/* Boot splash and sleep: the nlogo.bin frame. */
void draw_splash(uint8_t *frame);
void draw_sleep(uint8_t *frame);

void draw_paused(uint8_t *frame, const char *excerpt, size_t excerpt_len,
                 size_t highlight_start, size_t highlight_end,
                 unsigned percent, unsigned wpm);
