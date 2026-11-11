#include "draw.h"
#include "rsvp_fit.h"
#include "sleep_logo.h"
#include <stdio.h>
#include <string.h>

bool draw_rect_empty(const draw_rect_t *r)
{
    return !r || r->x1 <= r->x0 || r->y1 <= r->y0;
}

void draw_rect_clamp(draw_rect_t *r)
{
    if (!r) return;
    if (r->x0 < 0) r->x0 = 0;
    if (r->y0 < 0) r->y0 = 0;
    if (r->x1 > FB_WIDTH) r->x1 = FB_WIDTH;
    if (r->y1 > FB_HEIGHT) r->y1 = FB_HEIGHT;
    if (r->x1 < r->x0) r->x1 = r->x0;
    if (r->y1 < r->y0) r->y1 = r->y0;
}

void draw_rect_union(draw_rect_t *dst, const draw_rect_t *src)
{
    if (!dst || draw_rect_empty(src)) return;
    if (draw_rect_empty(dst)) {
        *dst = *src;
        draw_rect_clamp(dst);
        return;
    }
    if (src->x0 < dst->x0) dst->x0 = src->x0;
    if (src->y0 < dst->y0) dst->y0 = src->y0;
    if (src->x1 > dst->x1) dst->x1 = src->x1;
    if (src->y1 > dst->y1) dst->y1 = src->y1;
    draw_rect_clamp(dst);
}

void fb_clear(uint8_t *frame, bool white)
{
    memset(frame, white ? 0xFF : 0x00, FB_BYTES);
}

void fb_fill_rect(uint8_t *frame, int x0, int y0, int x1, int y1, bool white)
{
    if (!frame) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint8_t *byte = &frame[y * FB_STRIDE + x / 8];
            uint8_t bit = (uint8_t)(0x80u >> (x % 8));
            if (white) *byte |= bit;
            else *byte &= (uint8_t)~bit;
        }
    }
}

void fb_pixel_black(uint8_t *frame, int x, int y)
{
    if ((unsigned)x >= FB_WIDTH || (unsigned)y >= FB_HEIGHT) return;
    frame[y * FB_STRIDE + x / 8] &= (uint8_t)~(0x80u >> (x % 8));
}

void fb_invert_rect(uint8_t *frame, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            frame[y * FB_STRIDE + x / 8] ^= (uint8_t)(0x80u >> (x % 8));
        }
    }
}

void fb_hline(uint8_t *frame, int x0, int x1, int y)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; ++x) fb_pixel_black(frame, x, y);
}

void fb_vline(uint8_t *frame, int x, int y0, int y1)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; ++y) fb_pixel_black(frame, x, y);
}

static int clamp_radius(int w, int h, int radius)
{
    if (radius < 0) radius = 0;
    int max_r = w < h ? w / 2 : h / 2;
    if (max_r < 0) max_r = 0;
    if (radius > max_r) radius = max_r;
    return radius;
}

void fb_stroke_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1, int radius)
{
    if (!frame) return;
    fb_fill_round_rect(frame, x0, y0, x1, y1, radius, false);
    if (x1 - x0 > 2 && y1 - y0 > 2) {
        int ir = radius > 0 ? radius - 1 : 0;
        fb_fill_round_rect(frame, x0 + 1, y0 + 1, x1 - 1, y1 - 1, ir, true);
    }
}

void fb_fill_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1, int radius, bool white)
{
    if (!frame) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    radius = clamp_radius(w, h, radius);
    long rr = (long)radius * (long)radius;
    for (int y = y0; y < y1; ++y) {
        int inset = 0;
        if (radius > 0) {
            int dy;
            if (y < y0 + radius) dy = (y0 + radius - 1) - y;
            else if (y >= y1 - radius) dy = y - (y1 - radius);
            else dy = -1;
            if (dy >= 0) {
                long yy = (long)dy * (long)dy;
                int lo = 0, hi = radius;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if ((long)mid * mid + yy <= rr) lo = mid + 1;
                    else hi = mid;
                }
                int span = lo > 0 ? lo - 1 : 0;
                inset = radius - span;
                if (inset < 0) inset = 0;
            }
        }
        int xa = x0 + inset, xb = x1 - inset;
        if (xa < xb) fb_fill_rect(frame, xa, y, xb, y + 1, white);
    }
}

