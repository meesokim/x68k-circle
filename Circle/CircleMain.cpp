#include "CircleKernel.h"
#include "CircleVideo.h"
#include "CircleSound.h"
#include "CircleInput.h"
#include <libretro.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <fatfs/ff.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void retro_log_print(enum retro_log_level level, const char *fmt, ...)
{
    char buf[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    TLogSeverity circleLevel = LogDebug;
    if (level == RETRO_LOG_ERROR) circleLevel = LogError;
    else if (level == RETRO_LOG_WARN) circleLevel = LogWarning;
    else if (level == RETRO_LOG_INFO) circleLevel = LogNotice;

    CLogger::Get()->Write("px68k", circleLevel, "%s", buf);
}

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
            *dir = "SD:";
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
        const char **dir = (const char **)data;
        if (dir) {
            *dir = "SD:/keropi";
            return true;
        }
        break;
    }
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY: {
        const char **dir = (const char **)data;
        if (dir) {
            *dir = "SD:";
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
    if (data) {
        CircleVideoUpdate(data, width, height, pitch);
    }
}

static void retro_audio_sample_callback(int16_t left, int16_t right)
{
    int16_t sample[2] = { left, right };
    CircleSoundWrite(sample, 1);
}

static size_t retro_audio_batch_callback(const int16_t *data, size_t frames)
{
    return CircleSoundWrite(data, frames);
}

static void retro_input_poll_callback(void)
{
    CircleInputPoll();
}

static int16_t retro_input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    return CircleInputGetState(port, device, index, id);
}

static const char* FindBootDisk(void)
{
    static const char *candidates[] = {
        "SD:/human302.xdf",
        "SD:/keropi/human302.xdf",
        "SD:/boot.xdf",
        "SD:/boot.dim",
        "SD:/boot.d88"
    };

    FILINFO fno;
    for (unsigned i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        if (f_stat(candidates[i], &fno) == FR_OK) {
            return candidates[i];
        }
    }

    DIR dir;
    if (f_opendir(&dir, "SD:/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            size_t len = strlen(fno.fname);
            if (len > 4) {
                const char *ext = fno.fname + len - 4;
                if (strcasecmp(ext, ".xdf") == 0 ||
                    strcasecmp(ext, ".dim") == 0 ||
                    strcasecmp(ext, ".d88") == 0 ||
                    strcasecmp(ext, ".hds") == 0) {
                    static char foundPath[256];
                    snprintf(foundPath, sizeof(foundPath), "SD:/%s", fno.fname);
                    f_closedir(&dir);
                    return foundPath;
                }
            }
        }
        f_closedir(&dir);
    }

    return nullptr;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write("Kernel", LogNotice, "Initializing px68k-libretro core...");

    f_mkdir("SD:/keropi");

    // Copy IPLROM/CGROM if in root SD but not in keropi
    FILINFO fno;
    if (f_stat("SD:/keropi/iplrom.dat", &fno) != FR_OK && f_stat("SD:/iplrom.dat", &fno) == FR_OK) {
        // can be opened from root or keropi
    }

    retro_set_environment(retro_env_callback);
    retro_set_video_refresh(retro_video_refresh_callback);
    retro_set_audio_sample(retro_audio_sample_callback);
    retro_set_audio_sample_batch(retro_audio_batch_callback);
    retro_set_input_poll(retro_input_poll_callback);
    retro_set_input_state(retro_input_state_callback);

    retro_init();

    const char *bootDisk = FindBootDisk();
    struct retro_game_info game;
    memset(&game, 0, sizeof(game));

    if (bootDisk) {
        m_Logger.Write("Kernel", LogNotice, "Booting with disk: %s", bootDisk);
        game.path = bootDisk;
        if (!retro_load_game(&game)) {
            m_Logger.Write("Kernel", LogError, "Failed to load disk: %s", bootDisk);
            retro_load_game(NULL);
        }
    } else {
        m_Logger.Write("Kernel", LogWarning, "No boot disk found; starting px68k without disk");
        retro_load_game(NULL);
    }

    m_Logger.Write("Kernel", LogNotice, "Entering px68k main emulation loop...");

    const unsigned frameUs = 16666; // ~60 FPS
    unsigned frameCount = 0;

    while (1) {
        unsigned startTicks = m_Timer.GetClockTicks();

        retro_run();

        frameCount++;
        if ((frameCount % 60) == 0) {
            BindDevices();
        }

        unsigned elapsedUs = m_Timer.GetClockTicks() - startTicks;
        if (elapsedUs < frameUs) {
            CTimer::SimpleusDelay(frameUs - elapsedUs);
        }
    }

    retro_unload_game();
    retro_deinit();

    return ShutdownHalt;
}

int main(void)
{
    CKernel Kernel;
    if (!Kernel.Initialize()) {
        return 1;
    }
    Kernel.Run();
    return 0;
}
