#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode the next UTF-8 codepoint. Returns bytes consumed (1..4), or 0 if nbytes
 * is 0. Invalid sequences become U+FFFD and consume one byte. */
int utf8_next(const char *s, size_t nbytes, uint32_t *cp);
unsigned utf8_count_codepoints(const char *s, size_t nbytes);
/* Byte length of the first n codepoints, never splitting a sequence. */
size_t utf8_prefix_bytes(const char *s, size_t nbytes, unsigned n_codepoints);
bool utf8_is_boundary(unsigned char byte);