void fb_invert_round_rect(uint8_t *frame, int x0, int y0, int x1, int y1, int radius)
{
    if (!frame) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    /* Clip so a partially off-screen rect still draws the visible part. */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    radius = clamp_radius(w, h, radius);
    long rr = (long)radius * (long)radius;
    for (int y = y0; y < y1; ++y) {
        int inset = 0;
        if (radius > 0) {
            int dy;
            if (y < y0 + radius) dy = (y0 + radius - 1) - y;
            else if (y >= y1 - radius) dy = y - (y1 - radius);
            else dy = -1;
            if (dy >= 0) {
                long yy = (long)dy * (long)dy;
                int lo = 0, hi = radius;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if ((long)mid * mid + yy <= rr) lo = mid + 1;
                    else hi = mid;
                }
                int span = lo > 0 ? lo - 1 : 0;
                inset = radius - span;
                if (inset < 0) inset = 0;
            }
        }
        int xa = x0 + inset, xb = x1 - inset;
        if (xa < xb) fb_invert_rect(frame, xa, y, xb, y + 1);
    }
}

static void ellipsize(char *dst, size_t dst_sz, const char *src, font_id_t id, int max_px)
{
    if (!dst_sz) return;
    dst[0] = 0;
    if (!src) return;
    size_t len = strlen(src);
    if (font_measure_n(id, src, len) <= max_px) {
        if (len >= dst_sz) len = dst_sz - 1;
        memcpy(dst, src, len);
        dst[len] = 0;
        return;
    }
    const char *dots = "...";
    int dots_w = font_measure(id, dots);
    unsigned cps = utf8_count_codepoints(src, len);
    while (cps > 0) {
        cps--;
        size_t n = utf8_prefix_bytes(src, len, cps);
        if (font_measure_n(id, src, n) + dots_w <= max_px || cps == 0) {
            if (n + 3 >= dst_sz) n = dst_sz > 4 ? dst_sz - 4 : 0;
            memcpy(dst, src, n);
            memcpy(dst + n, dots, 4);
            return;
        }
    }
    if (dst_sz > 4) memcpy(dst, dots, 4);
}

void draw_text_centered(uint8_t *frame, int baseline, font_id_t id, const char *text)
{
    if (!text) return;
    int w = font_measure(id, text);
    int x = (FB_WIDTH - w) / 2;
    if (x < 0) x = 0;
    font_draw(frame, x, baseline, id, text);
}

static bool s_reader_hints = true;

void draw_set_reader_hints(bool on) { s_reader_hints = on; }
bool draw_reader_hints(void) { return s_reader_hints; }

int draw_footer_top(void)
{
    const int ascent = font_ascent(FONT_SMALL);
    const int descent = font_descent(FONT_SMALL);
    const int line_h = font_line_height(FONT_SMALL);
    const int foot_pad = 6;
    const int gap = 4;
    int last_base = FB_HEIGHT - foot_pad - descent;
    int first_base = last_base - line_h;
    return first_base - ascent - gap;
}

/* Area the word may occupy. Full panel when reader hints are off. */
static int reader_content_bottom(void)
{
    return s_reader_hints ? draw_footer_top() : FB_HEIGHT - 4;
}

/* Black bar, white title. Returns the y where content starts. */
static int draw_title_bar(uint8_t *frame, const char *title, const char *cue)
{
    const int ascent = font_ascent(FONT_SMALL);
    const int descent = font_descent(FONT_SMALL);
    const int bar_h = ascent + descent + 8;
    if (title && title[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, title, FONT_SMALL, FB_WIDTH - 16);
        draw_text_centered(frame, 4 + ascent, FONT_SMALL, clipped);
    }
    if (cue && cue[0]) {
        int w = font_measure(FONT_SMALL, cue);
        font_draw(frame, FB_WIDTH - 6 - w, 4 + ascent, FONT_SMALL, cue);
    }
    fb_invert_rect(frame, 0, 0, FB_WIDTH, bar_h);
    return bar_h;
}

void draw_footer_hints(uint8_t *frame, const char *line1, const char *line2)
{
    if (!frame) return;
    const int descent = font_descent(FONT_SMALL);
    const int line_h = font_line_height(FONT_SMALL);
    const int foot_pad = 6;
    const int max_w = FB_WIDTH - 16;
    int last_base = FB_HEIGHT - foot_pad - descent;
    bool have2 = line2 && line2[0];
    int first_base = have2 ? last_base - line_h : last_base;
    if (line1 && line1[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, line1, FONT_SMALL, max_w);
        draw_text_centered(frame, first_base, FONT_SMALL, clipped);
    }
    if (have2) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, line2, FONT_SMALL, max_w);
        draw_text_centered(frame, last_base, FONT_SMALL, clipped);
    }
}

