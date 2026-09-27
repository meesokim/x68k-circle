#include "ttf_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "cp949_map.inc"

static unsigned char *g_ttf = NULL;
static int g_ttf_size = 0;
static stbtt_fontinfo g_info;
static int g_height = 0;
static float g_scale = 0;
static int g_ascent = 0;
static int g_ready = 0;

static uint16_t *g_cp949 = NULL;

static int Cp949TrailIndex(unsigned b)
{
    if (b >= 0x41 && b <= 0x5A) return (int)(b - 0x41);
    if (b >= 0x61 && b <= 0x7A) return 26 + (int)(b - 0x61);
    if (b >= 0x81 && b <= 0xFE) return 52 + (int)(b - 0x81);
    return -1;
}

static void Cp949Ensure(void)
{
    if (g_cp949) return;
    g_cp949 = (uint16_t *)calloc(kCp949MapBytes / 2, sizeof(uint16_t));
    if (!g_cp949) return;
    mz_ulong dest = kCp949MapBytes;
    if (mz_uncompress((unsigned char *)g_cp949, &dest, kCp949Zlib, kCp949ZlibSize) != MZ_OK
        || dest != kCp949MapBytes) {
        free(g_cp949);
        g_cp949 = NULL;
    }
}

static uint32_t Cp949Pair(unsigned lead, unsigned trail)
{
    int ti;
    unsigned slot;
    Cp949Ensure();
    if (!g_cp949 || lead < 0x81 || lead > 0xFE) return 0;
    ti = Cp949TrailIndex(trail);
    if (ti < 0) return 0;
    slot = (lead - 0x81) * 178u + (unsigned)ti;
    return g_cp949[slot];
}

static int Utf8Ok(const unsigned char *s)
{
    while (*s) {
        if (*s < 0x80) {
            s++;
            continue;
        }
        if ((*s & 0xE0) == 0xC0 && s[1]) {
            if ((s[1] & 0xC0) != 0x80 || (*s & 0xFE) == 0xC0) return 0;
            s += 2;
            continue;
        }
        if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            unsigned cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
            s += 3;
            continue;
        }
        if ((*s & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
            unsigned cp = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12)
                        | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
            if (cp < 0x10000 || cp > 0x10FFFF) return 0;
            s += 4;
            continue;
        }
        return 0;
    }
    return 1;
}

/* Returns bytes consumed. *cp is the Unicode scalar. */
static int NextCp(const unsigned char **ps, uint32_t *cp, int utf8)
{
    const unsigned char *s = *ps;
    if (!*s) {
        *cp = 0;
        return 0;
    }
    if (utf8) {
        if (*s < 0x80) {
            *cp = *s;
            *ps = s + 1;
            return 1;
        }
        if ((*s & 0xE0) == 0xC0) {
            *cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            *ps = s + 2;
            return 2;
        }
        if ((*s & 0xF0) == 0xE0) {
            *cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            *ps = s + 3;
            return 3;
        }
        *cp = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *ps = s + 4;
        return 4;
    }
    if (*s < 0x80) {
        *cp = *s;
        *ps = s + 1;
        return 1;
    }
    if (s[1]) {
        uint32_t u = Cp949Pair(s[0], s[1]);
        if (u) {
            *cp = u;
            *ps = s + 2;
            return 2;
        }
        if (Cp949TrailIndex(s[1]) >= 0) {
            *cp = '?';
            *ps = s + 2;
            return 2;
        }
    }
    *cp = '?';
    *ps = s + 1;
    return 1;
}

static uint16_t Blend565(uint16_t fg, uint16_t bg, unsigned alpha)
{
    unsigned inv = 255 - alpha;
    unsigned r = ((((fg >> 11) & 0x1F) * alpha + ((bg >> 11) & 0x1F) * inv) / 255);
    unsigned g = ((((fg >> 5) & 0x3F) * alpha + ((bg >> 5) & 0x3F) * inv) / 255);
    unsigned b = (((fg & 0x1F) * alpha + (bg & 0x1F) * inv) / 255);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int GlyphAdvance(uint32_t cp)
{
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&g_info, (int)cp, &adv, &lsb);
    adv = (int)(adv * g_scale + 0.5f);
    if (adv <= 0) adv = g_height / 2;
    return adv;
}

static int LoadTtfFile(void)
{
    static const char *paths[] = {
        "NanumGothic.ttf",
        "dist_baremetal/NanumGothic.ttf",
        "SD:/NanumGothic.ttf",
        "SD:/keropi/NanumGothic.ttf",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        NULL
    };
    int i;
    for (i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "rb");
        long sz;
        unsigned char *buf;
        if (!fp) continue;
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            continue;
        }
        sz = ftell(fp);
        if (sz <= 0) {
            fclose(fp);
            continue;
        }
        fseek(fp, 0, SEEK_SET);
        buf = (unsigned char *)malloc((size_t)sz);
        if (!buf) {
            fclose(fp);
            return 0;
        }
        if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
            free(buf);
            fclose(fp);
            continue;
        }
        fclose(fp);
        if (!stbtt_InitFont(&g_info, buf, stbtt_GetFontOffsetForIndex(buf, 0))) {
            free(buf);
            continue;
        }
        g_ttf = buf;
        g_ttf_size = (int)sz;
        printf("[ttf] loaded %s (%ld KB)\n", paths[i], sz / 1024);
        return 1;
    }
    printf("[ttf] NanumGothic.ttf not found\n");
    return 0;
}

