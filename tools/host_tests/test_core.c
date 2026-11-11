#include "rsvp_tokenizer.h"
#include "rsvp_timing.h"
#include "rsvp_fit.h"
#include "rsvp_bookmark.h"
#include "rsvp_settings.h"
#include "draw.h"
#include "drive_counters.h"
#include "font.h"
#include "upload_name.h"
#include "rsvp_sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_sched_unit(void);
void test_sched_demo(void);
void test_session_time(void);

static int g_fails;

void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fails++;
    }
}

void expect_eq_u(unsigned a, unsigned b, const char *msg)
{
    if (a != b) {
        fprintf(stderr, "FAIL: %s (got %u expected %u)\n", msg, a, b);
        g_fails++;
    }
}

void expect_eq_s(const char *a, const char *b, const char *msg)
{
    if (!a) a = "";
    if (!b) b = "";
    if (strcmp(a, b) != 0) {
        fprintf(stderr, "FAIL: %s (got '%s' expected '%s')\n", msg, a, b);
        g_fails++;
    }
}

static int tokenize_all(const char *text, rsvp_token_t *out, int cap)
{
    rsvp_mem_src_t mem;
    rsvp_stream_t s;
    rsvp_stream_init_mem(&s, &mem, text, strlen(text));
    int n = 0;
    while (n < cap && rsvp_next_token(&s, &out[n])) n++;
    return n;
}

static void test_tokenizer(void)
{
    rsvp_token_t t[16];
    int n = tokenize_all("Hello world", t, 16);
    expect_eq_u((unsigned)n, 2, "hello world count");
    expect_eq_s(t[0].text, "Hello", "hello");
    expect_eq_s(t[1].text, "world", "world");
    expect_eq_u((unsigned)t[0].byte_offset, 0, "hello offset");
    expect_eq_u((unsigned)t[1].byte_offset, 6, "world offset");

    n = tokenize_all("Hi,", t, 16);
    expect_eq_u((unsigned)n, 1, "clause count");
    expect_true(t[0].flags & RSVP_F_CLAUSE, "comma is clause");
    expect_true(!(t[0].flags & RSVP_F_SENTENCE_END), "comma not sentence");

    n = tokenize_all("End.", t, 16);
    expect_eq_u((unsigned)n, 1, "sentence count");
    expect_true(t[0].flags & RSVP_F_SENTENCE_END, "period is sentence");

    n = tokenize_all("Wait!", t, 16);
    expect_true(t[0].flags & RSVP_F_SENTENCE_END, "bang is sentence");

    n = tokenize_all("What?", t, 16);
    expect_true(t[0].flags & RSVP_F_SENTENCE_END, "question is sentence");

    n = tokenize_all("A\n\nB", t, 16);
    expect_eq_u((unsigned)n, 2, "paragraph count");
    expect_eq_s(t[0].text, "A", "para A");
    expect_eq_s(t[1].text, "B", "para B");
    expect_true(t[1].flags & RSVP_F_PARAGRAPH, "B after blank line");
    expect_true(!(t[0].flags & RSVP_F_PARAGRAPH), "A not paragraph");

    n = tokenize_all("  lots   of\tspace  ", t, 16);
    expect_eq_u((unsigned)n, 3, "collapsed whitespace");
    expect_eq_s(t[0].text, "lots", "lots");
    expect_eq_s(t[1].text, "of", "of");
    expect_eq_s(t[2].text, "space", "space");

    n = tokenize_all("caf\xC3\xA9 time", t, 16);
    expect_eq_u((unsigned)n, 2, "utf8 count");
    expect_eq_s(t[0].text, "caf\xC3\xA9", "cafe token");
    expect_eq_u(t[0].codepoints, 4, "cafe codepoints");
    expect_eq_u((unsigned)t[1].byte_offset, 6, "time offset after cafe");

    const char *quoted = "\"Hello.\"";
    n = tokenize_all(quoted, t, 16);
    expect_eq_u((unsigned)n, 1, "quoted sentence count");
    expect_true(t[0].flags & RSVP_F_SENTENCE_END, "quoted Hello. is sentence");

    /* BOM should be skipped and not become a token. */
    const char bom[] = "\xEF\xBB\xBF" "Hi";
    rsvp_mem_src_t mem;
    rsvp_stream_t s;
    rsvp_stream_init_mem(&s, &mem, bom, sizeof bom - 1);
    expect_true(rsvp_next_token(&s, &t[0]), "bom token");
    expect_eq_s(t[0].text, "Hi", "bom skipped");
    expect_eq_u((unsigned)t[0].byte_offset, 3, "offset after bom");

    /* Buffer-boundary spanning: a word that crosses a refill. */
    char longtext[3000];
    memset(longtext, 'x', 2040);
    memcpy(longtext + 2040, " edgeword after", 16);
    longtext[2056] = 0;
    /* 2040 x's are one long token then leftover chunks, then edgeword? 
     * 2040 x's without spaces is one huge word split at TOKEN_MAX (96). */
    n = tokenize_all(longtext, t, 16);
    expect_true(n >= 2, "long word produced multiple chunks");
    expect_eq_u((unsigned)t[0].byte_length, RSVP_TOKEN_MAX, "first chunk is max");
    expect_true(t[1].flags & RSVP_F_CONTINUED, "second chunk continued");

    /* A space just before the 2048 boundary, then a word. */
    memset(longtext, 'a', 2040);
    longtext[2040] = ' ';
    memcpy(longtext + 2041, "edgeword", 8);
    longtext[2049] = 0;
    rsvp_stream_init_mem(&s, &mem, longtext, 2049);
    /* skip the long 'aaa' token(s) */
    int saw_edge = 0;
    rsvp_token_t one;
    while (rsvp_next_token(&s, &one)) {
        if (strcmp(one.text, "edgeword") == 0) {
            saw_edge = 1;
            expect_eq_u((unsigned)one.byte_offset, 2041, "edgeword offset");
        }
    }
    expect_true(saw_edge, "edgeword across buffer boundary");

    /* Seek resumes at a later token. */
    rsvp_stream_init_mem(&s, &mem, "one two three", 13);
    expect_true(rsvp_next_token(&s, &t[0]), "one");
    expect_true(rsvp_stream_seek(&s, 4) == 0, "seek two");
    expect_true(rsvp_next_token(&s, &t[1]), "after seek");
    expect_eq_s(t[1].text, "two", "seeked to two");

    expect_true(rsvp_token_is_sentence_initial(NULL, &t[0]), "first is initial");
    t[0].flags = RSVP_F_SENTENCE_END;
    t[1].flags = 0;
    expect_true(rsvp_token_is_sentence_initial(&t[0], &t[1]), "after sentence");
    t[0].flags = 0;
    expect_true(!rsvp_token_is_sentence_initial(&t[0], &t[1]), "mid sentence");
}

