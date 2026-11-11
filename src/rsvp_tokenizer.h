#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RSVP_TOKEN_MAX 96
#define RSVP_STREAM_BUF 2048

enum {
    RSVP_F_SENTENCE_END = 1u << 0,
    RSVP_F_CLAUSE       = 1u << 1,
    RSVP_F_PARAGRAPH    = 1u << 2,
    RSVP_F_LONG         = 1u << 3,
    RSVP_F_CONTINUED    = 1u << 4,
};

typedef struct {
    uint64_t byte_offset;
    uint16_t byte_length;
    uint16_t flags;
    uint16_t codepoints;
    char text[RSVP_TOKEN_MAX + 1];
} rsvp_token_t;

typedef int (*rsvp_read_fn)(void *ctx, void *buf, int n);
typedef int (*rsvp_seek_fn)(void *ctx, uint64_t offset);

typedef struct {
    rsvp_read_fn read;
    rsvp_seek_fn seek;
    void *ctx;
    uint64_t file_pos;   /* next unread source byte */
    uint64_t buf_base;   /* file offset of buf[0] */
    uint64_t offset_bias;
    char buf[RSVP_STREAM_BUF];
    int buf_len;
    int buf_off;
    bool eof;
    bool pending_paragraph;
    bool skipped_bom;
    int error;
    char leftover[RSVP_TOKEN_MAX + 1];
    uint64_t leftover_offset;
    uint16_t leftover_flags;
} rsvp_stream_t;

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
} rsvp_mem_src_t;

void rsvp_stream_init(rsvp_stream_t *s, rsvp_read_fn read, rsvp_seek_fn seek, void *ctx);
void rsvp_stream_init_file(rsvp_stream_t *s, FILE *f);
void rsvp_stream_init_mem(rsvp_stream_t *s, rsvp_mem_src_t *mem, const void *data, size_t len);
int rsvp_stream_seek(rsvp_stream_t *s, uint64_t offset);
uint64_t rsvp_stream_tell(const rsvp_stream_t *s);
bool rsvp_next_token(rsvp_stream_t *s, rsvp_token_t *tok);

int rsvp_file_read(void *ctx, void *buf, int n);
int rsvp_file_seek(void *ctx, uint64_t offset);
int rsvp_mem_read(void *ctx, void *buf, int n);
int rsvp_mem_seek(void *ctx, uint64_t offset);

void rsvp_classify_token(rsvp_token_t *tok);
bool rsvp_token_is_sentence_initial(const rsvp_token_t *prev, const rsvp_token_t *cur);
