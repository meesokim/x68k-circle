#include <SDL2/SDL.h>
#include <libretro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define PATH_SEP '/'
#endif

// Forward declarations
static bool SetupDirectories(const char *specified_path);
static void InitKeyMap(void);

// Audio State
static SDL_AudioDeviceID s_audio_dev = 0;
static const int AUDIO_SAMPLE_RATE = 44100;

// Video State
static SDL_Window *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture *s_texture = NULL;
static unsigned s_video_width = 800;
static unsigned s_video_height = 600;
static bool s_fullscreen = false;

// Input State
static uint8_t s_keystate[RETROK_LAST];
static uint16_t s_padstate[2]; // bitmask of RETRO_DEVICE_ID_JOYPAD_*
static int s_mouse_dx = 0;
static int s_mouse_dy = 0;
static uint8_t s_mouse_btn = 0;
static bool s_mouse_grabbed = false;
static SDL_GameController *s_controller = NULL;

static uint16_t s_scancode_to_retro[SDL_NUM_SCANCODES];

static char s_system_dir[512] = ".";
static char s_save_dir[512] = ".";
static bool s_running = true;

// Libretro logging callback
static void retro_log_print(enum retro_log_level level, const char *fmt, ...)
{
    char buf[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    const char *prefix = "[INFO]";
    if (level == RETRO_LOG_ERROR) prefix = "[ERROR]";
    else if (level == RETRO_LOG_WARN) prefix = "[WARN]";
    else if (level == RETRO_LOG_DEBUG) prefix = "[DEBUG]";

    fprintf(stderr, "[px68k]%s %s\n", prefix, buf);
}

// Libretro environment callback
static bool retro_env_callback(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback *cb = (struct retro_log_callback *)data;
        if (cb) {
            cb->log = retro_log_print;
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const enum retro_pixel_format *fmt = (const enum retro_pixel_format *)data;
        if (fmt && *fmt == RETRO_PIXEL_FORMAT_RGB565) {
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
        const char **dir = (const char **)data;
        if (dir) {
            *dir = s_system_dir;
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
        const char **dir = (const char **)data;
        if (dir) {
            *dir = s_save_dir;
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY: {
        const char **dir = (const char **)data;
        if (dir) {
            *dir = s_system_dir;
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
        bool *can_dupe = (bool *)data;
        if (can_dupe) {
            *can_dupe = true;
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        if (var && var->key) {
            if (strcmp(var->key, "px68k_ramsize") == 0) {
                var->value = "2MB";
                return true;
            }
            if (strcmp(var->key, "px68k_cpuspeed") == 0) {
                var->value = "10MHz";
                return true;
            }
            if (strcmp(var->key, "px68k_frameskip") == 0) {
                var->value = "Full Full";
                return true;
            }
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
        bool *updated = (bool *)data;
        if (updated) {
            *updated = false;
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

static void retro_video_refresh_callback(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;

    if (!s_texture || s_video_width != width || s_video_height != height) {
        if (s_texture) {
            SDL_DestroyTexture(s_texture);
        }
        s_video_width = width;
        s_video_height = height;
        s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
    }

    if (s_texture) {
        SDL_UpdateTexture(s_texture, NULL, data, (int)pitch);
        SDL_RenderClear(s_renderer);
        SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
        SDL_RenderPresent(s_renderer);
    }
}

static void retro_audio_sample_callback(int16_t left, int16_t right)
{
    int16_t sample[2] = { left, right };
    if (s_audio_dev) {
        // Prevent audio latency buildup (> 150ms queued)
        if (SDL_GetQueuedAudioSize(s_audio_dev) < (AUDIO_SAMPLE_RATE * 4 * 15 / 100)) {
            SDL_QueueAudio(s_audio_dev, sample, sizeof(sample));
        }
    }
}

static size_t retro_audio_batch_callback(const int16_t *data, size_t frames)
{
    if (s_audio_dev && data && frames > 0) {
        Uint32 queued = SDL_GetQueuedAudioSize(s_audio_dev);
        Uint32 max_queued = AUDIO_SAMPLE_RATE * 4 * 15 / 100; // ~150ms buffer limit
        if (queued < max_queued) {
            SDL_QueueAudio(s_audio_dev, data, (Uint32)(frames * 4));
        }
    }
    return frames;
}

static void retro_input_poll_callback(void)
{
    // Events polled in main loop
}

static int16_t retro_input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port == 0) {
        if (device == RETRO_DEVICE_KEYBOARD) {
            if (id < RETROK_LAST) {
                return s_keystate[id];
            }
            return 0;
        }
        if (device == RETRO_DEVICE_JOYPAD) {
            if (id < 16) {
                return (s_padstate[0] & (1 << id)) ? 1 : 0;
            }
            return 0;
        }
        if (device == RETRO_DEVICE_MOUSE) {
            switch (id) {
            case RETRO_DEVICE_ID_MOUSE_X: {
                int dx = s_mouse_dx;
                s_mouse_dx = 0;
                return (int16_t)dx;
            }
            case RETRO_DEVICE_ID_MOUSE_Y: {
                int dy = s_mouse_dy;
                s_mouse_dy = 0;
                return (int16_t)dy;
            }
            case RETRO_DEVICE_ID_MOUSE_LEFT:
                return (s_mouse_btn & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
            case RETRO_DEVICE_ID_MOUSE_RIGHT:
                return (s_mouse_btn & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;
            }
            return 0;
        }
    }
    return 0;
}

static void InitKeyMap(void)
{
    memset(s_scancode_to_retro, 0, sizeof(s_scancode_to_retro));

    // Alphabetic
    for (int i = 0; i < 26; i++) {
        s_scancode_to_retro[SDL_SCANCODE_A + i] = RETROK_a + i;
    }
    // Numbers 1-9
    for (int i = 0; i < 9; i++) {
        s_scancode_to_retro[SDL_SCANCODE_1 + i] = RETROK_1 + i;
    }
    s_scancode_to_retro[SDL_SCANCODE_0] = RETROK_0;

    s_scancode_to_retro[SDL_SCANCODE_RETURN]     = RETROK_RETURN;
    s_scancode_to_retro[SDL_SCANCODE_ESCAPE]     = RETROK_ESCAPE;
    s_scancode_to_retro[SDL_SCANCODE_BACKSPACE]  = RETROK_BACKSPACE;
    s_scancode_to_retro[SDL_SCANCODE_TAB]        = RETROK_TAB;
    s_scancode_to_retro[SDL_SCANCODE_SPACE]      = RETROK_SPACE;
    s_scancode_to_retro[SDL_SCANCODE_MINUS]      = RETROK_MINUS;
    s_scancode_to_retro[SDL_SCANCODE_EQUALS]     = RETROK_EQUALS;
    s_scancode_to_retro[SDL_SCANCODE_LEFTBRACKET]= RETROK_LEFTBRACKET;
    s_scancode_to_retro[SDL_SCANCODE_RIGHTBRACKET]= RETROK_RIGHTBRACKET;
    s_scancode_to_retro[SDL_SCANCODE_BACKSLASH]  = RETROK_BACKSLASH;
    s_scancode_to_retro[SDL_SCANCODE_SEMICOLON]  = RETROK_SEMICOLON;
    s_scancode_to_retro[SDL_SCANCODE_APOSTROPHE] = RETROK_QUOTE;
    s_scancode_to_retro[SDL_SCANCODE_GRAVE]      = RETROK_BACKQUOTE;
    s_scancode_to_retro[SDL_SCANCODE_COMMA]      = RETROK_COMMA;
    s_scancode_to_retro[SDL_SCANCODE_PERIOD]     = RETROK_PERIOD;
    s_scancode_to_retro[SDL_SCANCODE_SLASH]      = RETROK_SLASH;
    s_scancode_to_retro[SDL_SCANCODE_CAPSLOCK]   = RETROK_CAPSLOCK;

    // Function keys
    for (int i = 0; i < 12; i++) {
        s_scancode_to_retro[SDL_SCANCODE_F1 + i] = RETROK_F1 + i;
    }

    s_scancode_to_retro[SDL_SCANCODE_INSERT]     = RETROK_INSERT;
    s_scancode_to_retro[SDL_SCANCODE_HOME]       = RETROK_HOME;
    s_scancode_to_retro[SDL_SCANCODE_PAGEUP]     = RETROK_PAGEUP;
    s_scancode_to_retro[SDL_SCANCODE_DELETE]     = RETROK_DELETE;
    s_scancode_to_retro[SDL_SCANCODE_END]        = RETROK_END;
    s_scancode_to_retro[SDL_SCANCODE_PAGEDOWN]   = RETROK_PAGEDOWN;
    s_scancode_to_retro[SDL_SCANCODE_RIGHT]      = RETROK_RIGHT;
    s_scancode_to_retro[SDL_SCANCODE_LEFT]       = RETROK_LEFT;
    s_scancode_to_retro[SDL_SCANCODE_DOWN]       = RETROK_DOWN;
    s_scancode_to_retro[SDL_SCANCODE_UP]         = RETROK_UP;

    // Keypad
    s_scancode_to_retro[SDL_SCANCODE_KP_DIVIDE]  = RETROK_KP_DIVIDE;
    s_scancode_to_retro[SDL_SCANCODE_KP_MULTIPLY]= RETROK_KP_MULTIPLY;
    s_scancode_to_retro[SDL_SCANCODE_KP_MINUS]   = RETROK_KP_MINUS;
    s_scancode_to_retro[SDL_SCANCODE_KP_PLUS]    = RETROK_KP_PLUS;
    s_scancode_to_retro[SDL_SCANCODE_KP_ENTER]   = RETROK_KP_ENTER;
    for (int i = 1; i <= 9; i++) {
        s_scancode_to_retro[SDL_SCANCODE_KP_1 + i - 1] = RETROK_KP1 + i - 1;
    }
    s_scancode_to_retro[SDL_SCANCODE_KP_0]       = RETROK_KP0;
    s_scancode_to_retro[SDL_SCANCODE_KP_PERIOD]  = RETROK_KP_PERIOD;

    // Modifiers
    s_scancode_to_retro[SDL_SCANCODE_LCTRL]      = RETROK_LCTRL;
    s_scancode_to_retro[SDL_SCANCODE_LSHIFT]     = RETROK_LSHIFT;
    s_scancode_to_retro[SDL_SCANCODE_LALT]       = RETROK_LALT;
    s_scancode_to_retro[SDL_SCANCODE_LGUI]       = RETROK_LMETA;
    s_scancode_to_retro[SDL_SCANCODE_RCTRL]      = RETROK_RCTRL;
    s_scancode_to_retro[SDL_SCANCODE_RSHIFT]     = RETROK_RSHIFT;
    s_scancode_to_retro[SDL_SCANCODE_RALT]       = RETROK_RALT;
    s_scancode_to_retro[SDL_SCANCODE_RGUI]       = RETROK_RMETA;
}

static void ProcessSDLEvent(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        s_running = false;
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        bool down = (e->type == SDL_KEYDOWN);
        SDL_Scancode sc = e->key.keysym.scancode;

        // Hotkeys
        if (down && sc == SDL_SCANCODE_F10) {
            s_fullscreen = !s_fullscreen;
            SDL_SetWindowFullscreen(s_window, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            break;
        }
        if (down && sc == SDL_SCANCODE_F11) {
            s_mouse_grabbed = !s_mouse_grabbed;
            SDL_SetRelativeMouseMode(s_mouse_grabbed ? SDL_TRUE : SDL_FALSE);
            break;
        }

        // Map to Libretro Keyboard
        if (sc < SDL_NUM_SCANCODES) {
            uint16_t rk = s_scancode_to_retro[sc];
            if (rk < RETROK_LAST && rk > 0) {
                s_keystate[rk] = down ? 1 : 0;
            }
        }

        // Also map keyboard to Joypad 0 for convenient play
        #define SET_PAD_BIT(bit, val) \
            if (val) s_padstate[0] |= (1 << (bit)); \
            else s_padstate[0] &= ~(1 << (bit))

        switch (sc) {
        case SDL_SCANCODE_UP:    SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_UP, down); break;
        case SDL_SCANCODE_DOWN:  SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_DOWN, down); break;
        case SDL_SCANCODE_LEFT:  SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_LEFT, down); break;
        case SDL_SCANCODE_RIGHT: SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_RIGHT, down); break;
        case SDL_SCANCODE_Z:     SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_B, down); break;
        case SDL_SCANCODE_X:     SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_A, down); break;
        case SDL_SCANCODE_A:     SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_Y, down); break;
        case SDL_SCANCODE_S:     SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_X, down); break;
        case SDL_SCANCODE_RETURN:SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_START, down); break;
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_SPACE: SET_PAD_BIT(RETRO_DEVICE_ID_JOYPAD_SELECT, down); break;
        default: break;
        }
        #undef SET_PAD_BIT
        break;
    }

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        bool down = (e->type == SDL_CONTROLLERBUTTONDOWN);
        int bit = -1;
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: bit = RETRO_DEVICE_ID_JOYPAD_B; break;
        case SDL_CONTROLLER_BUTTON_B: bit = RETRO_DEVICE_ID_JOYPAD_A; break;
        case SDL_CONTROLLER_BUTTON_X: bit = RETRO_DEVICE_ID_JOYPAD_Y; break;
        case SDL_CONTROLLER_BUTTON_Y: bit = RETRO_DEVICE_ID_JOYPAD_X; break;
        case SDL_CONTROLLER_BUTTON_START: bit = RETRO_DEVICE_ID_JOYPAD_START; break;
        case SDL_CONTROLLER_BUTTON_BACK:  bit = RETRO_DEVICE_ID_JOYPAD_SELECT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    bit = RETRO_DEVICE_ID_JOYPAD_UP; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  bit = RETRO_DEVICE_ID_JOYPAD_DOWN; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  bit = RETRO_DEVICE_ID_JOYPAD_LEFT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  bit = RETRO_DEVICE_ID_JOYPAD_L; break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: bit = RETRO_DEVICE_ID_JOYPAD_R; break;
        default: break;
        }
        if (bit >= 0) {
            if (down) s_padstate[0] |= (1 << bit);
            else s_padstate[0] &= ~(1 << bit);
        }
        break;
    }

    case SDL_MOUSEMOTION:
        s_mouse_dx += e->motion.xrel;
        s_mouse_dy += e->motion.yrel;
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        s_mouse_btn = SDL_GetMouseState(NULL, NULL);
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_MIDDLE) {
            s_mouse_grabbed = !s_mouse_grabbed;
            SDL_SetRelativeMouseMode(s_mouse_grabbed ? SDL_TRUE : SDL_FALSE);
        }
        break;
    }
}

