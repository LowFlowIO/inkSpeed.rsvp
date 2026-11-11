#include "utf8.h"

int utf8_next(const char *s, size_t nbytes, uint32_t *cp)
{
    if (!s || !cp || nbytes == 0) return 0;
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0u) == 0xC0u && nbytes >= 2 && (p[1] & 0xC0u) == 0x80u) {
        uint32_t v = ((uint32_t)(c & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
        if (v >= 0x80u) {
            *cp = v;
            return 2;
        }
    } else if ((c & 0xF0u) == 0xE0u && nbytes >= 3 &&
               (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u) {
        uint32_t v = ((uint32_t)(c & 0x0Fu) << 12) |
                     ((uint32_t)(p[1] & 0x3Fu) << 6) |
                     (uint32_t)(p[2] & 0x3Fu);
        if (v >= 0x800u && (v < 0xD800u || v > 0xDFFFu)) {
            *cp = v;
            return 3;
        }
    } else if ((c & 0xF8u) == 0xF0u && nbytes >= 4 &&
               (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u &&
               (p[3] & 0xC0u) == 0x80u) {
        uint32_t v = ((uint32_t)(c & 0x07u) << 18) |
                     ((uint32_t)(p[1] & 0x3Fu) << 12) |
                     ((uint32_t)(p[2] & 0x3Fu) << 6) |
                     (uint32_t)(p[3] & 0x3Fu);
        if (v >= 0x10000u && v <= 0x10FFFFu) {
            *cp = v;
            return 4;
        }
    }
    *cp = 0xFFFDu;
    return 1;
}

unsigned utf8_count_codepoints(const char *s, size_t nbytes)
{
    unsigned n = 0;
    size_t i = 0;
    while (i < nbytes) {
        uint32_t cp;
        int used = utf8_next(s + i, nbytes - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        n++;
    }
    return n;
}

size_t utf8_prefix_bytes(const char *s, size_t nbytes, unsigned n_codepoints)
{
    size_t i = 0;
    unsigned seen = 0;
    while (i < nbytes && seen < n_codepoints) {
        uint32_t cp;
        int used = utf8_next(s + i, nbytes - i, &cp);
        if (used <= 0) break;
        i += (size_t)used;
        seen++;
    }
    return i;
}

bool utf8_is_boundary(unsigned char byte)
{
    return (byte & 0xC0u) != 0x80u;
}
