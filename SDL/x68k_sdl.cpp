#include <SDL2/SDL.h>
#include <libretro.h>
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

#include <dirent.h>

#ifdef _WIN32
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

extern "C" {
void FDD_SetFD(int drive, const char* filename, int readonly);
void FDD_EjectFD(int drive);
void FDD_SetFDInt(void);
void WinX68k_Reset(void);
}

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

// UI Framebuffer for Launcher / File Picker (800 x 600 RGB565)
static uint16_t s_ui_buffer[800 * 600];

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
static bool s_request_picker = false;
static std::string s_current_fdd[2] = {"", ""};

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

        // F12 / F11: Open FDD Disk Selector / Launcher
        if (down && (sc == SDL_SCANCODE_F12 || sc == SDL_SCANCODE_F11)) {
            s_request_picker = true;
            break;
        }

        // F10: Toggle Fullscreen
        if (down && sc == SDL_SCANCODE_F10) {
            s_fullscreen = !s_fullscreen;
            SDL_SetWindowFullscreen(s_window, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            break;
        }

        // Map to Libretro Keyboard
        if (sc < SDL_NUM_SCANCODES) {
            uint16_t rk = s_scancode_to_retro[sc];
            if (rk < RETROK_LAST && rk > 0) {
                s_keystate[rk] = down ? 1 : 0;
            }
        }

        // Map keyboard to Joypad 0 for convenient play
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
#ifdef _WIN32
    _mkdir("keropi");
#else
    mkdir("keropi", 0755);
#endif

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

// -------------------------------------------------------------
// Interactive File & Directory Picker
// -------------------------------------------------------------
struct FileEntry {
    std::string name;
    std::string full_path;
    bool is_dir;
    uint64_t size;
};

class CFilePicker {
public:
    std::string current_dir;
    std::vector<FileEntry> entries;
    int selected_idx;
    int scroll_offset;
    int target_drive; // 0 for FDD0, 1 for FDD1
    int num_subdirs;
    int num_disk_files;
    std::string status_msg;
    uint32_t status_timer;

    CFilePicker() : selected_idx(0), scroll_offset(0), target_drive(0),
                    num_subdirs(0), num_disk_files(0), status_timer(0) {
        char cwd[1024];
#ifdef _WIN32
        if (_getcwd(cwd, sizeof(cwd))) current_dir = cwd;
#else
        if (getcwd(cwd, sizeof(cwd))) current_dir = cwd;
#endif
        else current_dir = ".";

#ifdef _WIN32
        char full[1024];
        if (_fullpath(full, current_dir.c_str(), sizeof(full))) current_dir = full;
#else
        char *resolved = realpath(current_dir.c_str(), NULL);
        if (resolved) {
            current_dir = resolved;
            free(resolved);
        }
#endif
    }

    void SetStatus(const std::string &msg) {
        status_msg = msg;
        status_timer = SDL_GetTicks() + 3000;
    }

    void ScanDirectory() {
        entries.clear();
        selected_idx = 0;
        scroll_offset = 0;

        // Strip trailing slashes unless root
        while (current_dir.length() > 1 && (current_dir.back() == '/' || current_dir.back() == '\\')) {
#ifdef _WIN32
            if (current_dir.length() == 3 && current_dir[1] == ':') break;
#endif
            current_dir.pop_back();
        }

        bool is_root = false;
#ifdef _WIN32
        if ((current_dir.length() <= 3 && current_dir.find(':') != std::string::npos) ||
            current_dir == "/" || current_dir == "\\") {
            is_root = true;
        }
#else
        if (current_dir == "/" || current_dir.empty()) {
            is_root = true;
        }
#endif

        // Parent directory ".."
        if (!is_root) {
            FileEntry parent;
            parent.name = "..";
            parent.full_path = "";
            parent.is_dir = true;
            parent.size = 0;
            entries.push_back(parent);
        }

        std::vector<FileEntry> dirs;
        std::vector<FileEntry> files;

        DIR *dir = opendir(current_dir.c_str());
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                // Ignore dot and hidden files/directories (starting with '.')
                if (de->d_name[0] == '.') continue;

                std::string full = current_dir;
                if (!full.empty() && full.back() != '/' && full.back() != '\\') {
                    full += PATH_SEP;
                }
                full += de->d_name;

                struct stat st;
                if (stat(full.c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        FileEntry fe;
                        fe.name = de->d_name;
                        fe.full_path = full;
                        fe.is_dir = true;
                        fe.size = 0;
                        dirs.push_back(fe);
                    } else {
                        std::string name = de->d_name;
                        size_t dot = name.find_last_of('.');
                        if (dot != std::string::npos) {
                            std::string ext = name.substr(dot);
                            for (char &c : ext) c = tolower(c);
                            if (ext == ".xdf" || ext == ".dim" || ext == ".d88" ||
                                ext == ".88d" || ext == ".hdm" || ext == ".dup" ||
                                ext == ".2hd" || ext == ".img" || ext == ".hds" ||
                                ext == ".hdf" || ext == ".cmd" || ext == ".m3u" ||
                                ext == ".fd"  || ext == ".fdi") {
                                FileEntry fe;
                                fe.name = de->d_name;
                                fe.full_path = full;
                                fe.is_dir = false;
                                fe.size = (uint64_t)st.st_size;
                                files.push_back(fe);
                            }
                        }
                    }
                }
            }
            closedir(dir);
        }

        std::sort(dirs.begin(), dirs.end(), [](const FileEntry &a, const FileEntry &b) {
            return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        std::sort(files.begin(), files.end(), [](const FileEntry &a, const FileEntry &b) {
            return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });

        num_subdirs = (int)dirs.size();
        num_disk_files = (int)files.size();

        for (auto &d : dirs) entries.push_back(d);
        for (auto &f : files) entries.push_back(f);

        printf("[px68k] Scanned '%s': %d subdirs, %d disks (total %zu entries)\n",
               current_dir.c_str(), num_subdirs, num_disk_files, entries.size());
    }

    void NavigateToParent() {
        while (current_dir.length() > 1 && (current_dir.back() == '/' || current_dir.back() == '\\')) {
#ifdef _WIN32
            if (current_dir.length() == 3 && current_dir[1] == ':') break;
#endif
            current_dir.pop_back();
        }
#ifdef _WIN32
        if ((current_dir.length() <= 3 && current_dir.find(':') != std::string::npos) ||
            current_dir == "/" || current_dir == "\\") return;
#else
        if (current_dir == "/" || current_dir.empty()) return;
#endif
        size_t last_sep = current_dir.find_last_of("/\\");
        if (last_sep != std::string::npos) {
            if (last_sep == 0) current_dir = "/";
#ifdef _WIN32
            else if (last_sep == 2 && current_dir[1] == ':') current_dir = current_dir.substr(0, 3);
#endif
            else current_dir = current_dir.substr(0, last_sep);
        }
        ScanDirectory();
    }

    void RenderUI() {
        // Clear background with dark navy blue (0x0842)
        FillBoxRGB565(s_ui_buffer, 800, 0, 0, 800, 600, 0x0842);

        // Header Bar (Y: 0..65)
        FillBoxRGB565(s_ui_buffer, 800, 0, 0, 800, 65, 0x10A4);
        DrawBoxRGB565(s_ui_buffer, 800, 0, 0, 800, 65, 0x31C8);

        DrawTextRGB565(s_ui_buffer, 800, 20, 10, "SHARP X68000 (PX68K) - FDD / DISK SELECTOR", 0xFFE0, 0x10A4);

        char driveStr[128];
        snprintf(driveStr, sizeof(driveStr), "Target: [ FDD %d (%s) ]   (Press [1] for FDD0, [2] for FDD1)",
                 target_drive, target_drive == 0 ? "Drive A" : "Drive B");
        DrawTextRGB565(s_ui_buffer, 800, 20, 28, driveStr, 0x07FF, 0x10A4);

        // Currently loaded disks
        const char *cur0 = !s_current_fdd[0].empty() ? s_current_fdd[0].c_str() : "<Empty>";
        const char *cur1 = !s_current_fdd[1].empty() ? s_current_fdd[1].c_str() : "<Empty>";
        char fddStatus[256];
        snprintf(fddStatus, sizeof(fddStatus), "FDD0: %.35s   |   FDD1: %.35s", cur0, cur1);
        DrawTextRGB565(s_ui_buffer, 800, 20, 46, fddStatus, 0xCE79, 0x10A4);

        // Directory Path Bar (Y: 72..96)
        FillBoxRGB565(s_ui_buffer, 800, 10, 72, 780, 24, 0x18C6);
        DrawBoxRGB565(s_ui_buffer, 800, 10, 72, 780, 24, 0x4228);

        char pathBuf[256];
        snprintf(pathBuf, sizeof(pathBuf), "Path: %.48s  [%d subdirs, %d disks]",
                 current_dir.c_str(), num_subdirs, num_disk_files);
        DrawTextRGB565(s_ui_buffer, 800, 18, 76, pathBuf, 0xFFE0, 0x18C6);

        // Clickable [ UP .. ] button on Directory Bar
        FillBoxRGB565(s_ui_buffer, 800, 705, 74, 80, 20, 0x2A69);
        DrawBoxRGB565(s_ui_buffer, 800, 705, 74, 80, 20, 0x07FF);
        DrawTextRGB565(s_ui_buffer, 800, 713, 76, "[ UP .. ]", 0xFFFF, 0x2A69);

        // File List Container (Y: 102..526)
        FillBoxRGB565(s_ui_buffer, 800, 10, 102, 780, 424, 0x0000);
        DrawBoxRGB565(s_ui_buffer, 800, 10, 102, 780, 424, 0x31A6);

        const int max_visible = 21;
        int total = (int)entries.size();

        if (total == 0) {
            DrawTextRGB565(s_ui_buffer, 800, 40, 130, "(This directory is empty - click [ UP .. ] to go back)", 0x8410, 0x0000);
        } else {
            for (int i = 0; i < max_visible; i++) {
                int idx = scroll_offset + i;
                if (idx >= total) break;

                int y_pos = 106 + i * 20;
                bool is_sel = (idx == selected_idx);

                if (is_sel) {
                    FillBoxRGB565(s_ui_buffer, 800, 12, y_pos - 1, 755, 19, 0x035D);
                }

                uint16_t bg = is_sel ? 0x035D : 0x0000;
                const auto &e = entries[idx];

                if (e.name == "..") {
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, "[UP-DIR] ..  (Parent Directory)", is_sel ? 0xFFFF : 0xFFE0, bg);
                    DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, "[ GO UP ]", is_sel ? 0xFFFF : 0x07FF, bg);
                } else if (e.is_dir) {
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, "[DIR]   ", is_sel ? 0xFFFF : 0x07FF, bg);
                    std::string dname = e.name + "/";
                    if (dname.length() > 56) dname = dname.substr(0, 53) + "...";
                    DrawTextRGB565(s_ui_buffer, 800, 84, y_pos, dname.c_str(), is_sel ? 0xFFFF : 0xFFE0, bg);
                    DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, "[ OPEN > ]", is_sel ? 0xFFFF : 0x07E0, bg);
                } else {
                    bool is_hdd = (e.name.find(".hdf") != std::string::npos || e.name.find(".hds") != std::string::npos);
                    uint16_t tag_color = is_hdd ? 0x07E0 : 0x07FF;
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, is_hdd ? "[HDD]   " : "[FDD]   ", is_sel ? 0xFFFF : tag_color, bg);

                    std::string fname = e.name;
                    if (fname.length() > 56) fname = fname.substr(0, 53) + "...";
                    DrawTextRGB565(s_ui_buffer, 800, 84, y_pos, fname.c_str(), 0xFFFF, bg);

                    // File size
                    char sizeStr[32];
                    if (e.size >= 1024 * 1024) {
                        snprintf(sizeStr, sizeof(sizeStr), "%5.1f MB", (double)e.size / (1024.0 * 1024.0));
                    } else {
                        snprintf(sizeStr, sizeof(sizeStr), "%5u KB", (unsigned)(e.size / 1024));
                    }
                    DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, sizeStr, is_sel ? 0xFFFF : 0x9CE7, bg);
                }
            }

            // Scrollbar
            if (total > max_visible) {
                int sb_x = 776;
                int sb_y = 106;
                int sb_h = 416;
                FillBoxRGB565(s_ui_buffer, 800, sb_x, sb_y, 8, sb_h, 0x18C6);

                int thumb_h = std::max(20, (sb_h * max_visible) / total);
                int thumb_y = sb_y + (scroll_offset * (sb_h - thumb_h)) / (total - max_visible);
                FillBoxRGB565(s_ui_buffer, 800, sb_x, thumb_y, 8, thumb_h, 0x07FF);
            }
        }

        // Status Toast Message
        if (SDL_GetTicks() < status_timer && !status_msg.empty()) {
            FillBoxRGB565(s_ui_buffer, 800, 150, 485, 500, 30, 0x2124);
            DrawBoxRGB565(s_ui_buffer, 800, 150, 485, 500, 30, 0x07FF);
            DrawTextRGB565(s_ui_buffer, 800, 170, 492, status_msg.c_str(), 0xFFFF, 0x2124);
        }

        // Footer Bar (Y: 532..596)
        FillBoxRGB565(s_ui_buffer, 800, 0, 532, 800, 68, 0x10A4);
        DrawBoxRGB565(s_ui_buffer, 800, 0, 532, 800, 68, 0x31C8);

        // Action Buttons (Y: 536..558)
        // 1. [ OPEN/SELECT ] (x: 15..140)
        FillBoxRGB565(s_ui_buffer, 800, 15, 536, 125, 22, 0x0288);
        DrawBoxRGB565(s_ui_buffer, 800, 15, 536, 125, 22, 0x07E0);
        DrawTextRGB565(s_ui_buffer, 800, 18, 539, "[ OPEN/SELECT ]", 0xFFFF, 0x0288);

        // 2. [ UP .. ] (x: 145..230)
        FillBoxRGB565(s_ui_buffer, 800, 145, 536, 85, 22, 0x2128);
        DrawBoxRGB565(s_ui_buffer, 800, 145, 536, 85, 22, 0x07FF);
        DrawTextRGB565(s_ui_buffer, 800, 152, 539, "[ UP .. ]", 0xFFFF, 0x2128);

        // 3. [ FDD 0 ] (x: 235..325)
        uint16_t d0_bg = (target_drive == 0) ? 0x035D : 0x18C6;
        FillBoxRGB565(s_ui_buffer, 800, 235, 536, 90, 22, d0_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 235, 536, 90, 22, (target_drive == 0) ? 0xFFFF : 0x4228);
        DrawTextRGB565(s_ui_buffer, 800, 244, 539, "[ FDD 0 ]", (target_drive == 0) ? 0xFFE0 : 0xCE79, d0_bg);

        // 4. [ FDD 1 ] (x: 330..420)
        uint16_t d1_bg = (target_drive == 1) ? 0x035D : 0x18C6;
        FillBoxRGB565(s_ui_buffer, 800, 330, 536, 90, 22, d1_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 330, 536, 90, 22, (target_drive == 1) ? 0xFFFF : 0x4228);
        DrawTextRGB565(s_ui_buffer, 800, 339, 539, "[ FDD 1 ]", (target_drive == 1) ? 0xFFE0 : 0xCE79, d1_bg);

        // 5. [ EJECT ] (x: 425..510)
        FillBoxRGB565(s_ui_buffer, 800, 425, 536, 85, 22, 0x3100);
        DrawBoxRGB565(s_ui_buffer, 800, 425, 536, 85, 22, 0xF800);
        DrawTextRGB565(s_ui_buffer, 800, 432, 539, "[ EJECT ]", 0xFFFF, 0x3100);

        // 6. [ RESET ] (x: 515..600)
        FillBoxRGB565(s_ui_buffer, 800, 515, 536, 85, 22, 0x3180);
        DrawBoxRGB565(s_ui_buffer, 800, 515, 536, 85, 22, 0xFD20);
        DrawTextRGB565(s_ui_buffer, 800, 522, 539, "[ RESET ]", 0xFFFF, 0x3180);

        // 7. [ CLOSE/ESC ] (x: 675..785)
        FillBoxRGB565(s_ui_buffer, 800, 675, 536, 110, 22, 0x2124);
        DrawBoxRGB565(s_ui_buffer, 800, 675, 536, 110, 22, 0x8410);
        DrawTextRGB565(s_ui_buffer, 800, 678, 539, "[ CLOSE/ESC ]", 0xFFFF, 0x2124);

        // Hint lines
        DrawTextRGB565(s_ui_buffer, 800, 20, 563,
                       "Keys: Enter/Space/Right: Open Subdir | Backspace/Left: Parent | 1/2: Target Drive | Esc: Close",
                       0xCE79, 0x10A4);
        DrawTextRGB565(s_ui_buffer, 800, 20, 579,
                       "Mouse: Click [DIR] or [UP] to Navigate | Double-click to Select | Scroll to Browse",
                       0x8410, 0x10A4);
    }

    // Modal Loop: returns true if disk was selected or action taken
    bool RunModal(SDL_Window *win, SDL_Renderer *rend, SDL_Texture *&tex,
                  std::string &out_path, int &out_drive, bool is_startup = false)
    {
        ScanDirectory();

        // Release mouse grab if active
        if (s_mouse_grabbed) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }

        const int max_visible = 21;
        uint32_t last_click_time = 0;
        bool modal_active = true;
        bool selected = false;

        auto ActivateCurrentSelection = [&]() {
            int total = (int)entries.size();
            if (selected_idx >= 0 && selected_idx < total) {
                const auto &entry = entries[selected_idx];
                if (entry.name == "..") {
                    NavigateToParent();
                } else if (entry.is_dir) {
                    current_dir = entry.full_path;
                    ScanDirectory();
                } else {
                    out_path = entry.full_path;
                    out_drive = target_drive;
                    selected = true;
                    modal_active = false;
                }
            }
        };

        while (modal_active && s_running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    s_running = false;
                    modal_active = false;
                    return false;
                }

                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode key = e.key.keysym.sym;
                    int total = (int)entries.size();

                    switch (key) {
                    case SDLK_UP:
                        if (selected_idx > 0) {
                            selected_idx--;
                            if (selected_idx < scroll_offset) {
                                scroll_offset = selected_idx;
                            }
                        }
                        break;
                    case SDLK_DOWN:
                        if (selected_idx < total - 1) {
                            selected_idx++;
                            if (selected_idx >= scroll_offset + max_visible) {
                                scroll_offset = selected_idx - max_visible + 1;
                            }
                        }
                        break;
                    case SDLK_PAGEUP:
                        selected_idx = std::max(0, selected_idx - max_visible);
                        scroll_offset = std::max(0, scroll_offset - max_visible);
                        break;
                    case SDLK_PAGEDOWN:
                        selected_idx = std::min(total - 1, selected_idx + max_visible);
                        if (selected_idx >= scroll_offset + max_visible) {
                            scroll_offset = std::min(std::max(0, total - max_visible), scroll_offset + max_visible);
                        }
                        break;
                    case SDLK_HOME:
                        selected_idx = 0;
                        scroll_offset = 0;
                        break;
                    case SDLK_END:
                        selected_idx = std::max(0, total - 1);
                        scroll_offset = std::max(0, total - max_visible);
                        break;
                    case SDLK_BACKSPACE:
                    case SDLK_LEFT:
                        NavigateToParent();
                        break;
                    case SDLK_1:
                        target_drive = 0;
                        SetStatus("Target switched to FDD0 (Drive A)");
                        break;
                    case SDLK_2:
                        target_drive = 1;
                        SetStatus("Target switched to FDD1 (Drive B)");
                        break;
                    case SDLK_e:
                        if (!is_startup) {
                            FDD_EjectFD(target_drive);
                            s_current_fdd[target_drive] = "";
                            FDD_SetFDInt();
                            char msg[64];
                            snprintf(msg, sizeof(msg), "FDD%d Ejected", target_drive);
                            SetStatus(msg);
                        }
                        break;
                    case SDLK_r:
                        if (!is_startup) {
                            WinX68k_Reset();
                            SetStatus("System Resetting...");
                        }
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_F11:
                    case SDLK_F12:
                        modal_active = false;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE:
                    case SDLK_RIGHT:
                        ActivateCurrentSelection();
                        break;
                    default:
                        break;
                    }
                }

                if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    int total = (int)entries.size();
                    switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (selected_idx > 0) {
                            selected_idx--;
                            if (selected_idx < scroll_offset) scroll_offset = selected_idx;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (selected_idx < total - 1) {
                            selected_idx++;
                            if (selected_idx >= scroll_offset + max_visible) {
                                scroll_offset = selected_idx - max_visible + 1;
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    case SDL_CONTROLLER_BUTTON_B:
                        NavigateToParent();
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    case SDL_CONTROLLER_BUTTON_A:
                    case SDL_CONTROLLER_BUTTON_START:
                        ActivateCurrentSelection();
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    case SDL_CONTROLLER_BUTTON_X:
                        target_drive = 0;
                        SetStatus("Target switched to FDD0 (Drive A)");
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    case SDL_CONTROLLER_BUTTON_Y:
                        target_drive = 1;
                        SetStatus("Target switched to FDD1 (Drive B)");
                        break;
                    case SDL_CONTROLLER_BUTTON_BACK:
                        modal_active = false;
                        break;
                    }
                } else if (e.type == SDL_JOYBUTTONDOWN) {
                    if (e.jbutton.button == 0) ActivateCurrentSelection();
                    else if (e.jbutton.button == 1) NavigateToParent();
                } else if (e.type == SDL_JOYHATMOTION) {
                    int total = (int)entries.size();
                    if (e.jhat.value & SDL_HAT_UP) {
                        if (selected_idx > 0) {
                            selected_idx--;
                            if (selected_idx < scroll_offset) scroll_offset = selected_idx;
                        }
                    } else if (e.jhat.value & SDL_HAT_DOWN) {
                        if (selected_idx < total - 1) {
                            selected_idx++;
                            if (selected_idx >= scroll_offset + max_visible) {
                                scroll_offset = selected_idx - max_visible + 1;
                            }
                        }
                    } else if (e.jhat.value & SDL_HAT_LEFT) {
                        NavigateToParent();
                    } else if (e.jhat.value & SDL_HAT_RIGHT) {
                        ActivateCurrentSelection();
                    }
                }

                if (e.type == SDL_MOUSEWHEEL) {
                    int total = (int)entries.size();
                    if (e.wheel.y > 0) { // Scroll Up
                        scroll_offset = std::max(0, scroll_offset - 3);
                    } else if (e.wheel.y < 0) { // Scroll Down
                        scroll_offset = std::min(std::max(0, total - max_visible), scroll_offset + 3);
                    }
                }

                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int my = e.button.y;
                    int mx = e.button.x;

                    // 1. Directory Bar [ UP .. ] Button (Y: 72..96, X: 700..785)
                    if (my >= 72 && my <= 96 && mx >= 700 && mx <= 785) {
                        NavigateToParent();
                        continue;
                    }

                    // 2. Click inside list area
                    if (my >= 106 && my < 106 + max_visible * 20 && mx >= 10 && mx <= 770) {
                        int row = (my - 106) / 20;
                        int idx = scroll_offset + row;
                        if (idx >= 0 && idx < (int)entries.size()) {
                            uint32_t now = SDL_GetTicks();
                            const auto &entry = entries[idx];
                            bool do_open = false;

                            if (entry.name == "..") {
                                // Click on [UP-DIR] or right tag or double click
                                if (mx <= 100 || mx >= 660 || (idx == selected_idx && (now - last_click_time) < 450)) {
                                    do_open = true;
                                }
                            } else if (entry.is_dir) {
                                // Single-click on [DIR] tag on left OR [ OPEN > ] button on right OR double-click row
                                if (mx <= 80 || mx >= 660 || (idx == selected_idx && (now - last_click_time) < 450)) {
                                    do_open = true;
                                }
                            } else {
                                // File: Double click or right click area
                                if (mx >= 660 || (idx == selected_idx && (now - last_click_time) < 450)) {
                                    do_open = true;
                                }
                            }

                            if (do_open) {
                                if (entry.name == "..") {
                                    NavigateToParent();
                                } else if (entry.is_dir) {
                                    current_dir = entry.full_path;
                                    ScanDirectory();
                                } else {
                                    out_path = entry.full_path;
                                    out_drive = target_drive;
                                    selected = true;
                                    modal_active = false;
                                }
                            } else {
                                selected_idx = idx;
                            }
                            last_click_time = now;
                        }
                    }

                    // 3. Footer Action Buttons (Y: 536..558)
                    if (my >= 536 && my <= 558) {
                        if (mx >= 15 && mx <= 140) {
                            // [ OPEN/SELECT ]
                            ActivateCurrentSelection();
                        } else if (mx >= 145 && mx <= 230) {
                            // [ UP .. ]
                            NavigateToParent();
                        } else if (mx >= 235 && mx <= 325) {
                            // [ FDD 0 ]
                            target_drive = 0;
                            SetStatus("Target switched to FDD0 (Drive A)");
                        } else if (mx >= 330 && mx <= 420) {
                            // [ FDD 1 ]
                            target_drive = 1;
                            SetStatus("Target switched to FDD1 (Drive B)");
                        } else if (mx >= 425 && mx <= 510) {
                            // [ EJECT ]
                            if (!is_startup) {
                                FDD_EjectFD(target_drive);
                                s_current_fdd[target_drive] = "";
                                FDD_SetFDInt();
                                char msg[64];
                                snprintf(msg, sizeof(msg), "FDD%d Ejected", target_drive);
                                SetStatus(msg);
                            }
                        } else if (mx >= 515 && mx <= 600) {
                            // [ RESET ]
                            if (!is_startup) {
                                WinX68k_Reset();
                                SetStatus("System Resetting...");
                            }
                        } else if (mx >= 675 && mx <= 785) {
                            // [ CLOSE/ESC ]
                            modal_active = false;
                        }
                    }
                }
            }

            // Render Launcher Frame
            RenderUI();

            // Update texture (800x600 RGB565)
            if (!tex || s_video_width != 800 || s_video_height != 600) {
                if (tex) SDL_DestroyTexture(tex);
                s_video_width = 800;
                s_video_height = 600;
                tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGB565,
                                        SDL_TEXTUREACCESS_STREAMING, 800, 600);
            }

            if (tex) {
                SDL_UpdateTexture(tex, NULL, s_ui_buffer, 800 * sizeof(uint16_t));
                SDL_RenderClear(rend);
                SDL_RenderCopy(rend, tex, NULL, NULL);
                SDL_RenderPresent(rend);
            }

            SDL_Delay(16); // ~60 FPS
        }

        // Restore mouse grab if desired
        if (s_mouse_grabbed) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }

        return selected;
    }
};