static bool FileExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && !S_ISDIR(st.st_mode));
}

static bool DirExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static void CopyFileIfMissing(const char *src, const char *dst)
{
    if (FileExists(dst)) return;
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
}

static bool SetupDirectories(const char *specified_path)
{
    // Try to ensure "keropi" directory exists
#ifdef _WIN32
    _mkdir("keropi");
#else
    mkdir("keropi", 0755);
#endif

    // If BIOS files are in dist_baremetal/keropi or public/system, copy them to keropi/
    const char *candidates_ipl[] = {
        "keropi/iplrom.dat",
        "dist_baremetal/keropi/iplrom.dat",
        "public/system/iplrom.dat",
        "iplrom.dat"
    };
    const char *candidates_cg[] = {
        "keropi/cgrom.dat",
        "dist_baremetal/keropi/cgrom.dat",
        "public/system/cgrom.dat",
        "cgrom.dat"
    };

    for (size_t i = 0; i < sizeof(candidates_ipl)/sizeof(candidates_ipl[0]); i++) {
        if (FileExists(candidates_ipl[i])) {
            CopyFileIfMissing(candidates_ipl[i], "keropi/iplrom.dat");
            break;
        }
    }
    for (size_t i = 0; i < sizeof(candidates_cg)/sizeof(candidates_cg[0]); i++) {
        if (FileExists(candidates_cg[i])) {
            CopyFileIfMissing(candidates_cg[i], "keropi/cgrom.dat");
            break;
        }
    }

    strcpy(s_system_dir, ".");
    strcpy(s_save_dir, "keropi");

    return true;
}

