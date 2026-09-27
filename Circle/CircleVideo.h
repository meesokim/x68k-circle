#ifndef _CircleVideo_h
#define _CircleVideo_h

#include <circle/screen.h>
#include <circle/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void CircleVideoInit(CScreenDevice *pScreenDevice);
void CircleVideoClear(uint16_t color);
void CircleVideoUpdate(const void *data, unsigned width, unsigned height, size_t pitch_bytes);
// RGB565 overlay, tightly packed. Shown at the upper right for duration_us.
void CircleVideoSetOverlay(const uint16_t *pixels, unsigned width, unsigned height, unsigned duration_us);
uint16_t* CircleVideoGetFramebuffer(void);
unsigned CircleVideoGetWidth(void);
unsigned CircleVideoGetHeight(void);

#ifdef __cplusplus
}
#endif

#endif
