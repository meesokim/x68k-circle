#include "CircleVideo.h"
#include <circle/bcmframebuffer.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <string.h>

static CScreenDevice *s_pScreenDevice = nullptr;
static uint16_t *s_pScreenBuf = nullptr;
static unsigned s_nWidth = 800;
static unsigned s_nHeight = 600;
static unsigned s_nPitch = 800; // in uint16_t pixels

static uint16_t s_overlay[480 * 64];
static unsigned s_overlay_w = 0;
static unsigned s_overlay_h = 0;
static unsigned s_overlay_until = 0;

static void BlitOverlay(void)
{
    if (!s_pScreenBuf || s_overlay_w == 0 || s_overlay_h == 0) return;
    unsigned now = CTimer::GetClockTicks();
    if ((int)(s_overlay_until - now) <= 0) {
        s_overlay_w = 0;
        return;
    }
    const unsigned margin = 8;
    if (s_nWidth < s_overlay_w + margin || s_nHeight < s_overlay_h + margin) return;
    unsigned x0 = s_nWidth - s_overlay_w - margin;
    unsigned y0 = margin;
    for (unsigned y = 0; y < s_overlay_h; y++) {
        memcpy(s_pScreenBuf + (y0 + y) * s_nPitch + x0,
               s_overlay + y * s_overlay_w,
               s_overlay_w * sizeof(uint16_t));
    }
}

extern "C" {

void CircleVideoInit(CScreenDevice *pScreenDevice)
{
    s_pScreenDevice = pScreenDevice;
    if (s_pScreenDevice && s_pScreenDevice->GetFrameBuffer())
    {
        CBcmFrameBuffer *pFB = s_pScreenDevice->GetFrameBuffer();
        s_pScreenBuf = (uint16_t *)pFB->GetBuffer();
        s_nWidth = pFB->GetWidth();
        s_nHeight = pFB->GetHeight();
        s_nPitch = pFB->GetPitch() / sizeof(uint16_t);
        CircleVideoClear(0x0000);
    }
}

void CircleVideoClear(uint16_t color)
{
    if (!s_pScreenBuf || s_nWidth == 0 || s_nHeight == 0) return;

    for (unsigned y = 0; y < s_nHeight; y++)
    {
        uint16_t *row = s_pScreenBuf + y * s_nPitch;
        for (unsigned x = 0; x < s_nWidth; x++)
        {
            row[x] = color;
        }
    }
    BlitOverlay();
    CleanAndInvalidateDataCacheRange((u32)(uintptr_t)s_pScreenBuf,
                                     s_nPitch * s_nHeight * sizeof(uint16_t));
}

void CircleVideoSetOverlay(const uint16_t *pixels, unsigned width, unsigned height, unsigned duration_us)
{
    if (!pixels || width == 0 || height == 0 || width > 480 || height > 64 || duration_us == 0) {
        s_overlay_w = 0;
        return;
    }
    s_overlay_w = width;
    s_overlay_h = height;
    for (unsigned y = 0; y < height; y++)
        memcpy(s_overlay + y * width, pixels + y * width, width * sizeof(uint16_t));
    s_overlay_until = CTimer::GetClockTicks() + duration_us;
}

void CircleVideoUpdate(const void *data, unsigned width, unsigned height, size_t pitch_bytes)
{
    if (!s_pScreenBuf || !data || width == 0 || height == 0) return;

    unsigned copy_w = width > s_nWidth ? s_nWidth : width;
    unsigned copy_h = height > s_nHeight ? s_nHeight : height;

    unsigned dst_x = (s_nWidth > copy_w) ? (s_nWidth - copy_w) / 2 : 0;
    unsigned dst_y = (s_nHeight > copy_h) ? (s_nHeight - copy_h) / 2 : 0;

    const uint8_t *src_bytes = (const uint8_t *)data;
    const unsigned row_bytes = s_nWidth * sizeof(uint16_t);

    // The disk menu fills the screen. A smaller X68000 frame is centered, so
    // anything outside that rectangle must be cleared or the menu stays behind it.
    for (unsigned y = 0; y < dst_y; y++)
        memset(s_pScreenBuf + y * s_nPitch, 0, row_bytes);
    for (unsigned y = dst_y + copy_h; y < s_nHeight; y++)
        memset(s_pScreenBuf + y * s_nPitch, 0, row_bytes);

    for (unsigned y = 0; y < copy_h; y++)
    {
        const uint16_t *src_row = (const uint16_t *)(src_bytes + y * pitch_bytes);
        uint16_t *dst_row = s_pScreenBuf + (dst_y + y) * s_nPitch;
        if (dst_x > 0)
            memset(dst_row, 0, dst_x * sizeof(uint16_t));
        memcpy(dst_row + dst_x, src_row, copy_w * sizeof(uint16_t));
        unsigned tail = s_nWidth - dst_x - copy_w;
        if (tail > 0)
            memset(dst_row + dst_x + copy_w, 0, tail * sizeof(uint16_t));
    }

    BlitOverlay();
    CleanAndInvalidateDataCacheRange((u32)(uintptr_t)s_pScreenBuf,
                                     s_nPitch * s_nHeight * sizeof(uint16_t));
}

uint16_t* CircleVideoGetFramebuffer(void)
{
    return s_pScreenBuf;
}

unsigned CircleVideoGetWidth(void)
{
    return s_nWidth;
}

unsigned CircleVideoGetHeight(void)
{
    return s_nHeight;
}

} // extern "C"
