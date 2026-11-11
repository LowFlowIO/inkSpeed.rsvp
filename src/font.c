#include "font.h"
#include "font_priv.h"
#include "draw.h"
#include <string.h>

static const font_face_t *face_of(font_id_t id)
{
    if ((unsigned)id >= FONT_COUNT) id = FONT_SMALL;
    return font_faces[id];
}

int font_ascent(font_id_t id) { return face_of(id)->ascent; }
int font_descent(font_id_t id) { return face_of(id)->descent; }
int font_line_height(font_id_t id) { return face_of(id)->line_height; }
int font_px_size(font_id_t id) { return face_of(id)->px; }

const char *font_id_name(font_id_t id)
{
    switch (id) {
    case FONT_SMALL: return "Small";
    case FONT_MEDIUM: return "Medium";
    case FONT_LARGE: return "Large";
    default: return "Small";
    }
}

static const font_glyph_t *find_glyph(const font_face_t *face, uint32_t cp)
{
    int lo = 0, hi = face->glyph_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t g = face->glyphs[mid].cp;
        if (g == cp) return &face->glyphs[mid];
        if (g < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

static int glyph_advance(const font_face_t *face, uint32_t cp)
{
    const font_glyph_t *g = find_glyph(face, cp);
    if (g) return g->advance;
    if (cp == ' ' || cp == 0x00A0u) return face->space_advance;
    return (face->px * 3) / 5;
}

int font_measure_n(font_id_t id, const char *utf8, size_t nbytes)
{
    const font_face_t *face = face_of(id);
    if (!utf8) return 0;
    int width = 0;
    size_t i = 0;
    while (i < nbytes) {
        uint32_t cp;
        int used = utf8_next(utf8 + i, nbytes - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        width += glyph_advance(face, cp);
    }
    return width;
}

int font_measure(font_id_t id, const char *utf8)
{
    return font_measure_n(id, utf8, utf8 ? strlen(utf8) : 0);
}

static void draw_missing(uint8_t *frame, int x, int baseline, const font_face_t *face)
{
    int w = (face->px * 3) / 5;
    int top = baseline - (face->ascent * 4) / 5;
    int bot = baseline + face->descent / 3;
    if (bot <= top) bot = top + 4;
    for (int yy = top; yy <= bot; ++yy) {
        fb_pixel_black(frame, x, yy);
        fb_pixel_black(frame, x + w - 1, yy);
    }
    for (int xx = x; xx < x + w; ++xx) {
        fb_pixel_black(frame, xx, top);
        fb_pixel_black(frame, xx, bot);
    }
}

static void draw_glyph(uint8_t *frame, int origin_x, int baseline,
                       const font_face_t *face, const font_glyph_t *g)
{
    if (!g->width || !g->height) return;
    const uint8_t *bits = face->bits + g->bitmap_off;
    const int bits_n = (int)g->width * (int)g->height;
    for (int i = 0; i < bits_n; ++i) {
        if (!(bits[i / 8] & (uint8_t)(0x80u >> (i % 8)))) continue;
        int gx = i % g->width;
        int gy = i / g->width;
        fb_pixel_black(frame, origin_x + g->xoff + gx, baseline + g->yoff + gy);
    }
}

void font_draw_n(uint8_t *frame, int x, int baseline, font_id_t id,
                 const char *utf8, size_t nbytes)
{
    const font_face_t *face = face_of(id);
    if (!frame || !utf8) return;
    int pen = x;
    size_t i = 0;
    while (i < nbytes) {
        uint32_t cp;
        int used = utf8_next(utf8 + i, nbytes - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        const font_glyph_t *g = find_glyph(face, cp);
        if (g) draw_glyph(frame, pen, baseline, face, g);
        else if (cp != ' ' && cp != 0x00A0u) draw_missing(frame, pen, baseline, face);
        pen += glyph_advance(face, cp);
    }
}

void font_draw(uint8_t *frame, int x, int baseline, font_id_t id, const char *utf8)
{
    font_draw_n(frame, x, baseline, id, utf8, utf8 ? strlen(utf8) : 0);
}

int font_orp_index_n(const char *utf8, size_t nbytes)
{
    unsigned n = utf8_count_codepoints(utf8, nbytes);
    if (n == 0) return 0;
    int idx;
    if (n <= 1) idx = 0;
    else if (n <= 5) idx = 1;
    else if (n <= 9) idx = 2;
    else if (n <= 13) idx = 3;
    else idx = 4;
    if ((unsigned)idx >= n) idx = (int)n - 1;
    return idx;
}

int font_orp_index(const char *utf8)
{
    return font_orp_index_n(utf8, utf8 ? strlen(utf8) : 0);
}

int font_codepoint_center_x(font_id_t id, const char *utf8, size_t nbytes, int index)
{
    const font_face_t *face = face_of(id);
    if (!utf8 || index < 0) return 0;
    int x = 0;
    int i_cp = 0;
    size_t i = 0;
    while (i < nbytes) {
        uint32_t cp;
        int used = utf8_next(utf8 + i, nbytes - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        const font_glyph_t *g = find_glyph(face, cp);
        int adv = glyph_advance(face, cp);
        if (i_cp == index) {
            if (g && g->width) return x + g->xoff + g->width / 2;
            return x + adv / 2;
        }
        x += adv;
        i_cp++;
    }
    return x;
}