static void test_timing(void)
{
    expect_eq_u(rsvp_clamp_wpm(50), 100, "clamp min");
    expect_eq_u(rsvp_clamp_wpm(800), 700, "clamp max");
    expect_eq_u(rsvp_clamp_wpm(312), 300, "clamp step");
    expect_eq_u(rsvp_base_ms(300), 200, "300 wpm is 200ms");
    expect_eq_u(rsvp_base_ms(600), 100, "600 wpm is 100ms");

    rsvp_token_t tok;
    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "word");
    tok.byte_length = 4;
    tok.codepoints = 4;
    unsigned base = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_NORMAL);
    expect_eq_u(base, 200, "plain word base");

    tok.flags = RSVP_F_SENTENCE_END;
    unsigned sent = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_NORMAL);
    expect_true(sent > base, "sentence pause adds time");
    unsigned sent_long = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_LONG);
    expect_true(sent_long > sent, "long punctuation > normal");
    unsigned sent_off = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_OFF);
    expect_eq_u(sent_off, base, "punct off ignores sentence");

    tok.flags = RSVP_F_CLAUSE;
    unsigned clause = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_NORMAL);
    expect_true(clause > base && clause < sent, "clause between base and sentence");

    tok.flags = RSVP_F_PARAGRAPH;
    unsigned para = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_NORMAL);
    expect_true(para > sent, "paragraph extra");

    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "extraordinarily");
    tok.byte_length = (uint16_t)strlen(tok.text);
    tok.codepoints = (uint16_t)strlen(tok.text);
    unsigned longw = rsvp_token_duration_ms(&tok, 300, RSVP_PUNCT_OFF);
    expect_true(longw > 200, "long word pause even if punct off");

    unsigned slow = rsvp_token_duration_ms(&tok, 150, RSVP_PUNCT_NORMAL);
    unsigned fast = rsvp_token_duration_ms(&tok, 600, RSVP_PUNCT_NORMAL);
    expect_true(slow >= fast, "higher WPM is not slower");
}