void draw_message(uint8_t *frame, const char *title, const char *line1,
                  const char *line2, const char *footer)
{
    fb_clear(frame, true);
    const int pad = 12;
    const int inner = 8;
    fb_fill_round_rect(frame, inner, inner, FB_WIDTH - inner, FB_HEIGHT - inner, 10, false);
    fb_fill_round_rect(frame, inner + 1, inner + 1, FB_WIDTH - inner - 1, FB_HEIGHT - inner - 1, 9, true);

    int foot_top = FB_HEIGHT - inner;
    if (footer && footer[0]) {
        foot_top = draw_footer_top();
        draw_footer_hints(frame, footer, NULL);
    }

    int y = pad + 2;
    if (title && title[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, title, FONT_MEDIUM, FB_WIDTH - 2 * pad);
        int base = y + font_ascent(FONT_MEDIUM);
        if (base + font_descent(FONT_MEDIUM) <= foot_top - 2) {
            draw_text_centered(frame, base, FONT_MEDIUM, clipped);
            y = base + font_descent(FONT_MEDIUM) + 12;
        }
    }
    if (line1 && line1[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, line1, FONT_SMALL, FB_WIDTH - 2 * pad);
        int base = y + font_ascent(FONT_SMALL);
        if (base + font_descent(FONT_SMALL) <= foot_top - 2) {
            draw_text_centered(frame, base, FONT_SMALL, clipped);
            y = base + font_descent(FONT_SMALL) + 6;
        }
    }
    if (line2 && line2[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, line2, FONT_SMALL, FB_WIDTH - 2 * pad);
        int base = y + font_ascent(FONT_SMALL);
        if (base + font_descent(FONT_SMALL) <= foot_top - 2) {
            draw_text_centered(frame, base, FONT_SMALL, clipped);
        }
    }
}

void draw_text_page(uint8_t *frame, const char *title,
                     const char *const *lines, int count, int first,
                     const char *hint1, const char *hint2)
{
    fb_clear(frame, true);
    int y = draw_title_bar(frame, title, NULL) + 6;
    const int line_h = font_line_height(FONT_SMALL);
    const int ascent = font_ascent(FONT_SMALL);
    const int bottom = draw_footer_top() - 2;
    if (first < 0) first = 0;
    if (first > count) first = count;
    for (int i = first; i < count; ++i) {
        if (y + ascent > bottom) break;
        if (lines[i] && lines[i][0]) {
            char clipped[64];
            ellipsize(clipped, sizeof clipped, lines[i], FONT_SMALL, FB_WIDTH - 16);
            font_draw(frame, 8, y + ascent, FONT_SMALL, clipped);
        }
        y += line_h;
    }
    draw_footer_hints(frame, hint1, hint2);
}

void draw_menu(uint8_t *frame, const char *title, const char *subtitle,
               const char *const *items, unsigned count, unsigned selected,
               const char *hint1, const char *hint2)
{
    fb_clear(frame, true);
    const font_id_t fid = FONT_SMALL;
    const int ascent = font_ascent(fid);
    const int descent = font_descent(fid);
    const int line_h = font_line_height(fid);
    const int pad = 8;
    const int radius = 6;
    const int content_bottom = draw_footer_top();

    if (!items) count = 0;
    if (count && selected >= count) selected = count - 1;

    const int row_pad = 2;
    const int row_h = ascent + descent + row_pad * 2;

    /* Page cue is drawn into the bar so it inverts with the title. */
    char cue[16] = "";
    int bar_h = draw_title_bar(frame, title, NULL);
    int y = bar_h + 6;
    if (subtitle && subtitle[0]) {
        char clipped[64];
        ellipsize(clipped, sizeof clipped, subtitle, fid, FB_WIDTH - 2 * pad);
        draw_text_centered(frame, y + ascent, fid, clipped);
        y += line_h + 6;
    } else {
        y += 6;
    }

    if (count) {

    unsigned visible_max = 5;
    if (row_h > 0) {
        unsigned fit = (unsigned)((content_bottom - y) / row_h);
        if (fit == 0) fit = 1;
        if (fit < visible_max) visible_max = fit;
    }
    unsigned pages = (count + visible_max - 1) / visible_max;
    unsigned first = (selected / visible_max) * visible_max;
    unsigned page = visible_max ? first / visible_max : 0;
    unsigned vis = count - first;
    if (vis > visible_max) vis = visible_max;

    if (pages > 1) {
        snprintf(cue, sizeof cue, "%u/%u", page + 1, pages);
        /* Redraw the bar with the cue so both invert together. */
        fb_invert_rect(frame, 0, 0, FB_WIDTH, bar_h);
        (void)draw_title_bar(frame, title, cue);
    }

    for (unsigned row = 0; row < vis; ++row) {
        unsigned idx = first + row;
        char clipped[64];
        ellipsize(clipped, sizeof clipped, items[idx], fid, FB_WIDTH - 2 * pad - 8);
        int row_top = y + (int)row * row_h;
        if (row_top + row_h > content_bottom) break;
        int baseline = row_top + row_pad + ascent;
        draw_text_centered(frame, baseline, fid, clipped);
        if (idx == selected) {
            fb_invert_round_rect(frame, pad, row_top + 1, FB_WIDTH - pad,
                                 row_top + row_h - 1, radius);
        }
    }
    }
    draw_footer_hints(frame, hint1, hint2);
}

