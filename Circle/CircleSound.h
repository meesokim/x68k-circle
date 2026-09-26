#ifndef _CircleSound_h
#define _CircleSound_h

#include <vc4/sound/vchiqsoundbasedevice.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void CircleSoundInit(CVCHIQSoundBaseDevice *pSoundDevice);
size_t CircleSoundWrite(const int16_t *data, size_t frames);
unsigned CircleSoundGetQueueFramesAvail(void);

#ifdef __cplusplus
}
#endif

#endif