static void test_bookmark(void)
{
    rsvp_bookmark_t b = {
        .version = RSVP_BMK_VERSION,
        .file_size = 12345,
        .byte_offset = 678,
        .path_crc32 = rsvp_crc32("chapter1.txt", 12),
    };
    uint8_t raw[RSVP_BMK_SIZE];
    expect_true(rsvp_bookmark_encode(raw, &b), "encode");
    rsvp_bookmark_t out;
    expect_true(rsvp_bookmark_decode(raw, sizeof raw, &out), "decode");
    expect_eq_u((unsigned)out.file_size, 12345, "size");
    expect_eq_u((unsigned)out.byte_offset, 678, "offset");
    expect_eq_u(out.path_crc32, b.path_crc32, "path crc");

    raw[10] ^= 0xFF;
    expect_true(!rsvp_bookmark_decode(raw, sizeof raw, &out), "corrupt checksum rejected");

    expect_true(rsvp_bookmark_encode(raw, &b), "reencode");
    raw[0] = 'X';
    expect_true(!rsvp_bookmark_decode(raw, sizeof raw, &out), "bad magic rejected");

    /* Offset past EOF is rejected. */
    b.byte_offset = 20000;
    expect_true(rsvp_bookmark_encode(raw, &b), "encode past eof");
    expect_true(!rsvp_bookmark_decode(raw, sizeof raw, &out), "offset past size rejected");

    expect_true(rsvp_crc32("abc", 3) != rsvp_crc32("abd", 3), "crc differs");
    expect_eq_u(rsvp_crc32("abc", 3), rsvp_crc32("abc", 3), "crc stable");

    /* Completed flag lives in the v1 reserved field; old files stay valid. */
    rsvp_bookmark_t done = {
        .version = RSVP_BMK_VERSION,
        .file_size = 1000,
        .byte_offset = 400,
        .path_crc32 = 1,
    };
    rsvp_bookmark_mark_complete(&done);
    expect_true(done.flags & RSVP_BMK_F_COMPLETED, "complete flag set");
    expect_eq_u((unsigned)done.byte_offset, 0, "complete resets offset to start");
    expect_true(rsvp_bookmark_is_complete(&done), "flagged bookmark is complete");
    expect_eq_u((unsigned)rsvp_bookmark_resume_offset(&done), 0, "resume after complete is 0");
    expect_true(rsvp_bookmark_encode(raw, &done), "encode completed");
    expect_true(rsvp_bookmark_decode(raw, sizeof raw, &out), "decode completed");
    expect_true(out.flags & RSVP_BMK_F_COMPLETED, "completed flag survives encode");
    expect_eq_u((unsigned)out.byte_offset, 0, "completed offset stays 0");

    rsvp_bookmark_t legacy = {
        .version = RSVP_BMK_VERSION,
        .file_size = 2000,
        .byte_offset = 2000,
        .path_crc32 = 2,
    };
    expect_true(rsvp_bookmark_is_complete(&legacy), "legacy EOF bookmark is complete");
    expect_eq_u((unsigned)rsvp_bookmark_resume_offset(&legacy), 0, "legacy EOF resumes at 0");

    rsvp_bookmark_t mid = {
        .version = RSVP_BMK_VERSION,
        .file_size = 2000,
        .byte_offset = 1999,
        .path_crc32 = 3,
    };
    expect_true(!rsvp_bookmark_is_complete(&mid), "offset before EOF is not complete");
    expect_eq_u((unsigned)rsvp_bookmark_resume_offset(&mid), 1999, "mid-book resume kept");

    rsvp_bookmark_t emptyf = {
        .version = RSVP_BMK_VERSION,
        .file_size = 0,
        .byte_offset = 0,
        .path_crc32 = 4,
    };
    expect_true(!rsvp_bookmark_is_complete(&emptyf), "empty file is never complete");
}

static void test_fit(void)
{
    rsvp_token_t tok;
    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "Hi");
    tok.byte_length = 2;
    tok.codepoints = 2;
    rsvp_fit_result_t fit;
    rsvp_fit_word(&tok, FONT_LARGE, true, RSVP_MAX_WORD_PX, &fit);
    expect_eq_u(fit.font, FONT_LARGE, "short word stays large");
    expect_eq_u(fit.split_bytes, 2, "no split for Hi");
    expect_true(fit.width > 0 && fit.width <= RSVP_MAX_WORD_PX, "Hi width fits");

    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "Supercalifragilisticexpialidocious");
    tok.byte_length = (uint16_t)strlen(tok.text);
    tok.codepoints = tok.byte_length;
    rsvp_fit_word(&tok, FONT_LARGE, true, RSVP_MAX_WORD_PX, &fit);
    expect_true(fit.split_bytes <= tok.byte_length, "split not past end");
    expect_true(fit.width <= RSVP_MAX_WORD_PX || fit.split_bytes > 0, "long word handled");
    /* Either it downsized enough to fit entirely, or it split. */
    if (fit.split_bytes < tok.byte_length) {
        expect_eq_u(fit.font, FONT_SMALL, "split uses small font");
        expect_true(utf8_is_boundary((unsigned char)tok.text[fit.split_bytes]) ||
                    fit.split_bytes == tok.byte_length,
                    "split on utf8 boundary");
    } else {
        expect_true(fit.width <= RSVP_MAX_WORD_PX, "full long word fits after downsize");
    }

    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "caf\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                     "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                     "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                     "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9");
    tok.byte_length = (uint16_t)strlen(tok.text);
    tok.codepoints = (uint16_t)utf8_count_codepoints(tok.text, tok.byte_length);
    rsvp_fit_word(&tok, FONT_LARGE, true, 40, &fit);
    expect_true(fit.split_bytes > 0, "utf8 split produced prefix");
    expect_true(fit.split_bytes <= tok.byte_length, "utf8 split range");
    if (fit.split_bytes < tok.byte_length) {
        expect_true(utf8_is_boundary((unsigned char)tok.text[fit.split_bytes]),
                    "does not split cafe mid-sequence");
    }

    /* allow_downsize false should keep the preferred face even if wider. */
    memset(&tok, 0, sizeof tok);
    strcpy(tok.text, "Word");
    tok.byte_length = 4;
    tok.codepoints = 4;
    rsvp_fit_word(&tok, FONT_MEDIUM, false, RSVP_MAX_WORD_PX, &fit);
    expect_eq_u(fit.font, FONT_MEDIUM, "no downsize when disabled");
}

static void test_settings_and_draw(void)
{
    rsvp_settings_t s;
    rsvp_settings_defaults(&s);
    expect_eq_u(s.wpm, 300, "default wpm");
    expect_eq_u(s.font, FONT_LARGE, "default font");
    s.wpm = 999;
    s.font = 9;
    s.punct_mode = 9;
    rsvp_settings_clamp(&s);
    expect_eq_u(s.wpm, 700, "clamped wpm");
    expect_eq_u(s.font, FONT_LARGE, "clamped font");
    expect_eq_u(s.punct_mode, RSVP_PUNCT_NORMAL, "clamped punct");

    uint8_t frame[FB_BYTES];
    font_id_t used = FONT_SMALL;
    int width = 0;
    draw_play_word(frame, "Reading", FONT_LARGE, DRAW_ALIGN_ORP, &used, &width);
    expect_true(width > 0, "play word has width");
    /* At least some black pixels were written. */
    int black = 0;
    for (int i = 0; i < FB_BYTES; ++i) {
        black += (int)__builtin_popcount((unsigned char)~frame[i]);
    }
    expect_true(black > 20, "play word painted glyphs");
}

