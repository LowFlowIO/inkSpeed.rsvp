#define _POSIX_C_SOURCE 200809L /* strcasecmp on strict C11 host builds */
#include "upload_name.h"
#include <string.h>
#include <strings.h>

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void upload_url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && hex_nibble(r[1]) >= 0 && hex_nibble(r[2]) >= 0) {
            *w++ = (char)((hex_nibble(r[1]) << 4) | hex_nibble(r[2]));
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

bool upload_safe_book_name(const char *in, char *out, size_t out_sz)
{
    if (!in || !out || out_sz < 8) return false;
    const char *base = in;
    for (const char *p = in; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    size_t n = 0;
    for (const char *p = base; *p && n + 5 < out_sz; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;
        out[n++] = (char)c;
    }
    out[n] = 0;
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.')) out[--n] = 0;
    if (!n || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) return false;
    size_t len = strlen(out);
    if (len < 4 || strcasecmp(out + len - 4, ".txt") != 0) {
        if (len + 4 >= out_sz) return false;
        memcpy(out + len, ".txt", 5);
    }
    return true;
}
