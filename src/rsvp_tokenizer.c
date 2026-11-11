#include "rsvp_tokenizer.h"
#include "utf8.h"
#include <limits.h>
#include <string.h>

int rsvp_file_read(void *ctx, void *buf, int n)
{
    FILE *f = ctx;
    if (!f || n <= 0) return 0;
    size_t got = fread(buf, 1, (size_t)n, f);
    if (got == 0 && ferror(f)) return -1;
    return (int)got;
}

int rsvp_file_seek(void *ctx, uint64_t offset)
{
    FILE *f = ctx;
    if (!f) return -1;
    if (offset > (uint64_t)LONG_MAX) return -1;
    return fseek(f, (long)offset, SEEK_SET) == 0 ? 0 : -1;
}

int rsvp_mem_read(void *ctx, void *buf, int n)
{
    rsvp_mem_src_t *m = ctx;
    if (!m || n <= 0) return 0;
    if (m->pos >= m->len) return 0;
    size_t left = m->len - m->pos;
    if ((size_t)n > left) n = (int)left;
    memcpy(buf, m->data + m->pos, (size_t)n);
    m->pos += (size_t)n;
    return n;
}

int rsvp_mem_seek(void *ctx, uint64_t offset)
{
    rsvp_mem_src_t *m = ctx;
    if (!m) return -1;
    if (offset > m->len) return -1;
    m->pos = (size_t)offset;
    return 0;
}

void rsvp_stream_init(rsvp_stream_t *s, rsvp_read_fn read, rsvp_seek_fn seek, void *ctx)
{
    memset(s, 0, sizeof *s);
    s->read = read;
    s->seek = seek;
    s->ctx = ctx;
}

void rsvp_stream_init_file(rsvp_stream_t *s, FILE *f)
{
    rsvp_stream_init(s, rsvp_file_read, rsvp_file_seek, f);
}

void rsvp_stream_init_mem(rsvp_stream_t *s, rsvp_mem_src_t *mem, const void *data, size_t len)
{
    mem->data = data;
    mem->len = len;
    mem->pos = 0;
    rsvp_stream_init(s, rsvp_mem_read, rsvp_mem_seek, mem);
}

static uint64_t cursor(const rsvp_stream_t *s)
{
    return s->offset_bias + s->buf_base + (uint64_t)s->buf_off;
}

uint64_t rsvp_stream_tell(const rsvp_stream_t *s)
{
    if (s->leftover[0]) return s->leftover_offset;
    return cursor(s);
}

int rsvp_stream_seek(rsvp_stream_t *s, uint64_t offset)
{
    if (s->seek && s->seek(s->ctx, offset) != 0) {
        s->error = 1;
        return -1;
    }
    s->file_pos = offset;
    s->buf_base = offset;
    s->buf_len = 0;
    s->buf_off = 0;
    s->eof = false;
    s->pending_paragraph = false;
    s->leftover[0] = 0;
    s->error = 0;
    if (offset != 0) s->skipped_bom = true;
    return 0;
}

static int refill(rsvp_stream_t *s)
{
    if (s->buf_off < s->buf_len || s->eof) return 0;
    if (!s->read) {
        s->eof = true;
        return 0;
    }
    s->buf_base = s->file_pos;
    int n = s->read(s->ctx, s->buf, RSVP_STREAM_BUF);
    if (n < 0) {
        s->error = 1;
        s->eof = true;
        return -1;
    }
    if (n == 0) {
        s->eof = true;
        s->buf_len = 0;
        s->buf_off = 0;
        return 0;
    }
    s->buf_len = n;
    s->buf_off = 0;
    s->file_pos += (uint64_t)n;
    if (!s->skipped_bom && s->buf_base == 0 && n >= 3 &&
        (unsigned char)s->buf[0] == 0xEF &&
        (unsigned char)s->buf[1] == 0xBB &&
        (unsigned char)s->buf[2] == 0xBF) {
        s->buf_off = 3;
        s->skipped_bom = true;
    } else {
        s->skipped_bom = true;
    }
    return 0;
}

