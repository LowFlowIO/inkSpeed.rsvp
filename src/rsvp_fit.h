#pragma once

#include <stdbool.h>
#include "font.h"
#include "rsvp_tokenizer.h"

#define RSVP_MAX_WORD_PX 184

typedef struct {
    font_id_t font;
    int width;
    int orp_index;
    int orp_center_x;
    uint16_t split_bytes;
} rsvp_fit_result_t;

void rsvp_fit_word(const rsvp_token_t *tok, font_id_t preferred, bool allow_downsize,
                   int max_width, rsvp_fit_result_t *out);
