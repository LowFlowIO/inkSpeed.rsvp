#pragma once

#include <stddef.h>
#include <stdint.h>
#include "utf8.h"

#define FONT_COUNT 3

typedef enum {
    FONT_SMALL = 0,
    FONT_MEDIUM = 1,
    FONT_LARGE = 2,
} font_id_t;

int font_ascent(font_id_t id);
int font_descent(font_id_t id);
int font_line_height(font_id_t id);
int font_px_size(font_id_t id);
const char *font_id_name(font_id_t id);

int font_measure_n(font_id_t id, const char *utf8, size_t nbytes);
int font_measure(font_id_t id, const char *utf8);

/* Draw black glyphs onto a 200x200 1-bit framebuffer (1 = white, 0 = black,
 * MSB leftmost). `baseline` is the typographic baseline. */
void font_draw_n(uint8_t *frame, int x, int baseline, font_id_t id,
                 const char *utf8, size_t nbytes);
void font_draw(uint8_t *frame, int x, int baseline, font_id_t id, const char *utf8);

/* 0-based codepoint index of a Spritz-style recognition point. */
int font_orp_index_n(const char *utf8, size_t nbytes);
int font_orp_index(const char *utf8);

/* Pixel offset from the left of the string to the center of that codepoint. */
int font_codepoint_center_x(font_id_t id, const char *utf8, size_t nbytes, int index);