void draw_wrapped(uint8_t *frame, const char *text, size_t nbytes, font_id_t id,
                  int x0, int top, int x1, int bottom,
                  size_t highlight_start, size_t highlight_end)
{
    if (!frame || !text || x1 <= x0) return;
    int max_w = x1 - x0;
    int line_h = font_line_height(id);
    int baseline = top + font_ascent(id);
    int hl_x0 = 0, hl_y0 = 0, hl_x1 = 0, hl_y1 = 0;
    bool have_hl = false;

    size_t i = 0;
    while (i < nbytes && baseline - font_ascent(id) < bottom) {
        int x = x0;
        /* Pack as many whole words as fit. A word too wide is split by codepoint. */
        if (text[i] == '\n') {
            baseline += line_h;
            i++;
            continue;
        }
        while (i < nbytes && text[i] != '\n' && baseline - font_ascent(id) < bottom) {
            size_t word_start = i;
            while (i < nbytes && text[i] != ' ' && text[i] != '\n' && text[i] != '\t') i++;
            size_t word_end = i;
            while (i < nbytes && (text[i] == ' ' || text[i] == '\t')) i++;
            size_t chunk_end = i; /* include trailing spaces when they fit */
            int w = font_measure_n(id, text + word_start, word_end - word_start);
            if (x > x0 && x + w > x0 + max_w) {
                baseline += line_h;
                x = x0;
                if (baseline - font_ascent(id) >= bottom) {
                    i = word_start;
                    break;
                }
            }
            if (w > max_w) {
                unsigned cps = utf8_count_codepoints(text + word_start, word_end - word_start);
                unsigned take = cps;
                while (take > 1 &&
                       font_measure_n(id, text + word_start,
                                      utf8_prefix_bytes(text + word_start,
                                                        word_end - word_start, take)) > max_w) {
                    take--;
                }
                size_t n = utf8_prefix_bytes(text + word_start, word_end - word_start, take);
                font_draw_n(frame, x, baseline, id, text + word_start, n);
                if (highlight_end > highlight_start) {
                    size_t a = word_start, b = word_start + n;
                    if (b > highlight_start && a < highlight_end) {
                        int hx = x + (a < highlight_start
                            ? font_measure_n(id, text + word_start, highlight_start - a) : 0);
                        size_t hs = a < highlight_start ? highlight_start : a;
                        size_t he = b < highlight_end ? b : highlight_end;
                        int hw = font_measure_n(id, text + hs, he - hs);
                        hl_x0 = hx; hl_x1 = hx + hw;
                        hl_y0 = baseline - font_ascent(id) - 1;
                        hl_y1 = baseline + font_descent(id) + 1;
                        have_hl = true;
                    }
                }
                i = word_start + n;
                baseline += line_h;
                x = x0;
                continue;
            }
            font_draw_n(frame, x, baseline, id, text + word_start, chunk_end - word_start);
            if (highlight_end > highlight_start) {
                size_t a = word_start, b = word_end;
                if (b > highlight_start && a < highlight_end) {
                    size_t hs = a < highlight_start ? highlight_start : a;
                    size_t he = b < highlight_end ? b : highlight_end;
                    int hx = x + font_measure_n(id, text + word_start, hs - a);
                    int hw = font_measure_n(id, text + hs, he - hs);
                    hl_x0 = hx; hl_x1 = hx + hw;
                    hl_y0 = baseline - font_ascent(id) - 1;
                    hl_y1 = baseline + font_descent(id) + 1;
                    have_hl = true;
                }
            }
            x += font_measure_n(id, text + word_start, chunk_end - word_start);
        }
        if (i < nbytes && text[i] == '\n') continue;
        if (i < nbytes) baseline += line_h;
        else break;
    }
    if (have_hl) fb_invert_round_rect(frame, hl_x0 - 2, hl_y0, hl_x1 + 2, hl_y1, 3);
}

