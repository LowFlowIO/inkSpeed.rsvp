#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RSVP_BMK_MAGIC 0x4D425049u /* 'IPBM' little-endian */
#define RSVP_BMK_VERSION 1
#define RSVP_BMK_SIZE 36
#define RSVP_BMK_F_COMPLETED 0x0001u

typedef struct {
    uint16_t version;
    uint16_t flags; /* stored in the v1 reserved field at bytes 6-7 */
    uint64_t file_size;
    uint64_t byte_offset;
    uint32_t path_crc32;
} rsvp_bookmark_t;

uint32_t rsvp_crc32(const void *data, size_t n);
bool rsvp_bookmark_encode(uint8_t out[RSVP_BMK_SIZE], const rsvp_bookmark_t *b);
bool rsvp_bookmark_decode(const uint8_t *in, size_t n, rsvp_bookmark_t *b);

/* Completed if the flag is set, or (legacy) the offset is at/after EOF.
 * An empty file is never treated as completed. */
bool rsvp_bookmark_is_complete(const rsvp_bookmark_t *b);
void rsvp_bookmark_mark_complete(rsvp_bookmark_t *b);
uint64_t rsvp_bookmark_resume_offset(const rsvp_bookmark_t *b);
