#pragma once
/* Upload portal name handling. Pure C (no ESP-IDF), so it is host-tested. */
#include <stdbool.h>
#include <stddef.h>

/* In-place %XX and '+' decoding of a URL query value. */
void upload_url_decode(char *s);

/* Basename only, no controls or path separators. Forces a .txt suffix.
 * Rejects empty, "." and "..". Call on the DECODED name. */
bool upload_safe_book_name(const char *in, char *out, size_t out_sz);