static void draw_focus_mark(uint8_t *frame, int x)
{
    int bot = reader_content_bottom() - 2;
    for (int d = 0; d < 5; ++d) {
        fb_pixel_black(frame, x, 22 + d);
        fb_pixel_black(frame, x - 1, 22 + d);
        fb_pixel_black(frame, x + 1, 22 + d);
        fb_pixel_black(frame, x, bot - 1 - d);
        fb_pixel_black(frame, x - 1, bot - 1 - d);
        fb_pixel_black(frame, x + 1, bot - 1 - d);
    }
    fb_hline(frame, x - 3, x + 3, 21);
    fb_hline(frame, x - 3, x + 3, bot);
}

typedef struct {
    rsvp_fit_result_t fit;
    rsvp_token_t tok;
    int baseline;
    int x;
    int align;
} play_layout_t;

static void play_layout(const char *word, font_id_t preferred, int align, play_layout_t *out)
{
    memset(out, 0, sizeof *out);
    out->align = align;
    if (word) {
        size_t n = strlen(word);
        if (n > RSVP_TOKEN_MAX) n = RSVP_TOKEN_MAX;
        memcpy(out->tok.text, word, n);
        out->tok.text[n] = 0;
        out->tok.byte_length = (uint16_t)n;
        out->tok.codepoints = (uint16_t)utf8_count_codepoints(out->tok.text, n);
    }
    rsvp_fit_word(&out->tok, preferred, true, RSVP_MAX_WORD_PX, &out->fit);
    int content_h = reader_content_bottom();
    if (content_h < 40) content_h = FB_HEIGHT;
    out->baseline = (content_h + font_ascent(out->fit.font) - font_descent(out->fit.font)) / 2;
    if (align == DRAW_ALIGN_ORP && out->tok.text[0]) {
        int orp = font_orp_index_n(out->tok.text, out->tok.byte_length);
        int cx = font_codepoint_center_x(out->fit.font, out->tok.text, out->tok.byte_length, orp);
        out->x = DRAW_ORP_COLUMN - cx;
        if (out->x < 4) out->x = 4;
        if (out->x + out->fit.width > FB_WIDTH - 4) out->x = FB_WIDTH - 4 - out->fit.width;
        if (out->x < 4) out->x = 4;
    } else {
        out->x = (FB_WIDTH - out->fit.width) / 2;
        if (out->x < 4) out->x = 4;
    }
}

static void include_focus_marks(draw_rect_t *r)
{
    int bot_line = reader_content_bottom() - 2;
    draw_rect_t top = {DRAW_ORP_COLUMN - 3, 21, DRAW_ORP_COLUMN + 4, 27};
    draw_rect_t bot = {DRAW_ORP_COLUMN - 3, bot_line - 5, DRAW_ORP_COLUMN + 4, bot_line + 1};
    draw_rect_clamp(&top);
    draw_rect_clamp(&bot);
    draw_rect_union(r, &top);
    draw_rect_union(r, &bot);
}

draw_rect_t draw_play_word_bounds(const char *word, font_id_t preferred, int align)
{
    play_layout_t lay;
    play_layout(word, preferred, align, &lay);
    draw_rect_t r = {0, 0, 0, 0};
    const int pad = 6;
    if (lay.tok.byte_length > 0 && lay.fit.width > 0) {
        r.x0 = lay.x - pad;
        r.x1 = lay.x + lay.fit.width + pad;
        r.y0 = lay.baseline - font_ascent(lay.fit.font) - 3;
        r.y1 = lay.baseline + font_descent(lay.fit.font) + 3;
        draw_rect_clamp(&r);
    }
    if (align == DRAW_ALIGN_ORP) include_focus_marks(&r);
    return r;
}

