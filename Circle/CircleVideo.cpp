#include "CircleVideo.h"
#include <circle/bcmframebuffer.h>
#include <circle/synchronize.h>
#include <string.h>

static CScreenDevice *s_pScreenDevice = nullptr;
static uint16_t *s_pScreenBuf = nullptr;
static unsigned s_nWidth = 800;
static unsigned s_nHeight = 600;
static unsigned s_nPitch = 800; // in uint16_t pixels

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
    CleanAndInvalidateDataCacheRange((u32)(uintptr_t)s_pScreenBuf,
                                     s_nPitch * s_nHeight * sizeof(uint16_t));
}

void CircleVideoUpdate(const void *data, unsigned width, unsigned height, size_t pitch_bytes)
{
    if (!s_pScreenBuf || !data || width == 0 || height == 0) return;

    unsigned copy_w = width > s_nWidth ? s_nWidth : width;
    unsigned copy_h = height > s_nHeight ? s_nHeight : height;

    unsigned dst_x = (s_nWidth > copy_w) ? (s_nWidth - copy_w) / 2 : 0;
    unsigned dst_y = (s_nHeight > copy_h) ? (s_nHeight - copy_h) / 2 : 0;

    const uint8_t *src_bytes = (const uint8_t *)data;

    for (unsigned y = 0; y < copy_h; y++)
    {
        const uint16_t *src_row = (const uint16_t *)(src_bytes + y * pitch_bytes);
        uint16_t *dst_row = s_pScreenBuf + (dst_y + y) * s_nPitch + dst_x;
        memcpy(dst_row, src_row, copy_w * sizeof(uint16_t));
    }

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
