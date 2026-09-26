#include "CircleKernel.h"
#include "CircleVideo.h"
#include "CircleSound.h"
#include "CircleInput.h"
#include <circle/util.h>
#include <stdio.h>

static CKernel *s_pKernel = nullptr;

CKernel::CKernel(void)
    : m_Memory(TRUE)
    , m_Timer(&m_Interrupt)
    , m_Logger(m_Options.GetLogLevel(), &m_Timer)
    , m_CPUThrottle(CPUSpeedMaximum)
    , m_Screen(800, 600)
    , m_VCHIQ(CMemorySystem::Get(), &m_Interrupt)
    , m_pSound(nullptr)
    , m_USBHCI(&m_Interrupt, &m_Timer, TRUE)
    , m_pKeyboard(nullptr)
    , m_pGamePad{nullptr, nullptr}
    , m_pMouse(nullptr)
    , m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED)
{
    s_pKernel = this;
}

CKernel::~CKernel(void)
{
    s_pKernel = nullptr;
}

boolean CKernel::Initialize(void)
{
    if (!m_Screen.Initialize())
        return FALSE;

    CircleVideoInit(&m_Screen);
    CircleVideoClear(0x0000);

    if (!m_Interrupt.Initialize())
        return FALSE;

    if (!m_Timer.Initialize())
        return FALSE;

    m_CPUThrottle.SetSpeed(CPUSpeedMaximum, TRUE);

    m_Serial.Initialize(115200);
    m_Logger.Initialize(&m_Serial);

    m_Logger.Write("Kernel", LogNotice, "==========================================");
    m_Logger.Write("Kernel", LogNotice, " PX68K Baremetal for Raspberry Pi (Circle)");
    m_Logger.Write("Kernel", LogNotice, " Build: " __DATE__ " " __TIME__);
    m_Logger.Write("Kernel", LogNotice, "==========================================");

    boolean bEmmc = m_EMMC.Initialize();
    FRESULT mRes = f_mount(&m_FileSystem, "SD:", 1);
    m_Logger.Write("Kernel", LogNotice, "EMMC: %s, FatFs: %s (%d)",
                   bEmmc ? "OK" : "FAILED", (mRes == FR_OK) ? "OK" : "FAILED", (int)mRes);

    if (!m_USBHCI.Initialize()) {
        m_Logger.Write("Kernel", LogWarning, "USB HCI init failed");
    }

    if (!m_VCHIQ.Initialize()) {
        m_Logger.Write("Kernel", LogWarning, "VCHIQ init failed");
    } else {
        m_pSound = new CVCHIQSoundBaseDevice(&m_VCHIQ, 44100, 2048, VCHIQSoundDestinationAuto);
        if (m_pSound) {
            CircleSoundInit(m_pSound);
            m_Logger.Write("Kernel", LogNotice, "VCHIQ Sound initialized (44100Hz stereo)");
        }
    }

    CircleInputInit();
    BindDevices();

    return TRUE;
}

void CKernel::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    CircleInputKeyStatusHandlerRaw(ucModifiers, RawKeys);
}

void CKernel::KeyboardRemovedHandler(CDevice *pDevice, void *pContext)
{
    CKernel *pThis = (CKernel *)pContext;
    if (pThis && pThis->m_pKeyboard == (CUSBKeyboardDevice *)pDevice) {
        pThis->m_pKeyboard = nullptr;
    }
}

void CKernel::GamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState)
{
    CircleInputGamePadStatusHandler(nDeviceIndex, pState);
}

void CKernel::GamePadRemovedHandler(CDevice *pDevice, void *pContext)
{
    CKernel *pThis = (CKernel *)pContext;
    if (pThis) {
        for (unsigned i = 0; i < 2; i++) {
            if (pThis->m_pGamePad[i] == (CUSBGamePadDevice *)pDevice) {
                pThis->m_pGamePad[i] = nullptr;
            }
        }
    }
}

void CKernel::MouseStatusHandler(unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove)
{
    (void)nWheelMove;
    CircleInputMouseStatusHandler(nButtons, nDisplacementX, nDisplacementY);
}

void CKernel::BindDevices(void)
{
    if (!m_pKeyboard) {
        m_pKeyboard = (CUSBKeyboardDevice *)m_DeviceNameService.GetDevice("ukbd1", FALSE);
        if (m_pKeyboard) {
            m_pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
            m_pKeyboard->RegisterRemovedHandler(KeyboardRemovedHandler, this);
            m_Logger.Write("Kernel", LogNotice, "USB Keyboard bound");
        }
    }

    for (unsigned i = 0; i < 2; i++) {
        if (!m_pGamePad[i]) {
            char name[16];
            snprintf(name, sizeof(name), "ugpad%u", i + 1);
            m_pGamePad[i] = (CUSBGamePadDevice *)m_DeviceNameService.GetDevice(name, FALSE);
            if (m_pGamePad[i]) {
                m_pGamePad[i]->RegisterStatusHandler(GamePadStatusHandler);
                m_pGamePad[i]->RegisterRemovedHandler(GamePadRemovedHandler, this);
                m_Logger.Write("Kernel", LogNotice, "USB Gamepad %u bound", i + 1);
            }
        }
    }

    if (!m_pMouse) {
        m_pMouse = (CMouseDevice *)m_DeviceNameService.GetDevice("mouse1", FALSE);
        if (m_pMouse) {
            m_pMouse->RegisterStatusHandler(MouseStatusHandler);
            m_Logger.Write("Kernel", LogNotice, "USB Mouse bound");
        }
    }
}