static void paint_play_layout(uint8_t *frame, const play_layout_t *lay)
{
    if (lay->align == DRAW_ALIGN_ORP && lay->tok.text[0]) {
        draw_focus_mark(frame, DRAW_ORP_COLUMN);
    }
    if (lay->tok.byte_length) {
        font_draw_n(frame, lay->x, lay->baseline, lay->fit.font,
                    lay->tok.text, lay->tok.byte_length);
    }
}

void draw_play_word_update(uint8_t *frame, const char *word, font_id_t preferred,
                           int align, const draw_rect_t *prev_dirty,
                           draw_rect_t *out_dirty, font_id_t *used, int *width)
{
    play_layout_t lay;
    play_layout(word, preferred, align, &lay);
    if (used) *used = lay.fit.font;
    if (width) *width = lay.fit.width;
    draw_rect_t now = draw_play_word_bounds(word, preferred, align);
    if (!frame) {
        if (out_dirty) *out_dirty = now;
        return;
    }
    int footer = reader_content_bottom();
    if (!draw_rect_empty(&now) && now.y1 > footer) now.y1 = footer;
    if (draw_rect_empty(prev_dirty)) {
        fb_clear(frame, true);
        if (s_reader_hints) draw_footer_hints(frame, DRAW_HINT_PLAY_1, DRAW_HINT_PLAY_2);
    } else {
        draw_rect_t clear = *prev_dirty;
        draw_rect_union(&clear, &now);
        if (clear.y1 > footer) clear.y1 = footer;
        fb_fill_rect(frame, clear.x0, clear.y0, clear.x1, clear.y1, true);
    }
    paint_play_layout(frame, &lay);
    if (out_dirty) *out_dirty = now;
}

void draw_play_word(uint8_t *frame, const char *word, font_id_t preferred,
                    int align, font_id_t *used, int *width)
{
    draw_play_word_update(frame, word, preferred, align, NULL, NULL, used, width);
}

__attribute__((unused))
static void draw_brand(uint8_t *frame, const char *footer)
{
    fb_clear(frame, true);
    const char *title = "inkSpeed";
    font_id_t title_font = FONT_LARGE;
    if (font_measure(title_font, title) > FB_WIDTH - 16) title_font = FONT_MEDIUM;
    const char *sub = "RSVP Reader";
    int gap = 12;
    int block = font_line_height(title_font) + gap + font_line_height(FONT_SMALL);
    int top = (FB_HEIGHT - block) / 2;
    if (top < 8) top = 8;
    int title_base = top + font_ascent(title_font);
    int sub_base = title_base + font_descent(title_font) + gap + font_ascent(FONT_SMALL);
    int foot_top = FB_HEIGHT - 8;
    if (footer && footer[0]) {
        int foot_base = FB_HEIGHT - 10 - font_descent(FONT_SMALL);
        foot_top = foot_base - font_ascent(FONT_SMALL) - 8;
        if (sub_base + font_descent(FONT_SMALL) <= foot_top) {
            draw_text_centered(frame, foot_base, FONT_SMALL, footer);
        }
    }
    draw_text_centered(frame, title_base, title_font, title);
    if (sub_base + font_descent(FONT_SMALL) <= foot_top) {
        draw_text_centered(frame, sub_base, FONT_SMALL, sub);
    }
}

void draw_splash(uint8_t *frame)
{
    memcpy(frame, sleep_logo, FB_BYTES);
}

void draw_sleep(uint8_t *frame)
{
    memcpy(frame, sleep_logo, FB_BYTES);
}

void draw_paused(uint8_t *frame, const char *excerpt, size_t excerpt_len,
                 size_t highlight_start, size_t highlight_end,
                 unsigned percent, unsigned wpm)
{
    fb_clear(frame, true);
    const int pad = 8;
    char header[40];
    snprintf(header, sizeof header, "Paused  %u%%  %u WPM",
             percent > 100 ? 100 : percent, wpm);
    int head_base = pad + font_ascent(FONT_SMALL);
    draw_text_centered(frame, head_base, FONT_SMALL, header);
    int rule_y = head_base + font_descent(FONT_SMALL) + 4;
    fb_hline(frame, pad + 4, FB_WIDTH - pad - 4, rule_y);
    int foot_top = reader_content_bottom();
    draw_wrapped(frame, excerpt, excerpt_len, FONT_SMALL,
                 pad, rule_y + 6, FB_WIDTH - pad,
                 foot_top - 2,
                 highlight_start, highlight_end);
    if (s_reader_hints) draw_footer_hints(frame, DRAW_HINT_PAUSE_1, DRAW_HINT_PAUSE_2);
}
