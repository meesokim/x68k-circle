#include "CircleInput.h"
#include <libretro.h>
#include <string.h>

static uint8_t s_KeyState[RETROK_LAST];
static uint16_t s_PadState[2]; // bitmask of RETRO_DEVICE_ID_JOYPAD_*

static int s_MouseX = 0;
static int s_MouseY = 0;
static uint8_t s_MouseButton = 0;

static uint16_t s_HidToRetroKey[256];
static bool s_KeyTableInit = false;

struct KeyPair {
    uint8_t hid;
    uint16_t retro;
};

static const KeyPair s_KeyPairs[] = {
    { 0x04, RETROK_a }, { 0x05, RETROK_b }, { 0x06, RETROK_c }, { 0x07, RETROK_d },
    { 0x08, RETROK_e }, { 0x09, RETROK_f }, { 0x0A, RETROK_g }, { 0x0B, RETROK_h },
    { 0x0C, RETROK_i }, { 0x0D, RETROK_j }, { 0x0E, RETROK_k }, { 0x0F, RETROK_l },
    { 0x10, RETROK_m }, { 0x11, RETROK_n }, { 0x12, RETROK_o }, { 0x13, RETROK_p },
    { 0x14, RETROK_q }, { 0x15, RETROK_r }, { 0x16, RETROK_s }, { 0x17, RETROK_t },
    { 0x18, RETROK_u }, { 0x19, RETROK_v }, { 0x1A, RETROK_w }, { 0x1B, RETROK_x },
    { 0x1C, RETROK_y }, { 0x1D, RETROK_z },
    { 0x1E, RETROK_1 }, { 0x1F, RETROK_2 }, { 0x20, RETROK_3 }, { 0x21, RETROK_4 },
    { 0x22, RETROK_5 }, { 0x23, RETROK_6 }, { 0x24, RETROK_7 }, { 0x25, RETROK_8 },
    { 0x26, RETROK_9 }, { 0x27, RETROK_0 },
    { 0x28, RETROK_RETURN }, { 0x29, RETROK_ESCAPE }, { 0x2A, RETROK_BACKSPACE },
    { 0x2B, RETROK_TAB },    { 0x2C, RETROK_SPACE },  { 0x2D, RETROK_MINUS },
    { 0x2E, RETROK_EQUALS }, { 0x2F, RETROK_LEFTBRACKET }, { 0x30, RETROK_RIGHTBRACKET },
    { 0x31, RETROK_BACKSLASH }, { 0x33, RETROK_SEMICOLON }, { 0x34, RETROK_QUOTE },
    { 0x35, RETROK_BACKQUOTE }, { 0x36, RETROK_COMMA }, { 0x37, RETROK_PERIOD },
    { 0x38, RETROK_SLASH },  { 0x39, RETROK_CAPSLOCK },
    { 0x3A, RETROK_F1 }, { 0x3B, RETROK_F2 }, { 0x3C, RETROK_F3 }, { 0x3D, RETROK_F4 },
    { 0x3E, RETROK_F5 }, { 0x3F, RETROK_F6 }, { 0x40, RETROK_F7 }, { 0x41, RETROK_F8 },
    { 0x42, RETROK_F9 }, { 0x43, RETROK_F10 }, { 0x44, RETROK_F11 }, { 0x45, RETROK_F12 },
    { 0x49, RETROK_INSERT }, { 0x4A, RETROK_HOME }, { 0x4B, RETROK_PAGEUP },
    { 0x4C, RETROK_DELETE }, { 0x4D, RETROK_END },  { 0x4E, RETROK_PAGEDOWN },
    { 0x4F, RETROK_RIGHT },  { 0x50, RETROK_LEFT },  { 0x51, RETROK_DOWN }, { 0x52, RETROK_UP },
    { 0x54, RETROK_KP_DIVIDE }, { 0x55, RETROK_KP_MULTIPLY }, { 0x56, RETROK_KP_MINUS }, { 0x57, RETROK_KP_PLUS },
    { 0x58, RETROK_KP_ENTER },
    { 0x59, RETROK_KP1 }, { 0x5A, RETROK_KP2 }, { 0x5B, RETROK_KP3 },
    { 0x5C, RETROK_KP4 }, { 0x5D, RETROK_KP5 }, { 0x5E, RETROK_KP6 },
    { 0x5F, RETROK_KP7 }, { 0x60, RETROK_KP8 }, { 0x61, RETROK_KP9 },
    { 0x62, RETROK_KP0 }, { 0x63, RETROK_KP_PERIOD },
    { 0, 0 }
};

