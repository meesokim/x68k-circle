#include "hangul16.h"
#include "miniz.h"

#include <string.h>

#include "hangul16_blob.inc"

static uint8_t g_raw[11266 * 32 + 126 * 178 * 2];
static int g_ready = 0;

static void HangulEnsure(void)
{
    if (g_ready) return;
    g_ready = 1;

    static_assert(kHangulGlyphs == 11266, "hangul glyph count");
    static_assert(kCp949Leads == 126 && kCp949Trails == 178, "cp949 map size");

    const unsigned bits_bytes = kHangulGlyphs * 32;
    const unsigned map_bytes = kCp949Leads * kCp949Trails * 2;
    const unsigned total = bits_bytes + map_bytes;
    mz_ulong dest = total;
    if (mz_uncompress(g_raw, &dest, kHangulZlib, kHangulZlibSize) != MZ_OK || dest != total) {
        memset(g_raw, 0, sizeof(g_raw));
    }
}

const uint8_t *HangulGlyph16(uint32_t cp)
{
    HangulEnsure();
    unsigned index;
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        index = cp - 0xAC00;
    } else if (cp >= 0x3131 && cp <= 0x318E) {
        index = 11172 + (cp - 0x3131);
    } else {
        return 0;
    }
    return g_raw + index * 32;
}

static int Cp949TrailIndex(unsigned b)
{
    if (b >= 0x41 && b <= 0x5A) return (int)(b - 0x41);
    if (b >= 0x61 && b <= 0x7A) return 26 + (int)(b - 0x61);
    if (b >= 0x81 && b <= 0xFE) return 52 + (int)(b - 0x81);
    return -1;
}

uint32_t DecodeCp949Pair(unsigned lead, unsigned trail)
{
    HangulEnsure();
    if (lead < 0x81 || lead > 0xFE) return 0;
    int ti = Cp949TrailIndex(trail);
    if (ti < 0) return 0;
    unsigned slot = ((lead - 0x81) * kCp949Trails + (unsigned)ti) * 2;
    const unsigned bits_bytes = kHangulGlyphs * 32;
    const uint8_t *p = g_raw + bits_bytes + slot;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
