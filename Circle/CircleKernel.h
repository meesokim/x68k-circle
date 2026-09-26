#ifndef _CircleKernel_h
#define _CircleKernel_h

#include <circle/memory.h>
#include <circle/exceptionhandler.h>
#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/serial.h>
#include <circle/sched/scheduler.h>
#include <circle/cputhrottle.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/usb/usbgamepad.h>
#include <circle/input/mouse.h>
#include <circle/types.h>
#include <vc4/sound/vchiqsoundbasedevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel
{
public:
    CKernel(void);
    ~CKernel(void);

    boolean Initialize(void);
    TShutdownMode Run(void);

    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);
    static void GamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState);
    static void GamePadRemovedHandler(CDevice *pDevice, void *pContext);
    static void MouseStatusHandler(unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove);

private:
    void BindDevices(void);

private:
    CMemorySystem       m_Memory;
    CKernelOptions      m_Options;
    CDeviceNameService  m_DeviceNameService;
    CExceptionHandler   m_ExceptionHandler;
    CInterruptSystem    m_Interrupt;
    CTimer              m_Timer;
    CLogger             m_Logger;
    CSerialDevice       m_Serial;
    CActLED             m_ActLED;
    CCPUThrottle        m_CPUThrottle;
    CScreenDevice       m_Screen;
    CVCHIQDevice        m_VCHIQ;
    CScheduler          m_Scheduler;
    CVCHIQSoundBaseDevice *m_pSound;

    CUSBHCIDevice       m_USBHCI;
    CUSBKeyboardDevice *m_pKeyboard;
    CUSBGamePadDevice  *m_pGamePad[2];
    CMouseDevice       *m_pMouse;
    CEMMCDevice         m_EMMC;
    FATFS               m_FileSystem;
};

#endif
