#include "rsvp_bookmark.h"
#include <string.h>

static const uint32_t kCrcTable[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

uint32_t rsvp_crc32(const void *data, size_t n)
{
    const unsigned char *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        crc = kCrcTable[crc & 0x0Fu] ^ (crc >> 4);
        crc = kCrcTable[crc & 0x0Fu] ^ (crc >> 4);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

bool rsvp_bookmark_encode(uint8_t out[RSVP_BMK_SIZE], const rsvp_bookmark_t *b)
{
    if (!out || !b) return false;
    memset(out, 0, RSVP_BMK_SIZE);
    wr32(out + 0, RSVP_BMK_MAGIC);
    wr16(out + 4, b->version ? b->version : RSVP_BMK_VERSION);
    wr16(out + 6, b->flags);
    wr64(out + 8, b->file_size);
    wr64(out + 16, b->byte_offset);
    wr32(out + 24, b->path_crc32);
    wr32(out + 28, rsvp_crc32(out, 28));
    wr32(out + 32, 0);
    return true;
}

bool rsvp_bookmark_decode(const uint8_t *in, size_t n, rsvp_bookmark_t *b)
{
    if (!in || !b || n < RSVP_BMK_SIZE) return false;
    if (rd32(in + 0) != RSVP_BMK_MAGIC) return false;
    uint16_t version = rd16(in + 4);
    if (version != RSVP_BMK_VERSION) return false;
    uint32_t expect = rsvp_crc32(in, 28);
    if (rd32(in + 28) != expect) return false;
    memset(b, 0, sizeof *b);
    b->version = version;
    b->flags = rd16(in + 6);
    b->file_size = rd64(in + 8);
    b->byte_offset = rd64(in + 16);
    b->path_crc32 = rd32(in + 24);
    if (b->byte_offset > b->file_size) return false;
    return true;
}

bool rsvp_bookmark_is_complete(const rsvp_bookmark_t *b)
{
    if (!b) return false;
    if (b->file_size == 0) return false;
    if (b->flags & RSVP_BMK_F_COMPLETED) return true;
    return b->byte_offset >= b->file_size;
}

void rsvp_bookmark_mark_complete(rsvp_bookmark_t *b)
{
    if (!b) return;
    b->flags = (uint16_t)(b->flags | RSVP_BMK_F_COMPLETED);
    b->byte_offset = 0;
}

uint64_t rsvp_bookmark_resume_offset(const rsvp_bookmark_t *b)
{
    if (!b || rsvp_bookmark_is_complete(b)) return 0;
    return b->byte_offset;
}
