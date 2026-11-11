#pragma once

#include "rsvp_settings.h"
#include <stdbool.h>

typedef enum {
    READER_BACK = 0,
    READER_HOME,
    READER_POWER,
    READER_FAIL,
} reader_result_t;

/* resume: pick up a partial bookmark. false starts at the first word. */
reader_result_t reader_open(const char *rel, rsvp_settings_t *settings, bool resume);