static int fb_black(const uint8_t *frame, int x, int y)
{
    if ((unsigned)x >= FB_WIDTH || (unsigned)y >= FB_HEIGHT) return 0;
    uint8_t byte = frame[y * FB_STRIDE + x / 8];
    return (byte & (uint8_t)(0x80u >> (x % 8))) == 0;
}

static void ink_bounds(const uint8_t *frame, int *min_x, int *min_y, int *max_x, int *max_y, int *count)
{
    *min_x = FB_WIDTH;
    *min_y = FB_HEIGHT;
    *max_x = -1;
    *max_y = -1;
    *count = 0;
    for (int y = 0; y < FB_HEIGHT; ++y) {
        for (int x = 0; x < FB_WIDTH; ++x) {
            if (!fb_black(frame, x, y)) continue;
            if (x < *min_x) *min_x = x;
            if (y < *min_y) *min_y = y;
            if (x > *max_x) *max_x = x;
            if (y > *max_y) *max_y = y;
            (*count)++;
        }
    }
}

static int count_black_rect(const uint8_t *frame, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            n += fb_black(frame, x, y);
    return n;
}

static void write_pbm(const char *path, const uint8_t *frame)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "WARN: could not write %s\n", path);
        return;
    }
    fprintf(f, "P1\n%d %d\n", FB_WIDTH, FB_HEIGHT);
    for (int y = 0; y < FB_HEIGHT; ++y) {
        for (int x = 0; x < FB_WIDTH; ++x) {
            fputc(fb_black(frame, x, y) ? '1' : '0', f);
            fputc(x + 1 == FB_WIDTH ? '\n' : ' ', f);
        }
    }
    fclose(f);
}

static void test_font_baseline(void)
{
    uint8_t frame[FB_BYTES];
    const int base = 80;

    fb_clear(frame, true);
    font_draw(frame, 40, base, FONT_SMALL, "H");
    int min_x, min_y, max_x, max_y, count;
    ink_bounds(frame, &min_x, &min_y, &max_x, &max_y, &count);
    expect_true(count > 10, "H painted");
    expect_true(min_y < base, "cap H has ink above the baseline");
    expect_true(min_y >= base - font_ascent(FONT_SMALL) - 1, "H does not start above ascent");
    expect_true(max_y <= base + 1, "cap H does not hang below the baseline");
    expect_true(max_y >= base - 3, "cap H reaches the baseline");

    fb_clear(frame, true);
    font_draw(frame, 40, base, FONT_SMALL, "g");
    ink_bounds(frame, &min_x, &min_y, &max_x, &max_y, &count);
    expect_true(count > 10, "g painted");
    expect_true(min_y < base, "g has ink above the baseline");
    expect_true(max_y > base, "g descender goes below the baseline");
    expect_true(max_y <= base + font_descent(FONT_SMALL) + 1, "g stays within descent");

    fb_clear(frame, true);
    font_draw(frame, 20, base, FONT_MEDIUM, "Hg");
    ink_bounds(frame, &min_x, &min_y, &max_x, &max_y, &count);
    expect_true(min_y < base && max_y > base, "medium Hg straddles baseline");
    expect_true(min_y >= base - font_ascent(FONT_MEDIUM) - 1, "medium H within ascent");
}

