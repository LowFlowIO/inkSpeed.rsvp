#include "rsvp_session.h"
#include <stdio.h>

void rsvp_session_reset(rsvp_session_t *s)
{
    if (!s) return;
    s->accumulated_us = 0;
    s->running_since_us = 0;
    s->running = false;
}

void rsvp_session_resume(rsvp_session_t *s, int64_t now_us)
{
    if (!s || s->running) return;
    s->running_since_us = now_us;
    s->running = true;
}

void rsvp_session_pause(rsvp_session_t *s, int64_t now_us)
{
    if (!s || !s->running) return;
    int64_t dt = now_us - s->running_since_us;
    if (dt > 0) s->accumulated_us += dt;
    s->running = false;
}

unsigned rsvp_session_seconds(const rsvp_session_t *s, int64_t now_us)
{
    if (!s) return 0;
    int64_t total = s->accumulated_us;
    if (s->running) {
        int64_t dt = now_us - s->running_since_us;
        if (dt > 0) total += dt;
    }
    if (total < 0) total = 0;
    return (unsigned)(total / 1000000);
}

void rsvp_format_duration(unsigned sec, char *out, size_t out_sz)
{
    if (!out || !out_sz) return;
    unsigned h = sec / 3600u;
    unsigned m = (sec / 60u) % 60u;
    unsigned s = sec % 60u;
    if (h)
        snprintf(out, out_sz, "%u:%02u:%02u", h, m, s);
    else
        snprintf(out, out_sz, "%u:%02u", m, s);
}

void rsvp_format_session_line(char *out, size_t out_sz, unsigned sec, bool resumed)
{
    if (!out || !out_sz) return;
    char dur[24];
    rsvp_format_duration(sec, dur, sizeof dur);
    snprintf(out, out_sz, "%s %s", resumed ? "This session" : "Read in", dur);
}
