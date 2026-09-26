#ifndef _CircleInput_h
#define _CircleInput_h

#include <circle/usb/usbgamepad.h>
#include <circle/input/mouse.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void CircleInputInit(void);
void CircleInputKeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
void CircleInputGamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState);
void CircleInputMouseStatusHandler(unsigned nButtons, int nDisplacementX, int nDisplacementY);

int16_t CircleInputGetState(unsigned port, unsigned device, unsigned index, unsigned id);
void CircleInputPoll(void);

#ifdef __cplusplus
}
#endif

#endif
