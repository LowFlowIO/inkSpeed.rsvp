#pragma once

#include <stdint.h>
#include "font.h"

typedef struct {
    uint32_t cp;
    uint8_t width;
    uint8_t height;
    int8_t xoff;
    int8_t yoff;
    int8_t advance;
    uint16_t bitmap_off;
} font_glyph_t;

typedef struct {
    const char *label;
    int px;
    int ascent;
    int descent;
    int line_height;
    int space_advance;
    const font_glyph_t *glyphs;
    int glyph_count;
    const uint8_t *bits;
} font_face_t;

extern const font_face_t *const font_faces[FONT_COUNT];
