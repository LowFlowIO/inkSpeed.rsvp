#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Drift-compensated RSVP deadlines. Pure C, no ESP-IDF, so host tests can
 * drive it. Times are microseconds, same as esp_timer_get_time().
 *
 * Words are scheduled on an absolute timeline: each word adds its duration
 * (including punctuation pauses) to a running deadline instead of starting
 * a fresh timer when it appears. If a scan overruns, later words run a bit
 * short (but still at least one display service pass) until the average
 * matches the selected WPM. A long stall (pause, full refresh, or being
 * behind by more than the debt cap) resyncs rather than rushing. */

#define RSVP_SCHED_DEBT_CAP_US 500000
#define RSVP_SCHED_DEBT_WORDS 2

typedef struct {
    int64_t deadline_us;
    bool active;
} rsvp_sched_t;

void rsvp_sched_reset(rsvp_sched_t *s);

/* Call when a word is painted. word_ms is rsvp_token_duration_ms(). */
void rsvp_sched_begin_word(rsvp_sched_t *s, int64_t now_us, unsigned word_ms);

/* True once the current word's deadline has been reached. */
bool rsvp_sched_due(const rsvp_sched_t *s, int64_t now_us);

/* Debt cap used for a word of this duration: max(500 ms, 2 * word_ms). */
int64_t rsvp_sched_debt_cap_us(unsigned word_ms);