int TtfEnsure(int pixel_height)
{
    int ascent, descent, linegap;
    if (pixel_height < 8) pixel_height = 16;
    if (!g_ttf && !LoadTtfFile()) return 0;
    if (g_ready && g_height == pixel_height) return 1;
    g_height = pixel_height;
    g_scale = stbtt_ScaleForPixelHeight(&g_info, (float)pixel_height);
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
    g_ascent = (int)(ascent * g_scale + 0.5f);
    g_ready = 1;
    return 1;
}

static void DrawGlyph(uint16_t *buf, int stride, int clip_w, int clip_h,
                      int x, int y, uint32_t cp, uint16_t fg, uint16_t bg, int opaque_bg)
{
    int x0, y0, x1, y1, gw, gh, gx, gy;
    unsigned char stack_bmp[64 * 64];
    unsigned char *bmp;
    int draw_x, draw_y;

    stbtt_GetCodepointBitmapBox(&g_info, (int)cp, g_scale, g_scale, &x0, &y0, &x1, &y1);
    gw = x1 - x0;
    gh = y1 - y0;
    if (gw <= 0 || gh <= 0) return;

    bmp = (gw * gh <= (int)sizeof(stack_bmp)) ? stack_bmp : (unsigned char *)malloc((size_t)gw * (size_t)gh);
    if (!bmp) return;
    stbtt_MakeCodepointBitmap(&g_info, bmp, gw, gh, gw, g_scale, g_scale, (int)cp);

    draw_x = x + x0;
    draw_y = y + g_ascent + y0;
    for (gy = 0; gy < gh; gy++) {
        int py = draw_y + gy;
        if (py < 0 || py >= clip_h) continue;
        for (gx = 0; gx < gw; gx++) {
            int px = draw_x + gx;
            unsigned char a;
            uint16_t *dst;
            if (px < 0 || px >= clip_w) continue;
            a = bmp[gy * gw + gx];
            if (!a) continue;
            dst = buf + py * stride + px;
            if (opaque_bg)
                *dst = Blend565(fg, bg, a);
            else
                *dst = Blend565(fg, *dst, a);
        }
    }
    if (bmp != stack_bmp) free(bmp);
}

int TtfDrawRGB565(uint16_t *buf, int stride, int clip_w, int clip_h,
                  int x, int y, int limit_x,
                  const char *str, uint16_t fg, uint16_t bg, int opaque_bg)
{
    const unsigned char *p;
    int utf8;
    int cur;
    int dots;
    if (!str || !buf) return -1;
    if (!TtfEnsure(g_height ? g_height : 16)) return -1;
    if (limit_x <= 0 || limit_x > clip_w) limit_x = clip_w;

    utf8 = Utf8Ok((const unsigned char *)str);
    dots = GlyphAdvance((uint32_t)'.') * 3;

    /* Measure and decide the cut so "..." fits. */
    {
        const unsigned char *scan = (const unsigned char *)str;
        int width = 0;
        int cut = 0;
        int total = 0;
        uint32_t cp;
        while (NextCp(&scan, &cp, utf8)) {
            int adv = GlyphAdvance(cp);
            total += adv;
            if (width + adv + dots <= limit_x - x)
                cut = (int)(scan - (const unsigned char *)str);
            width += adv;
        }
        if (x + total <= limit_x) cut = -1; /* draw all */

        p = (const unsigned char *)str;
        cur = x;
        if (cut < 0) {
            uint32_t cp2;
            const unsigned char *q = p;
            while (NextCp(&q, &cp2, utf8)) {
                int adv = GlyphAdvance(cp2);
                if (cur + adv > limit_x) break;
                DrawGlyph(buf, stride, clip_w, clip_h, cur, y, cp2, fg, bg, opaque_bg);
                cur += adv;
            }
        } else {
            const unsigned char *end = p + cut;
            uint32_t cp2;
            while (p < end && NextCp(&p, &cp2, utf8)) {
                int adv = GlyphAdvance(cp2);
                DrawGlyph(buf, stride, clip_w, clip_h, cur, y, cp2, fg, bg, opaque_bg);
                cur += adv;
            }
            if (cur + dots <= limit_x) {
                int i;
                for (i = 0; i < 3; i++) {
                    int adv = GlyphAdvance((uint32_t)'.');
                    DrawGlyph(buf, stride, clip_w, clip_h, cur, y, (uint32_t)'.', fg, bg, opaque_bg);
                    cur += adv;
                }
            }
        }
    }
    return cur - x;
}
