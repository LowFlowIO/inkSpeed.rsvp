#pragma once

#include "font.h"
#include "rsvp_timing.h"

#define RSVP_ALIGN_CENTER 0
#define RSVP_ALIGN_ORP 1
#define RSVP_LAST_REL_MAX 192
#define RSVP_COUNTDOWN_MAX 3

typedef struct {
    unsigned wpm;
    unsigned font;       /* font_id_t */
    unsigned punct_mode;
    unsigned countdown;
    unsigned align;
    unsigned show_hints; /* 1: reader footer hints, 0: word dead-center */
    char last_rel[RSVP_LAST_REL_MAX];
} rsvp_settings_t;

void rsvp_settings_defaults(rsvp_settings_t *s);
void rsvp_settings_clamp(rsvp_settings_t *s);
const char *rsvp_punct_name(unsigned mode);
const char *rsvp_align_name(unsigned align);
