#include "rsvp_sched.h"

int64_t rsvp_sched_debt_cap_us(unsigned word_ms)
{
    int64_t cap = RSVP_SCHED_DEBT_CAP_US;
    int64_t two = (int64_t)RSVP_SCHED_DEBT_WORDS * (int64_t)word_ms * 1000;
    return two > cap ? two : cap;
}

void rsvp_sched_reset(rsvp_sched_t *s)
{
    if (!s) return;
    s->deadline_us = 0;
    s->active = false;
}

void rsvp_sched_begin_word(rsvp_sched_t *s, int64_t now_us, unsigned word_ms)
{
    if (!s) return;
    int64_t dur = (int64_t)word_ms * 1000;
    if (dur < 0) dur = 0;
    if (!s->active) {
        s->deadline_us = now_us + dur;
        s->active = true;
        return;
    }
    /* Extend the absolute timeline. If we are already far past the intended
     * start (pause, full refresh, or a long stall), resync so this word gets
     * its full duration instead of being due immediately. */
    int64_t intended_start = s->deadline_us;
    s->deadline_us += dur;
    if (now_us - intended_start > rsvp_sched_debt_cap_us(word_ms))
        s->deadline_us = now_us + dur;
}

bool rsvp_sched_due(const rsvp_sched_t *s, int64_t now_us)
{
    if (!s || !s->active) return false;
    return now_us >= s->deadline_us;
}