static void test_menu_alignment(void)
{
    uint8_t frame[FB_BYTES];
    const char *items[] = { "rsvp.txt" };
    draw_menu(frame, "Books", NULL, items, 1, 0, DRAW_HINT_MENU_1, DRAW_HINT_MENU_2);

    const char *dir = getenv("INKSPEED_PREVIEW_DIR");
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/menu_books.pbm", dir);
        write_pbm(path, frame);
    }

    /* Header text ("Books") must sit near the top, not down around y=55. */
    int header_ink = count_black_rect(frame, 20, 0, FB_WIDTH - 20, 30);
    expect_true(header_ink > 20, "Books title ink near top");

    /* Highlight bar: rows that are mostly black across the inner width.
     * Skip the title bar, which is a full-width black header. */
    int hl0 = -1, hl1 = -1;
    for (int y = 28; y < FB_HEIGHT; ++y) {
        int row_black = count_black_rect(frame, 6, y, FB_WIDTH - 6, y + 1);
        if (row_black >= 140) {
            if (hl0 < 0) hl0 = y;
            hl1 = y + 1;
        }
    }
    expect_true(hl0 >= 30, "highlight starts below the header");
    expect_true(hl1 > hl0 + 14, "highlight is tall enough for a text row");
    expect_true(hl1 < 160, "highlight is not in the lower half by mistake");

    /* Selected text is inverted, so the bar must contain white "holes"
     * (the glyphs) rather than being a solid rectangle. */
    int white_in_bar = (hl1 - hl0) * (FB_WIDTH - 12) - count_black_rect(frame, 6, hl0, FB_WIDTH - 6, hl1);
    expect_true(white_in_bar > 20, "highlight contains inverted glyph pixels");

    /* Rounded corners stay paper-white; the flat mid-edge is ink. */
    expect_true(!fb_black(frame, 8, hl0), "books highlight top-left corner is rounded");
    expect_true(!fb_black(frame, FB_WIDTH - 9, hl0), "books highlight top-right corner is rounded");
    expect_true(fb_black(frame, 8, (hl0 + hl1) / 2), "books highlight mid-left edge is filled");

    /* And almost no black text should sit immediately beneath the bar. */
    int below_to = hl1 + 16 > FB_HEIGHT ? FB_HEIGHT : hl1 + 16;
    int below = count_black_rect(frame, 20, hl1, FB_WIDTH - 20, below_to);
    expect_true(below < 20, "selected label is not hanging below the highlight");
    int books_foot = count_black_rect(frame, 16, draw_footer_top(), FB_WIDTH - 16, FB_HEIGHT);
    expect_true(books_foot > 16, "books footer hints are present");

    /* Home-style menu: first item "Books" must also sit inside its bar. */
    const char *home_items[] = { "Books", "Settings", "Power off" };
    draw_menu(frame, "Home", "No SD card", home_items, 3, 0,
              DRAW_HINT_MENU_1, DRAW_HINT_MENU_2);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/menu_home.pbm", dir);
        write_pbm(path, frame);
    }
    hl0 = hl1 = -1;
    for (int y = 28; y < FB_HEIGHT; ++y) {
        int row_black = count_black_rect(frame, 6, y, FB_WIDTH - 6, y + 1);
        if (row_black >= 140) {
            if (hl0 < 0) hl0 = y;
            hl1 = y + 1;
        }
    }
    expect_true(hl0 > 0 && hl1 > hl0 + 14, "home highlight present");
    white_in_bar = (hl1 - hl0) * (FB_WIDTH - 12) - count_black_rect(frame, 6, hl0, FB_WIDTH - 6, hl1);
    expect_true(white_in_bar > 20, "home highlight contains inverted text");
    expect_true(!fb_black(frame, 8, hl0), "home highlight top-left corner is rounded");
    expect_true(fb_black(frame, 8, (hl0 + hl1) / 2), "home highlight mid-left edge is filled");

    const char *settings_items[] = {
        "Speed  300", "Size  Medium", "Pauses  Normal", "Count  3 s", "Align  ORP"
    };
    draw_menu(frame, "Settings", NULL, settings_items, 5, 0,
              DRAW_HINT_SETTINGS_1, DRAW_HINT_SETTINGS_2);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/menu_settings.pbm", dir);
        write_pbm(path, frame);
    }
    hl0 = hl1 = -1;
    for (int y = 28; y < FB_HEIGHT; ++y) {
        int row_black = count_black_rect(frame, 6, y, FB_WIDTH - 6, y + 1);
        if (row_black >= 140) {
            if (hl0 < 0) hl0 = y;
            hl1 = y + 1;
        }
    }
    expect_true(hl0 > 0 && hl1 > hl0 + 14, "settings highlight present");
    white_in_bar = (hl1 - hl0) * (FB_WIDTH - 12) - count_black_rect(frame, 6, hl0, FB_WIDTH - 6, hl1);
    expect_true(white_in_bar > 20, "settings highlight contains inverted text");
    expect_true(!fb_black(frame, 8, hl0), "settings highlight corner is rounded");
    int settings_foot = count_black_rect(frame, 16, draw_footer_top(), FB_WIDTH - 16, FB_HEIGHT);
    expect_true(settings_foot > 16, "settings footer hints are present");
    expect_true(hl1 < draw_footer_top(), "settings items stay above the hint band");

    /* Page cue only when there is more than one page; title stays readable. */
    char page_items_store[12][16];
    const char *page_items[12];
    for (int i = 0; i < 12; ++i) {
        snprintf(page_items_store[i], sizeof page_items_store[i], "Book %02d.txt", i + 1);
        page_items[i] = page_items_store[i];
    }
    draw_menu(frame, "Books", NULL, page_items, 12, 6, DRAW_HINT_MENU_1, DRAW_HINT_MENU_2);
    int title_ink = count_black_rect(frame, 70, 0, 130, 28);
    int page_ink = count_black_rect(frame, 150, 0, 196, 28);
    expect_true(title_ink > 8, "paginated menu keeps the title");
    expect_true(page_ink > 4, "paginated menu shows a page cue");
}

static int mask_bits_outside(const uint8_t *black, const uint8_t *white, const draw_rect_t *keep)
{
    int n = 0;
    for (int y = 0; y < FB_HEIGHT; ++y) {
        for (int x = 0; x < FB_WIDTH; ++x) {
            if (!draw_rect_empty(keep) &&
                x >= keep->x0 && x < keep->x1 && y >= keep->y0 && y < keep->y1) {
                continue;
            }
            int i = y * FB_STRIDE + x / 8;
            uint8_t bit = (uint8_t)(0x80u >> (x % 8));
            if (((black[i] | white[i]) & bit) != 0) n++;
        }
    }
    return n;
}