static void InitKeyTable(void)
{
    if (s_KeyTableInit) return;
    memset(s_HidToRetroKey, 0, sizeof(s_HidToRetroKey));
    for (int i = 0; s_KeyPairs[i].hid != 0; i++) {
        s_HidToRetroKey[s_KeyPairs[i].hid] = s_KeyPairs[i].retro;
    }
    s_KeyTableInit = true;
}

extern "C" {

void CircleInputInit(void)
{
    InitKeyTable();
    memset(s_KeyState, 0, sizeof(s_KeyState));
    memset(s_PadState, 0, sizeof(s_PadState));
    s_MouseX = 0;
    s_MouseY = 0;
    s_MouseButton = 0;
}

void CircleInputKeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    InitKeyTable();
    memset(s_KeyState, 0, sizeof(s_KeyState));

    if (ucModifiers & 0x01) s_KeyState[RETROK_LCTRL] = 1;
    if (ucModifiers & 0x02) s_KeyState[RETROK_LSHIFT] = 1;
    if (ucModifiers & 0x04) s_KeyState[RETROK_LALT] = 1;
    if (ucModifiers & 0x08) s_KeyState[RETROK_LMETA] = 1;
    if (ucModifiers & 0x10) s_KeyState[RETROK_RCTRL] = 1;
    if (ucModifiers & 0x20) s_KeyState[RETROK_RSHIFT] = 1;
    if (ucModifiers & 0x40) s_KeyState[RETROK_RALT] = 1;
    if (ucModifiers & 0x80) s_KeyState[RETROK_RMETA] = 1;

    for (int i = 0; i < 6; i++) {
        unsigned char sc = RawKeys[i];
        if (sc == 0) continue;
        uint16_t rk = s_HidToRetroKey[sc];
        if (rk < RETROK_LAST) {
            s_KeyState[rk] = 1;
        }
    }
}

void CircleInputGamePadStatusHandler(unsigned nDeviceIndex, const TGamePadState *pState)
{
    if (nDeviceIndex >= 2 || !pState) return;

    uint16_t pad = 0;

    // Standard Circle gamepad buttons
    if (pState->buttons & GamePadButtonUp)     pad |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (pState->buttons & GamePadButtonDown)   pad |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (pState->buttons & GamePadButtonLeft)   pad |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (pState->buttons & GamePadButtonRight)  pad |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);

    if (pState->buttons & GamePadButtonA)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (pState->buttons & GamePadButtonB)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (pState->buttons & GamePadButtonX)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (pState->buttons & GamePadButtonY)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (pState->buttons & GamePadButtonL)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (pState->buttons & GamePadButtonR)      pad |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (pState->buttons & GamePadButtonSelect) pad |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (pState->buttons & GamePadButtonStart)  pad |= (1 << RETRO_DEVICE_ID_JOYPAD_START);

    // Fallback for generic USB gamepads with raw button masks
    if ((pad & 0xFF) == 0 && pState->nbuttons > 0) {
        if (pState->buttons & 0x01) pad |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
        if (pState->buttons & 0x02) pad |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
        if (pState->buttons & 0x04) pad |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
        if (pState->buttons & 0x08) pad |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    }

    s_PadState[nDeviceIndex] = pad;
}

void CircleInputMouseStatusHandler(unsigned nButtons, int nDisplacementX, int nDisplacementY)
{
    s_MouseButton = (uint8_t)nButtons;
    s_MouseX += nDisplacementX;
    s_MouseY += nDisplacementY;
}

void CircleInputPoll(void)
{
}

int16_t CircleInputGetState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_JOYPAD) {
        if (port >= 2) return 0;
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return (int16_t)s_PadState[port];
        }
        if (id < 16) {
            return (s_PadState[port] & (1 << id)) ? 1 : 0;
        }
        return 0;
    }
    else if (device == RETRO_DEVICE_KEYBOARD) {
        if (id < RETROK_LAST) {
            return s_KeyState[id] ? 1 : 0;
        }
        return 0;
    }
    else if (device == RETRO_DEVICE_MOUSE) {
        if (id == RETRO_DEVICE_ID_MOUSE_X) {
            int dx = s_MouseX;
            s_MouseX = 0;
            return (int16_t)dx;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_Y) {
            int dy = s_MouseY;
            s_MouseY = 0;
            return (int16_t)dy;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_LEFT) {
            return (s_MouseButton & 1) ? 1 : 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_RIGHT) {
            return (s_MouseButton & 2) ? 1 : 0;
        }
    }

    return 0;
}

} // extern "C"