static int peek_cp(rsvp_stream_t *s, uint32_t *cp, int *used)
{
    if (refill(s) < 0) return -1;
    if (s->buf_off >= s->buf_len) return 0;
    *used = utf8_next(s->buf + s->buf_off, (size_t)(s->buf_len - s->buf_off), cp);
    if (*used <= 0) return 0;
    /* If the buffer ends mid-sequence, read more by shifting remainder. */
    unsigned char b = (unsigned char)s->buf[s->buf_off];
    int need = 1;
    if ((b & 0xE0u) == 0xC0u) need = 2;
    else if ((b & 0xF0u) == 0xE0u) need = 3;
    else if ((b & 0xF8u) == 0xF0u) need = 4;
    int have = s->buf_len - s->buf_off;
    if (have < need && !s->eof) {
        memmove(s->buf, s->buf + s->buf_off, (size_t)have);
        s->buf_base += (uint64_t)s->buf_off;
        s->buf_len = have;
        s->buf_off = 0;
        if (!s->read) {
            s->eof = true;
        } else {
            int extra = s->read(s->ctx, s->buf + have, RSVP_STREAM_BUF - have);
            if (extra < 0) {
                s->error = 1;
                return -1;
            }
            if (extra == 0) s->eof = true;
            else {
                s->buf_len += extra;
                s->file_pos += (uint64_t)extra;
            }
        }
        if (s->buf_off >= s->buf_len) return 0;
        *used = utf8_next(s->buf + s->buf_off, (size_t)(s->buf_len - s->buf_off), cp);
        if (*used <= 0) return 0;
    }
    return 1;
}

static int consume(rsvp_stream_t *s, int used)
{
    if (s->buf_off + used > s->buf_len) return -1;
    s->buf_off += used;
    return 0;
}

static bool is_ws(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
           cp == '\f' || cp == '\v' || cp == 0x00A0u;
}

static bool is_wrapper(uint32_t cp)
{
    return cp == '"' || cp == '\'' || cp == ')' || cp == ']' || cp == '}' ||
           cp == 0x2019u || cp == 0x201Du || cp == 0x00BBu;
}

