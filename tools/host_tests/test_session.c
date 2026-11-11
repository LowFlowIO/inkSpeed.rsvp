#include "rsvp_session.h"
#include "draw.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

void expect_true(int cond, const char *msg);
void expect_eq_s(const char *a, const char *b, const char *msg);
void expect_eq_u(unsigned a, unsigned b, const char *msg);

static void expect_fmt(unsigned sec, const char *want)
{
    char got[24];
    rsvp_format_duration(sec, got, sizeof got);
    expect_eq_s(got, want, want);
}

void test_session_time(void)
{
    expect_fmt(0, "0:00");
    expect_fmt(5, "0:05");
    expect_fmt(260, "4:20");
    expect_fmt(3599, "59:59");
    expect_fmt(3600, "1:00:00");
    expect_fmt(3723, "1:02:03");
    expect_fmt(36000, "10:00:00");

    char line[40];
    rsvp_format_session_line(line, sizeof line, 260, false);
    expect_eq_s(line, "Read in 4:20", "full-book line");
    rsvp_format_session_line(line, sizeof line, 260, true);
    expect_eq_s(line, "This session 4:20", "resume line");
    rsvp_format_session_line(line, sizeof line, 3723, true);
    expect_eq_s(line, "This session 1:02:03", "resume hour line");

    expect_true(font_measure(FONT_SMALL, "Read in 4:20") <= FB_WIDTH - 24,
                "Read in 4:20 fits");
    expect_true(font_measure(FONT_SMALL, "This session 4:20") <= FB_WIDTH - 24,
                "This session 4:20 fits");
    expect_true(font_measure(FONT_SMALL, "This session 10:00:00") <= FB_WIDTH - 24,
                "This session 10:00:00 fits");

    rsvp_session_t s;
    rsvp_session_reset(&s);
    expect_eq_u(rsvp_session_seconds(&s, 0), 0, "fresh is 0");

    rsvp_session_resume(&s, 0);
    rsvp_session_pause(&s, 260 * 1000000ll);
    expect_eq_u(rsvp_session_seconds(&s, 260 * 1000000ll), 260, "4:20 run");
    /* Further wall time while paused must not count. */
    expect_eq_u(rsvp_session_seconds(&s, 1000 * 1000000ll), 260, "paused time excluded");

    rsvp_session_resume(&s, 1000 * 1000000ll);
    rsvp_session_pause(&s, 1010 * 1000000ll);
    expect_eq_u(rsvp_session_seconds(&s, 2000 * 1000000ll), 270, "second run added, gap excluded");

    rsvp_session_reset(&s);
    rsvp_session_resume(&s, 0);
    rsvp_session_resume(&s, 50 * 1000000ll); /* no-op, must not restart the clock */
    expect_eq_u(rsvp_session_seconds(&s, 80 * 1000000ll), 80, "double resume keeps first anchor");
    rsvp_session_pause(&s, 80 * 1000000ll);
    rsvp_session_pause(&s, 200 * 1000000ll);
    expect_eq_u(rsvp_session_seconds(&s, 200 * 1000000ll), 80, "double pause is a no-op");
}
