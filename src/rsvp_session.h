#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Session reading time (pure C, host-tested). Times are microseconds,
 * same as esp_timer_get_time(). Paused intervals are not counted. */

typedef struct {
    int64_t accumulated_us;
    int64_t running_since_us;
    bool running;
} rsvp_session_t;

void rsvp_session_reset(rsvp_session_t *s);
void rsvp_session_resume(rsvp_session_t *s, int64_t now_us);
void rsvp_session_pause(rsvp_session_t *s, int64_t now_us);

/* Whole seconds of active time, including a still-running interval. */
unsigned rsvp_session_seconds(const rsvp_session_t *s, int64_t now_us);

/* m:ss under an hour, h:mm:ss at or above. */
void rsvp_format_duration(unsigned sec, char *out, size_t out_sz);

/* "Read in 4:20" or "This session 4:20". */
void rsvp_format_session_line(char *out, size_t out_sz, unsigned sec, bool resumed);