static void classify(rsvp_token_t *tok)
{
    tok->flags &= (uint16_t)~(RSVP_F_SENTENCE_END | RSVP_F_CLAUSE | RSVP_F_LONG);
    if (tok->codepoints >= 12) tok->flags |= RSVP_F_LONG;
    if (!tok->byte_length) return;
    /* Walk codepoints, peel trailing wrappers, inspect the last remaining. */
    uint32_t cps[RSVP_TOKEN_MAX];
    unsigned n = 0;
    size_t i = 0;
    while (i < tok->byte_length && n < RSVP_TOKEN_MAX) {
        uint32_t cp;
        int used = utf8_next(tok->text + i, tok->byte_length - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        cps[n++] = cp;
    }
    int last = (int)n - 1;
    while (last >= 0 && is_wrapper(cps[last])) last--;
    if (last < 0) return;
    uint32_t end = cps[last];
    if (end == '.' || end == '!' || end == '?' || end == 0x2026u) {
        tok->flags |= RSVP_F_SENTENCE_END;
        return;
    }
    if (last >= 2 && cps[last] == '.' && cps[last - 1] == '.' && cps[last - 2] == '.') {
        tok->flags |= RSVP_F_SENTENCE_END;
        return;
    }
    if (end == ',' || end == ';' || end == ':' || end == 0x2013u || end == 0x2014u) {
        tok->flags |= RSVP_F_CLAUSE;
    }
}

void rsvp_classify_token(rsvp_token_t *tok)
{
    classify(tok);
}

bool rsvp_token_is_sentence_initial(const rsvp_token_t *prev, const rsvp_token_t *cur)
{
    if (!cur) return false;
    if (cur->flags & RSVP_F_PARAGRAPH) return true;
    if (!prev) return true;
    return (prev->flags & RSVP_F_SENTENCE_END) != 0;
}

static void emit_from_text(rsvp_token_t *tok, const char *src, size_t n,
                           uint64_t offset, uint16_t extra_flags)
{
    memset(tok, 0, sizeof *tok);
    if (n > RSVP_TOKEN_MAX) n = RSVP_TOKEN_MAX;
    memcpy(tok->text, src, n);
    tok->text[n] = 0;
    tok->byte_offset = offset;
    tok->byte_length = (uint16_t)n;
    tok->codepoints = (uint16_t)utf8_count_codepoints(tok->text, n);
    tok->flags = extra_flags;
    classify(tok);
}

bool rsvp_next_token(rsvp_stream_t *s, rsvp_token_t *tok)
{
    if (!s || !tok) return false;
    if (s->leftover[0]) {
        size_t n = strlen(s->leftover);
        emit_from_text(tok, s->leftover, n, s->leftover_offset,
                       (uint16_t)(s->leftover_flags | RSVP_F_CONTINUED));
        s->leftover[0] = 0;
        return true;
    }

    unsigned newlines = 0;
    for (;;) {
        uint32_t cp;
        int used = 0;
        int st = peek_cp(s, &cp, &used);
        if (st < 0) return false;
        if (st == 0) return false;
        if (!is_ws(cp)) break;
        if (cp == '\r') {
            newlines++;
            consume(s, used);
            uint32_t next;
            int nu = 0;
            if (peek_cp(s, &next, &nu) > 0 && next == '\n') consume(s, nu);
            continue;
        }
        if (cp == '\n') newlines++;
        consume(s, used);
    }
    if (newlines >= 2) s->pending_paragraph = true;

    uint64_t start = cursor(s);
    char text[RSVP_TOKEN_MAX + 8];
    size_t n = 0;
    for (;;) {
        uint32_t cp;
        int used = 0;
        int st = peek_cp(s, &cp, &used);
        if (st < 0) return false;
        if (st == 0) break;
        if (is_ws(cp)) break;
        if (n + (size_t)used > RSVP_TOKEN_MAX) {
            /* Keep a valid UTF-8 prefix and stash the rest of this token. */
            char rest[RSVP_TOKEN_MAX + 8];
            size_t rn = 0;
            rest[0] = 0;
            /* First store what we cannot fit from this codepoint if used itself > remaining? */
            size_t room = RSVP_TOKEN_MAX - n;
            if (room == 0 || (size_t)used > room) {
                /* Current codepoint does not fit: leftover starts here. */
            } else {
                memcpy(text + n, s->buf + s->buf_off, (size_t)used);
                n += (size_t)used;
                consume(s, used);
            }
            while (rn < RSVP_TOKEN_MAX) {
                uint32_t cp2;
                int u2 = 0;
                int st2 = peek_cp(s, &cp2, &u2);
                if (st2 <= 0) break;
                if (is_ws(cp2)) break;
                if (rn + (size_t)u2 > RSVP_TOKEN_MAX) break;
                memcpy(rest + rn, s->buf + s->buf_off, (size_t)u2);
                rn += (size_t)u2;
                consume(s, u2);
            }
            rest[rn] = 0;
            memcpy(s->leftover, rest, rn + 1);
            s->leftover_offset = start + n;
            s->leftover_flags = 0;
            break;
        }
        memcpy(text + n, s->buf + s->buf_off, (size_t)used);
        n += (size_t)used;
        consume(s, used);
    }
    if (n == 0) return false;
    text[n] = 0;
    uint16_t extra = 0;
    if (s->pending_paragraph) {
        extra |= RSVP_F_PARAGRAPH;
        s->pending_paragraph = false;
    }
    emit_from_text(tok, text, n, start, extra);
    return true;
}