static CFilePicker s_picker;

int main(int argc, char *argv[])
{
    const char *disk_to_load = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] [disk_image.xdf/dim/d88]\n", argv[0]);
            printf("Options:\n");
            printf("  -h, --help       Show this help message\n");
            printf("Hotkeys:\n");
            printf("  F12 / F11        Open FDD Disk Selector / Launcher\n");
            printf("  F10              Toggle Fullscreen\n");
            printf("  Mid-Click        Toggle Mouse Grab\n");
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

    std::string bootDiskPath;
    int bootDrive = 0;

    // If no disk was given on command line, show the Directory & File Selector!
    if (!disk_to_load) {
        printf("[px68k] Opening FDD Disk Selector...\n");
        if (s_picker.RunModal(s_window, s_renderer, s_texture, bootDiskPath, bootDrive, true)) {
            disk_to_load = bootDiskPath.c_str();
        }
    }

    struct retro_game_info game;
    memset(&game, 0, sizeof(game));

    if (disk_to_load && FileExists(disk_to_load)) {
        printf("[px68k] Booting with disk: %s\n", disk_to_load);
        game.path = disk_to_load;
        if (!retro_load_game(&game)) {
            fprintf(stderr, "[px68k] Failed to load disk: %s\n", disk_to_load);
            retro_load_game(NULL);
        }
    } else {
        printf("[px68k] Starting without disk...\n");
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

        // Handle in-game Disk Selector request (F12 or F11)
        if (s_request_picker) {
            s_request_picker = false;
            std::string new_disk;
            int new_drive = 0;
            if (s_picker.RunModal(s_window, s_renderer, s_texture, new_disk, new_drive, false)) {
                if (!new_disk.empty()) {
                    FDD_SetFD(new_drive, (char*)new_disk.c_str(), 0);
                    s_current_fdd[new_drive] = new_disk;
                    FDD_SetFDInt();
                    printf("[px68k] Inserted %s into FDD%d\n", new_disk.c_str(), new_drive);
                }
            }
            next_frame = SDL_GetPerformanceCounter() + ticks_per_frame;
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