static const char *FindDisk(const char *user_arg)
{
    if (user_arg && FileExists(user_arg)) {
        return user_arg;
    }

    static const char *disks[] = {
        "human302.xdf",
        "keropi/human302.xdf",
        "dist_baremetal/human302.xdf",
        "public/system/human302.xdf",
        "boot.xdf",
        "boot.dim",
        "boot.d88"
    };

    for (size_t i = 0; i < sizeof(disks)/sizeof(disks[0]); i++) {
        if (FileExists(disks[i])) {
            return disks[i];
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    const char *disk_to_load = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] [disk_image.xdf/dim/d88]\n", argv[0]);
            printf("Options:\n");
            printf("  -h, --help       Show this help message\n");
            printf("Hotkeys:\n");
            printf("  F10              Toggle Fullscreen\n");
            printf("  F11 / Mid-Click  Toggle Mouse Grab\n");
            printf("  Arrows / Enter   Joypad D-Pad / Start\n");
            printf("  Z, X, A, S       Joypad B, A, Y, X\n");
            return 0;
        } else if (argv[i][0] != '-') {
            disk_to_load = argv[i];
        }
    }

    SetupDirectories(NULL);
    InitKeyMap();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    if (SDL_NumJoysticks() > 0) {
        s_controller = SDL_GameControllerOpen(0);
    }

    s_window = SDL_CreateWindow("WebX68k / PX68K (SDL2)",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                800, 600,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s_window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(s_renderer, 800, 600);

    // Audio setup
    SDL_AudioSpec wanted, have;
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = AUDIO_SAMPLE_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    wanted.callback = NULL;

    s_audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &have, 0);
    if (s_audio_dev) {
        SDL_PauseAudioDevice(s_audio_dev, 0);
    } else {
        fprintf(stderr, "Warning: failed to open audio: %s\n", SDL_GetError());
    }

    // Initialize Libretro Core
    retro_set_environment(retro_env_callback);
    retro_set_video_refresh(retro_video_refresh_callback);
    retro_set_audio_sample(retro_audio_sample_callback);
    retro_set_audio_sample_batch(retro_audio_batch_callback);
    retro_set_input_poll(retro_input_poll_callback);
    retro_set_input_state(retro_input_state_callback);

    retro_init();

    const char *bootDisk = FindDisk(disk_to_load);
    struct retro_game_info game;
    memset(&game, 0, sizeof(game));

    if (bootDisk) {
        printf("[px68k] Booting with disk: %s\n", bootDisk);
        game.path = bootDisk;
        if (!retro_load_game(&game)) {
            fprintf(stderr, "[px68k] Failed to load disk: %s\n", bootDisk);
            retro_load_game(NULL);
        }
    } else {
        printf("[px68k] No boot disk found; starting without disk\n");
        retro_load_game(NULL);
    }

    // Precise 60 FPS timing loop
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 ticks_per_frame = perf_freq / 60;
    Uint64 next_frame = SDL_GetPerformanceCounter() + ticks_per_frame;

    while (s_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ProcessSDLEvent(&e);
        }

        retro_run();

        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next_frame) {
            Uint64 diff = next_frame - now;
            Uint32 ms_wait = (Uint32)((diff * 1000) / perf_freq);
            if (ms_wait > 1) {
                SDL_Delay(ms_wait - 1);
            }
            while (SDL_GetPerformanceCounter() < next_frame) {
                // busy wait for sub-millisecond precision
            }
        }
        next_frame += ticks_per_frame;
        if (SDL_GetPerformanceCounter() > next_frame + ticks_per_frame) {
            next_frame = SDL_GetPerformanceCounter() + ticks_per_frame;
        }
    }

    retro_unload_game();
    retro_deinit();

    if (s_audio_dev) SDL_CloseAudioDevice(s_audio_dev);
    if (s_texture) SDL_DestroyTexture(s_texture);
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window) SDL_DestroyWindow(s_window);
    if (s_controller) SDL_GameControllerClose(s_controller);
    SDL_Quit();

    return 0;
}