static unsigned drain_to_hold(drive_counters_t *c, const uint8_t *target,
                              uint8_t *black, uint8_t *white)
{
    unsigned last = drive_counters_prepare(c, target, 8, 8, black, white);
    int scans = 0;
    while (last > 0 && scans < 16) {
        drive_counters_complete(c, black, white);
        last = drive_counters_prepare(c, target, 8, 8, black, white);
        scans++;
    }
    return last;
}

static void test_splash_and_partial(void)
{
    uint8_t frame[FB_BYTES];
    draw_splash(frame);

    const char *dir = getenv("INKSPEED_PREVIEW_DIR");
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/splash.pbm", dir);
        write_pbm(path, frame);
    }

    int min_x, min_y, max_x, max_y, count;
    ink_bounds(frame, &min_x, &min_y, &max_x, &max_y, &count);
    expect_true(count > 1000, "splash logo has ink");
    /* v1.0.4 wordmark: centred inkSpeed + .RSVP | Reader, not a full-panel border. */
    expect_true(min_x < 30 && max_x > 170, "splash logo spans the panel width");
    expect_true(min_y > 40 && min_y < 80, "splash logo sits in the vertical middle");
    expect_true(max_y > 120 && max_y < 160, "splash logo stays above the lower margin");

    draw_sleep(frame);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/sleep.pbm", dir);
        write_pbm(path, frame);
    }
    int smin_x, smin_y, smax_x, smax_y, scount;
    ink_bounds(frame, &smin_x, &smin_y, &smax_x, &smax_y, &scount);
    expect_true(scount > 1000, "sleep logo has ink");
    expect_true(smin_x < 30 && smax_x > 170, "sleep logo spans the panel width");
    expect_true(smin_y > 40 && smin_y < 80, "sleep logo sits in the vertical middle");
    expect_true(smax_y > 120 && smax_y < 160, "sleep logo stays above the lower margin");
    int mid_ink = count_black_rect(frame, 16, 40, FB_WIDTH - 16, 155);
    expect_true(mid_ink > 200, "sleep logo has a centre mark");

    const char *excerpt = "The quick brown fox jumps over the lazy dog.";
    draw_paused(frame, excerpt, strlen(excerpt), 4, 9, 42, 300);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/paused.pbm", dir);
        write_pbm(path, frame);
    }
    int pmin_x, pmin_y, pmax_x, pmax_y, pcount;
    ink_bounds(frame, &pmin_x, &pmin_y, &pmax_x, &pmax_y, &pcount);
    expect_true(pcount > 80, "paused view has text");
    expect_true(pmin_y < 30, "paused header is near the top");
    expect_true(pmax_y > 170, "paused footer is near the bottom");
    int pause_gap = count_black_rect(frame, 20, 0, FB_WIDTH - 20, 4);
    expect_true(pause_gap == 0, "paused header is padded from the top edge");
    int pause_foot = count_black_rect(frame, 16, draw_footer_top(), FB_WIDTH - 16, FB_HEIGHT);
    expect_true(pause_foot > 20, "paused view has two-line hints");
    int pause_mid = count_black_rect(frame, 16, draw_footer_top() - 10, FB_WIDTH - 16, draw_footer_top());
    expect_true(pause_mid < 12, "paused excerpt does not collide with hints");

    draw_play_word(frame, "Reading", FONT_LARGE, DRAW_ALIGN_CENTER, NULL, NULL);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/playing.pbm", dir);
        write_pbm(path, frame);
    }
    int play_foot = count_black_rect(frame, 16, draw_footer_top(), FB_WIDTH - 16, FB_HEIGHT);
    expect_true(play_foot > 20, "playing view paints persistent hints");
    draw_rect_t prev_play = draw_play_word_bounds("Reading", FONT_LARGE, DRAW_ALIGN_CENTER);
    draw_play_word_update(frame, "Now", FONT_LARGE, DRAW_ALIGN_CENTER, &prev_play, NULL, NULL, NULL);
    int play_foot2 = count_black_rect(frame, 16, draw_footer_top(), FB_WIDTH - 16, FB_HEIGHT);
    expect_eq_u((unsigned)play_foot2, (unsigned)play_foot, "word update leaves play hints untouched");

    draw_message(frame, "Finished", "Read in 4:20", NULL, DRAW_HINT_DONE);
    if (dir && dir[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/finished.pbm", dir);
        write_pbm(path, frame);
    }
    int fmin_x, fmin_y, fmax_x, fmax_y, fcount;
    ink_bounds(frame, &fmin_x, &fmin_y, &fmax_x, &fmax_y, &fcount);
    expect_true(fcount > 40, "finished screen has ink");
    expect_true(fmin_y < 80, "Finished title sits in the upper half");
    expect_true(fmax_y > 170, "Any button: Home sits near the bottom");
    int fin_time = count_black_rect(frame, 24, 50, FB_WIDTH - 24, 120);
    expect_true(fin_time > 10, "finished screen shows the run time");
    int fin_gap = count_black_rect(frame, 24, 120, FB_WIDTH - 24, draw_footer_top() - 2);
    expect_true(fin_gap < 8, "finished time does not run into the hint band");

    expect_true(font_measure(FONT_SMALL, DRAW_HINT_MENU_1) <= FB_WIDTH - 16, "menu hint 1 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_MENU_2) <= FB_WIDTH - 16, "menu hint 2 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_SETTINGS_1) <= FB_WIDTH - 16, "settings hint 1 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_SETTINGS_2) <= FB_WIDTH - 16, "settings hint 2 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_PAUSE_1) <= FB_WIDTH - 16, "pause hint 1 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_PAUSE_2) <= FB_WIDTH - 16, "pause hint 2 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_PLAY_1) <= FB_WIDTH - 16, "play hint 1 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_PLAY_2) <= FB_WIDTH - 16, "play hint 2 fits");
    expect_true(font_measure(FONT_SMALL, DRAW_HINT_DONE) <= FB_WIDTH - 16, "finished hint fits");

    /* Dirty bounds: long word must not sit inside a short word's box. */
    draw_rect_t long_b = draw_play_word_bounds("extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER);
    draw_rect_t short_b = draw_play_word_bounds("a", FONT_LARGE, DRAW_ALIGN_CENTER);
    expect_true(!draw_rect_empty(&long_b), "long word has bounds");
    expect_true(!draw_rect_empty(&short_b), "short word has bounds");
    expect_true(long_b.x1 - long_b.x0 > short_b.x1 - short_b.x0,
                "long word box is wider than short word box");
    draw_rect_t uni = long_b;
    draw_rect_union(&uni, &short_b);
    expect_true(uni.x0 <= long_b.x0 && uni.x1 >= long_b.x1, "union covers long x");
    expect_true(uni.y0 <= long_b.y0 && uni.y1 >= long_b.y1, "union covers long y");

    draw_rect_t orp = draw_play_word_bounds("Hi", FONT_MEDIUM, DRAW_ALIGN_ORP);
    expect_true(orp.y0 <= 21, "ORP bounds include top focus marks");
    expect_true(orp.y1 <= draw_footer_top(), "ORP bottom marks stay above hints");
    expect_true(orp.y1 >= draw_footer_top() - 8, "ORP bounds include bottom focus marks");
    draw_rect_t word_box = draw_play_word_bounds("extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER);
    expect_true(word_box.y1 <= draw_footer_top(), "word box stays above the hint band");

    /* Identical frames: no pixels to drive. */
    uint8_t a[FB_BYTES], b[FB_BYTES];
    draw_play_word(a, "Same", FONT_MEDIUM, DRAW_ALIGN_CENTER, NULL, NULL);
    draw_play_word(b, "Same", FONT_MEDIUM, DRAW_ALIGN_CENTER, NULL, NULL);
    expect_true(memcmp(a, b, FB_BYTES) == 0, "identical rasterization matches");
    drive_counters_t counters;
    uint8_t black[FB_BYTES], white[FB_BYTES];
    drive_counters_seed(&counters, a);
    unsigned driven = drive_counters_prepare(&counters, b, 8, 8, black, white);
    expect_eq_u(driven, 0, "identical frame is a no-op");
    expect_eq_u(counters.pending_pixels, 0, "identical frame leaves nothing pending");

    /* long -> short via dirty update: leftover ink gone, outside chrome stays. */
    uint8_t play[FB_BYTES];
    fb_clear(play, true);
    draw_rect_t dirty = {0};
    draw_play_word_update(play, "extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER,
                          NULL, &dirty, NULL, NULL);
    expect_true(count_black_rect(play, 2, 2, 6, 6) == 0, "corner starts white");
    fb_pixel_black(play, 3, 3);
    fb_pixel_black(play, 4, 3);
    draw_rect_t prev = dirty;
    draw_play_word_update(play, "a", FONT_LARGE, DRAW_ALIGN_CENTER,
                          &prev, &dirty, NULL, NULL);
    expect_true(fb_black(play, 3, 3) && fb_black(play, 4, 3),
                "chrome outside the word band is unchanged");

    uint8_t ref[FB_BYTES];
    draw_play_word(ref, "a", FONT_LARGE, DRAW_ALIGN_CENTER, NULL, NULL);
    fb_pixel_black(ref, 3, 3);
    fb_pixel_black(ref, 4, 3);
    expect_true(memcmp(play, ref, FB_BYTES) == 0,
                "long-to-short matches a fresh short word plus chrome");

    /* A reference long-word frame compared against the short update: pixels
     * that belonged only to the long word must now be white. */
    uint8_t long_frame[FB_BYTES];
    draw_play_word(long_frame, "extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER, NULL, NULL);
    int leftover = 0;
    for (int y = 0; y < FB_HEIGHT; ++y) {
        for (int x = 0; x < FB_WIDTH; ++x) {
            if (x == 3 && y == 3) continue;
            if (x == 4 && y == 3) continue;
            if (fb_black(long_frame, x, y) && !fb_black(ref, x, y) && fb_black(play, x, y)) {
                leftover++;
            }
        }
    }
    expect_eq_u((unsigned)leftover, 0, "no leftover long-word ink after short update");

    /* Outside the old+new union, mixed masks stay HOLD (both planes 0). */
    uint8_t settled[FB_BYTES];
    draw_play_word(settled, "extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER, NULL, NULL);
    drive_counters_seed(&counters, settled);
    expect_eq_u(drain_to_hold(&counters, settled, black, white), 0, "first word drains to HOLD");

    uint8_t next[FB_BYTES];
    memcpy(next, settled, FB_BYTES);
    prev = draw_play_word_bounds("extraordinary", FONT_LARGE, DRAW_ALIGN_CENTER);
    draw_play_word_update(next, "a", FONT_LARGE, DRAW_ALIGN_CENTER, &prev, &dirty, NULL, NULL);
    draw_rect_t keep = prev;
    draw_rect_union(&keep, &dirty);
    driven = drive_counters_prepare(&counters, next, 8, 8, black, white);
    expect_true(driven > 0, "word change drives some pixels");
    expect_eq_u((unsigned)mask_bits_outside(black, white, &keep), 0,
                "pixels outside the old+new union stay HOLD");

    /* Drain the change, then an all-HOLD ending: same target again. */
    expect_eq_u(drain_to_hold(&counters, next, black, white), 0, "change drains");
    driven = drive_counters_prepare(&counters, next, 8, 8, black, white);
    expect_eq_u(driven, 0, "settled target is all HOLD");
    int leftover_mask = 0;
    for (int i = 0; i < FB_BYTES; ++i) leftover_mask += (black[i] | white[i]) != 0;
    expect_eq_u((unsigned)leftover_mask, 0, "drained masks are empty");
}

static void test_round_rects(void)
{
    uint8_t frame[FB_BYTES];
    fb_clear(frame, true);
    fb_fill_round_rect(frame, 20, 20, 80, 80, 12, false);
    expect_true(!fb_black(frame, 20, 20), "filled round-rect corner stays white");
    expect_true(!fb_black(frame, 79, 20), "filled round-rect opposite corner stays white");
    expect_true(fb_black(frame, 50, 50), "filled round-rect center is ink");
    expect_true(fb_black(frame, 20, 50), "filled round-rect left midpoint is ink");
    expect_true(fb_black(frame, 50, 20), "filled round-rect top midpoint is ink");

    fb_clear(frame, true);
    fb_fill_round_rect(frame, -30, -30, 24, 24, 20, false);
    expect_true(!fb_black(frame, 0, 0), "clipped round-rect keeps the visible corner rounded");
    expect_true(fb_black(frame, 16, 16), "clipped round-rect still fills the interior");

    fb_clear(frame, true);
    fb_fill_round_rect(frame, 10, 10, 11, 12, 40, false);
    expect_true(fb_black(frame, 10, 10) && fb_black(frame, 10, 11),
                "1-wide rect survives a huge radius");
    unsigned before = (unsigned)count_black_rect(frame, 0, 0, FB_WIDTH, FB_HEIGHT);
    fb_fill_round_rect(frame, 40, 40, 40, 90, 8, false);
    fb_invert_round_rect(frame, 50, 90, 50, 40, 8);
    unsigned after = (unsigned)count_black_rect(frame, 0, 0, FB_WIDTH, FB_HEIGHT);
    expect_eq_u(after, before, "empty round-rect ranges are no-ops");

    fb_clear(frame, true);
    fb_stroke_round_rect(frame, 30, 30, 90, 70, 8);
    expect_true(fb_black(frame, 60, 30), "stroked round-rect top edge is ink");
    expect_true(!fb_black(frame, 60, 50), "stroked round-rect interior stays paper");
    expect_true(!fb_black(frame, 30, 30), "stroked round-rect corner is open");
}

static void test_upload_names(void)
{
    char raw[160], name[96];
    strcpy(raw, "Pride%20and%20Prejudice.txt");
    upload_url_decode(raw);
    expect_eq_s(raw, "Pride and Prejudice.txt", "url decode %20");
    expect_true(upload_safe_book_name(raw, name, sizeof name), "spaced name accepted");
    expect_eq_s(name, "Pride and Prejudice.txt", "spaced name kept");

    strcpy(raw, "Caf%C3%A9.txt");
    upload_url_decode(raw);
    expect_eq_s(raw, "Caf\xC3\xA9.txt", "url decode UTF-8 %C3%A9");
    expect_true(upload_safe_book_name(raw, name, sizeof name), "UTF-8 name accepted");
    expect_eq_s(name, "Caf\xC3\xA9.txt", "UTF-8 name kept");

    strcpy(raw, "a+b%2Bc%zz");
    upload_url_decode(raw);
    expect_eq_s(raw, "a b+c%zz", "plus, %2B and bad escape");

    strcpy(raw, "..%2F..%2Fsecret.txt");
    upload_url_decode(raw);
    expect_true(upload_safe_book_name(raw, name, sizeof name), "traversal reduced to basename");
    expect_eq_s(name, "secret.txt", "decoded slash cannot escape /books");

    strcpy(raw, "%2E%2E");
    upload_url_decode(raw);
    expect_true(!upload_safe_book_name(raw, name, sizeof name), "decoded .. rejected");

    strcpy(raw, "dir%5Cnotes");
    upload_url_decode(raw);
    expect_true(upload_safe_book_name(raw, name, sizeof name), "backslash basename");
    expect_eq_s(name, "notes.txt", "backslash stripped, .txt forced");
}

int main(void)
{
    test_session_time();
    test_sched_unit();
    test_sched_demo();
    test_upload_names();
    test_tokenizer();
    test_timing();
    test_bookmark();
    test_fit();
    test_settings_and_draw();
    test_font_baseline();
    test_round_rects();
    test_menu_alignment();
    test_splash_and_partial();
    if (g_fails) {
        fprintf(stderr, "%d test(s) failed\n", g_fails);
        return 1;
    }
    printf("All host tests passed\n");
    return 0;
}
