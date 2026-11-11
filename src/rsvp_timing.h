#pragma once

#include "rsvp_tokenizer.h"

#define RSVP_WPM_MIN 100
#define RSVP_WPM_MAX 700
#define RSVP_WPM_STEP 25
#define RSVP_WPM_DEFAULT 300

#define RSVP_PUNCT_OFF 0
#define RSVP_PUNCT_NORMAL 1
#define RSVP_PUNCT_LONG 2

unsigned rsvp_clamp_wpm(unsigned wpm);
unsigned rsvp_base_ms(unsigned wpm);
unsigned rsvp_token_duration_ms(const rsvp_token_t *tok, unsigned wpm, unsigned punct_mode);
