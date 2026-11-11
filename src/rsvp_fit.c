#include "rsvp_fit.h"
#include <string.h>

static int measure(font_id_t id, const rsvp_token_t *tok, size_t n)
{
    return font_measure_n(id, tok->text, n);
}

void rsvp_fit_word(const rsvp_token_t *tok, font_id_t preferred, bool allow_downsize,
                   int max_width, rsvp_fit_result_t *out)
{
    memset(out, 0, sizeof *out);
    if (!tok || !out) return;
    if (max_width <= 0) max_width = RSVP_MAX_WORD_PX;
    if ((unsigned)preferred >= FONT_COUNT) preferred = FONT_LARGE;

    size_t n = tok->byte_length;
    if (n > RSVP_TOKEN_MAX) n = RSVP_TOKEN_MAX;

    font_id_t start = preferred;
    font_id_t end = allow_downsize ? FONT_SMALL : preferred;
    for (int id = (int)start; id >= (int)end; --id) {
        int w = measure((font_id_t)id, tok, n);
        if (w <= max_width || (id == (int)end && w > 0 &&
                               utf8_count_codepoints(tok->text, n) <= 1)) {
            out->font = (font_id_t)id;
            out->width = w;
            out->split_bytes = (uint16_t)n;
            out->orp_index = font_orp_index_n(tok->text, n);
            out->orp_center_x = font_codepoint_center_x(out->font, tok->text, n, out->orp_index);
            if (w <= max_width) return;
        }
        if (w <= max_width) {
            out->font = (font_id_t)id;
            out->width = w;
            out->split_bytes = (uint16_t)n;
            out->orp_index = font_orp_index_n(tok->text, n);
            out->orp_center_x = font_codepoint_center_x(out->font, tok->text, n, out->orp_index);
            return;
        }
    }

    /* Preferred sizes overflowed: use the smallest and split on a codepoint boundary. */
    font_id_t id = FONT_SMALL;
    unsigned cps = utf8_count_codepoints(tok->text, n);
    unsigned take = cps;
    size_t bytes = n;
    while (take > 1) {
        size_t cand = utf8_prefix_bytes(tok->text, n, take);
        if (measure(id, tok, cand) <= max_width) {
            bytes = cand;
            break;
        }
        take--;
        bytes = utf8_prefix_bytes(tok->text, n, take);
    }
    if (take <= 1) {
        take = cps ? 1 : 0;
        bytes = utf8_prefix_bytes(tok->text, n, take);
    }
    out->font = id;
    out->width = measure(id, tok, bytes);
    out->split_bytes = (uint16_t)bytes;
    out->orp_index = font_orp_index_n(tok->text, bytes);
    out->orp_center_x = font_codepoint_center_x(id, tok->text, bytes, out->orp_index);
}
