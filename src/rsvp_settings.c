#include "rsvp_settings.h"
#include <string.h>

void rsvp_settings_defaults(rsvp_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->wpm = RSVP_WPM_DEFAULT;
    s->font = FONT_LARGE;
    s->punct_mode = RSVP_PUNCT_NORMAL;
    s->countdown = 2;
    s->align = RSVP_ALIGN_ORP;
    s->show_hints = 1;
}

void rsvp_settings_clamp(rsvp_settings_t *s)
{
    if (!s) return;
    s->wpm = rsvp_clamp_wpm(s->wpm);
    if (s->font >= FONT_COUNT) s->font = FONT_LARGE;
    if (s->punct_mode > RSVP_PUNCT_LONG) s->punct_mode = RSVP_PUNCT_NORMAL;
    if (s->countdown > RSVP_COUNTDOWN_MAX) s->countdown = RSVP_COUNTDOWN_MAX;
    if (s->align > RSVP_ALIGN_ORP) s->align = RSVP_ALIGN_ORP;
    s->show_hints = s->show_hints ? 1 : 0;
    s->last_rel[RSVP_LAST_REL_MAX - 1] = 0;
}

const char *rsvp_punct_name(unsigned mode)
{
    if (mode == RSVP_PUNCT_OFF) return "Off";
    if (mode == RSVP_PUNCT_LONG) return "Long";
    return "Normal";
}

const char *rsvp_align_name(unsigned align)
{
    return align == RSVP_ALIGN_ORP ? "ORP" : "Center";
}
