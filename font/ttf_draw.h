#ifndef TTF_DRAW_H
#define TTF_DRAW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load NanumGothic.ttf on first use. pixel_height is the em size (16 or 24).
 * Returns 1 when the face is ready. The same file and renderer are used by
 * the baremetal menu and the Linux host menu. */
int TtfEnsure(int pixel_height);

/* Draw a UTF-8 string, or CP949 when the bytes are not valid UTF-8.
 * y is the top of the line. Stops at limit_x and appends "..." if needed.
 * opaque_bg blends against bg; otherwise each pixel blends against the buffer.
 * Returns the advance in pixels, or -1 when the font is not loaded. */
int TtfDrawRGB565(uint16_t *buf, int stride, int clip_w, int clip_h,
                  int x, int y, int limit_x,
                  const char *str, uint16_t fg, uint16_t bg, int opaque_bg);

#ifdef __cplusplus
}
#endif

#endif
