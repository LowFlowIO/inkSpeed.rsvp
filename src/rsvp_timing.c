#include "rsvp_timing.h"

unsigned rsvp_clamp_wpm(unsigned wpm)
{
    if (wpm < RSVP_WPM_MIN) return RSVP_WPM_MIN;
    if (wpm > RSVP_WPM_MAX) return RSVP_WPM_MAX;
    /* Snap down to the nearest step so stored values stay on the UI grid. */
    unsigned stepped = RSVP_WPM_MIN + ((wpm - RSVP_WPM_MIN) / RSVP_WPM_STEP) * RSVP_WPM_STEP;
    if (stepped < RSVP_WPM_MIN) stepped = RSVP_WPM_MIN;
    if (stepped > RSVP_WPM_MAX) stepped = RSVP_WPM_MAX;
    return stepped;
}

unsigned rsvp_base_ms(unsigned wpm)
{
    wpm = rsvp_clamp_wpm(wpm);
    return (60000u + wpm / 2u) / wpm;
}

unsigned rsvp_token_duration_ms(const rsvp_token_t *tok, unsigned wpm, unsigned punct_mode)
{
    unsigned base = rsvp_base_ms(wpm);
    unsigned extra = 0;
    if (!tok) return base;
    if (punct_mode > RSVP_PUNCT_LONG) punct_mode = RSVP_PUNCT_NORMAL;

    if (punct_mode == RSVP_PUNCT_OFF) {
        if (tok->flags & RSVP_F_PARAGRAPH) extra += base / 4u;
    } else {
        unsigned sentence = (base * 3u) / 5u; /* 60% */
        unsigned clause = (base * 3u) / 10u;  /* 30% */
        unsigned para = base;
        if (punct_mode == RSVP_PUNCT_LONG) {
            sentence = (sentence * 3u) / 2u;
            clause = (clause * 3u) / 2u;
            para = (para * 3u) / 2u;
        }
        if (tok->flags & RSVP_F_SENTENCE_END) extra += sentence;
        else if (tok->flags & RSVP_F_CLAUSE) extra += clause;
        if (tok->flags & RSVP_F_PARAGRAPH) extra += para;
    }

    unsigned letters = tok->codepoints;
    if (letters > 8u) extra += (base * (letters - 8u)) / 20u; /* 5% of base per extra letter */

    unsigned ms = base + extra;
    if (ms < 60u) ms = 60u;
    return ms;
}
