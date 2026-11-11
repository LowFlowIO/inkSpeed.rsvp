/* Host tests for rsvp_sched: absolute deadlines, debt cap, pause resync,
 * and a full-file simulation of the play loop at fixed scan periods. */
#include "rsvp_sched.h"
#include "rsvp_tokenizer.h"
#include "rsvp_timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* expect_* live in test_core.c */
void expect_true(int cond, const char *msg);
void expect_eq_u(unsigned a, unsigned b, const char *msg);

static char *sched_load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    *len = got;
    return b;
}

/* One pass through the file, matching the firmware play loop. Optional
 * pause_after_us inserts a stall and a reset (pause/resume). */
static int sim_play(const char *data, size_t len, unsigned wpm, unsigned punct,
                    int64_t scan_us, int64_t pause_after_us, int64_t pause_us,
                    unsigned *out_n, int64_t *out_elapsed_us, int64_t *out_ideal_us)
{
    rsvp_mem_src_t mem;
    rsvp_stream_t stream;
    rsvp_stream_init_mem(&stream, &mem, data, len);
    rsvp_token_t tok;
    if (!rsvp_next_token(&stream, &tok)) return -1;

    rsvp_sched_t sched;
    rsvp_sched_reset(&sched);
    int64_t t = 0;
    unsigned n = 0;
    int64_t ideal = 0;
    unsigned word_ms = rsvp_token_duration_ms(&tok, wpm, punct);
    rsvp_sched_begin_word(&sched, t, word_ms);
    ideal += (int64_t)word_ms * 1000;
    unsigned scans = 0;
    int paused = 0;
    for (;;) {
        n++;
        for (;;) {
            if (scans >= 1 && rsvp_sched_due(&sched, t)) break;
            t += scan_us;
            scans++;
            if (scans > 100000u) return -2;
        }
        if (!paused && pause_after_us > 0 && t >= pause_after_us) {
            t += pause_us;
            rsvp_sched_reset(&sched);
            paused = 1;
            rsvp_sched_begin_word(&sched, t, word_ms);
            ideal += (int64_t)word_ms * 1000;
            scans = 0;
            continue;
        }
        if (!rsvp_next_token(&stream, &tok)) break;
        word_ms = rsvp_token_duration_ms(&tok, wpm, punct);
        rsvp_sched_begin_word(&sched, t, word_ms);
        ideal += (int64_t)word_ms * 1000;
        scans = 0;
    }
    *out_n = n;
    *out_elapsed_us = t;
    *out_ideal_us = ideal;
    return 0;
}

static const char *demo_path(void)
{
    const char *env = getenv("INKSPEED_DEMO_TXT");
    if (env && env[0]) return env;
    static const char *cands[] = {
        "tools/host_tests/demo_rsvp_guide.txt",
        "demo_rsvp_guide.txt",
        NULL
    };
    for (int i = 0; cands[i]; i++) {
        FILE *f = fopen(cands[i], "rb");
        if (f) { fclose(f); return cands[i]; }
    }
    return "tools/host_tests/demo_rsvp_guide.txt";
}

void test_sched_unit(void)
{
    rsvp_sched_t s;
    rsvp_sched_reset(&s);
    expect_true(!rsvp_sched_due(&s, 0), "inactive not due");
    rsvp_sched_begin_word(&s, 0, 86);
    expect_true(!rsvp_sched_due(&s, 85000), "before deadline");
    expect_true(rsvp_sched_due(&s, 86000), "at deadline");
    rsvp_sched_begin_word(&s, 99000, 86);
    expect_true(!rsvp_sched_due(&s, 171999), "absolute still early");
    expect_true(rsvp_sched_due(&s, 172000), "absolute due");
    expect_eq_u((unsigned)(rsvp_sched_debt_cap_us(86) / 1000), 500, "cap at least 500ms");
    expect_true(rsvp_sched_debt_cap_us(200) == 500000, "200ms word still 500ms cap");
    expect_true(rsvp_sched_debt_cap_us(400) == 2 * 400ll * 1000, "400ms word cap is 2x");

    rsvp_sched_reset(&s);
    rsvp_sched_begin_word(&s, 0, 86);
    rsvp_sched_begin_word(&s, 2 * 1000 * 1000, 86);
    expect_true(!rsvp_sched_due(&s, 2 * 1000 * 1000), "resync not immediately due");
    expect_true(rsvp_sched_due(&s, 2 * 1000 * 1000 + 86000), "resync full duration");
}

void test_sched_demo(void)
{
    size_t len = 0;
    char *data = sched_load(demo_path(), &len);
    expect_true(data != NULL, "demo file readable");
    if (!data) return;

    unsigned n = 0;
    int64_t elapsed = 0, ideal = 0;
    struct {
        unsigned wpm;
        int64_t scan_us;
        int panel_limited;
    } cases[] = {
        {700, 33000, 0},
        {700, 40000, 0},
        {700, 50000, 0},
        {700, 60000, 0},
        {700, 90000, 1},
        {350, 33000, 0},
        {350, 40000, 0},
        {350, 50000, 0},
        {350, 60000, 0},
        {350, 90000, 0},
    };
    printf("scheduler on demo file (%zu bytes):\n", len);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int rc = sim_play(data, len, cases[i].wpm, RSVP_PUNCT_OFF, cases[i].scan_us,
                          0, 0, &n, &elapsed, &ideal);
        expect_true(rc == 0, "sim_play ok");
        double sec = elapsed / 1e6;
        double ideal_sec = ideal / 1e6;
        double eff = n * 60.0 / sec;
        printf("  %u WPM  scan=%lldms  tokens=%u  ideal=%.1fs  elapsed=%.1fs  effective=%.0f WPM\n",
               cases[i].wpm, (long long)(cases[i].scan_us / 1000), n, ideal_sec, sec, eff);
        if (cases[i].panel_limited) {
            double cap = 60000000.0 / (double)cases[i].scan_us;
            expect_true(eff <= cap + 5, "panel-limited rate at or below 1/scan");
            expect_true(eff >= cap - 30, "panel-limited rate near 1/scan");
        } else {
            double ratio = sec / ideal_sec;
            expect_true(ratio < 1.08, "elapsed close to ideal when scan < slot");
            expect_true(ratio > 0.97, "not faster than the token timer");
        }
    }

    int rc = sim_play(data, len, 700, RSVP_PUNCT_OFF, 33000,
                      2 * 1000 * 1000, 2 * 1000 * 1000, &n, &elapsed, &ideal);
    expect_true(rc == 0, "sim_play with pause ok");
    double net = (elapsed - 2 * 1000 * 1000) / 1e6;
    double ideal_sec = ideal / 1e6;
    printf("  pause+resync: net=%.1fs ideal=%.1fs tokens=%u\n", net, ideal_sec, n);
    expect_true(net / ideal_sec < 1.08, "pause resync does not drag");
    expect_true(net / ideal_sec > 0.97, "pause resync does not rush");
    free(data);
}
