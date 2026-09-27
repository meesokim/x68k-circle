#include "disk_menu.hpp"
#include <string.h>

extern "C" {
#include "prop.h"
void CircleVideoUpdate(const void *data, unsigned width, unsigned height, size_t pitch_bytes);
void CircleVideoClear(uint16_t color);
void CircleVideoSetOverlay(const uint16_t *pixels, unsigned width, unsigned height, unsigned duration_us);
int16_t CircleInputGetState(unsigned port, unsigned device, unsigned index, unsigned id);
}

class CTimer {
public:
    static void SimpleusDelay(unsigned nMicroSeconds);
    static unsigned GetClockTicks(void);
};

std::string s_current_fdd[2] = {"", ""};
std::string s_loaded_zip_path;
CFilePicker s_picker;

uint32_t DiskMenuNowMs()
{
    return (uint32_t)(CTimer::GetClockTicks() / 1000);
}

static void SyncConfigImage(int drive, const std::string &path)
{
    if (drive < 0 || drive > 1) return;
    strncpy(Config.FDDImage[drive], path.c_str(), MAX_PATH - 1);
    Config.FDDImage[drive][MAX_PATH - 1] = '\0';
}

static void ApplyInserted(CFilePicker &picker, const std::string &path, int drive, bool close_modal, bool &modal)
{
    s_current_fdd[drive] = path;
    SyncConfigImage(drive, path);
    FDD_SetFD(drive, (char *)path.c_str(), 0);
    FDD_SetFDInt();
    RememberDiskDirectory(picker.current_dir);
    picker.SetStatus(std::string("FDD") + std::to_string(drive) + ": " + GetBasename(path));
    printf("[px68k] Inserted %s into FDD%d\n", path.c_str(), drive);
    (void)close_modal;
    (void)modal;
    picker.BeginDiskConfirm(GetBasename(path));
}

static void ShowDiskPopup()
{
    const int W = 420;
    const int H = 52;
    static uint16_t buf[W * H];
    FillBoxRGB565(buf, W, 0, 0, W, H, 0x10A4);
    DrawBoxRGB565(buf, W, 0, 0, W, H, 0xFFE0);
    std::string n0 = s_current_fdd[0].empty() ? std::string("없음") : GetBasename(s_current_fdd[0]);
    std::string n1 = s_current_fdd[1].empty() ? std::string("없음") : GetBasename(s_current_fdd[1]);
    std::string line0 = std::string("FDD0: ") + n0;
    std::string line1 = std::string("FDD1: ") + n1;
    DrawTextRGB565(buf, W, 8, 6, line0.c_str(), 0xFFFF, 0x10A4, false, W - 8, W, H);
    DrawTextRGB565(buf, W, 8, 26, line1.c_str(), 0xFFFF, 0x10A4, false, W - 8, W, H);
    CircleVideoSetOverlay(buf, (unsigned)W, (unsigned)H, 2000000);
}

static void MaybeFillEmptyFdd1(const std::string &zpath, uint32_t used_index)
{
    if (!s_current_fdd[1].empty()) return;
    auto zdisks = ListZipDisks(zpath);
    if (zdisks.size() < 2) return;
    for (const auto &zd : zdisks) {
        if (zd.file_index == used_index) continue;
        std::string ext2 = ExtractZipEntry(zpath, zd.file_index, "keropi/cache");
        if (ext2.empty()) return;
        s_current_fdd[1] = ext2;
        SyncConfigImage(1, ext2);
        FDD_SetFD(1, (char *)ext2.c_str(), 0);
        FDD_SetFDInt();
        printf("[px68k] Auto-inserted companion disk into FDD1: %s\n", ext2.c_str());
        return;
    }
}

// Returns false when the entry is a directory. The caller then switches the target drive.
static bool TryMountEntry(CFilePicker &picker, const FileEntry &entry, int drive, bool close_modal, bool &modal)
{
    if (entry.name == ".." || entry.is_dir) return false;

    if (!picker.current_zip_path.empty()) {
        size_t p1 = entry.full_path.find('#');
        size_t p2 = (p1 == std::string::npos) ? std::string::npos
                                             : entry.full_path.find('#', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            picker.SetStatus("Failed to read ZIP entry");
            return true;
        }
        std::string zpath = entry.full_path.substr(0, p1);
        std::string idxstr = entry.full_path.substr(p1 + 1, p2 - p1 - 1);
        char *end = nullptr;
        unsigned long v = strtoul(idxstr.c_str(), &end, 10);
        if (end == idxstr.c_str()) {
            picker.SetStatus("Failed to read ZIP entry");
            return true;
        }
        uint32_t fidx = (uint32_t)v;
        std::string extracted = ExtractZipEntry(zpath, fidx, "keropi/cache");
        if (extracted.empty()) {
            picker.SetStatus("Failed to extract disk from ZIP!");
            return true;
        }
        s_loaded_zip_path = zpath;
        if (drive == 0) MaybeFillEmptyFdd1(zpath, fidx);
        ApplyInserted(picker, extracted, drive, close_modal, modal);
        return true;
    }

    if (IsZipFilename(entry.name)) {
        auto zdisks = ListZipDisks(entry.full_path);
        if (zdisks.empty()) {
            picker.SetStatus("No supported X68000 disk found in ZIP!");
            return true;
        }
        s_loaded_zip_path = entry.full_path;
        if (zdisks.size() == 1) {
            std::string extracted = ExtractZipEntry(entry.full_path, zdisks[0].file_index, "keropi/cache");
            if (extracted.empty()) {
                picker.SetStatus("Failed to extract disk from ZIP!");
                return true;
            }
            ApplyInserted(picker, extracted, drive, close_modal, modal);
            return true;
        }
        RememberDiskDirectory(picker.current_dir);
        picker.EnterZip(entry.full_path);
        char msg[96];
        snprintf(msg, sizeof(msg), "ZIP has %d disks. Choose one to insert.", picker.num_disk_files);
        picker.SetStatus(msg);
        return true;
    }

    s_loaded_zip_path.clear();
    ApplyInserted(picker, entry.full_path, drive, close_modal, modal);
    return true;
}

static void MoveSelection(CFilePicker &picker, int delta, int max_visible)
{
    int total = (int)picker.entries.size();
    if (total <= 0) return;
    int next = picker.selected_idx + delta;
    if (next < 0) next = 0;
    if (next > total - 1) next = total - 1;
    picker.selected_idx = next;
    if (picker.selected_idx < picker.scroll_offset)
        picker.scroll_offset = picker.selected_idx;
    if (picker.selected_idx >= picker.scroll_offset + max_visible)
        picker.scroll_offset = picker.selected_idx - max_visible + 1;
}

static void ActivateSelection(CFilePicker &picker, bool &modal)
{
    int total = (int)picker.entries.size();
    if (picker.selected_idx < 0 || picker.selected_idx >= total) return;
    const FileEntry &entry = picker.entries[picker.selected_idx];
    if (entry.name == "..") {
        picker.NavigateToParent();
    } else if (entry.is_dir) {
        picker.EnterDirectory(entry.full_path);
    } else {
        TryMountEntry(picker, entry, picker.target_drive, true, modal);
    }
}

static void InsertSelection(CFilePicker &picker, int drive, bool &modal)
{
    int total = (int)picker.entries.size();
    bool mounted = false;
    if (picker.selected_idx >= 0 && picker.selected_idx < total)
        mounted = TryMountEntry(picker, picker.entries[picker.selected_idx], drive, false, modal);
    picker.target_drive = drive;
    if (!mounted) {
        picker.SetStatus(drive == 0 ? "Target switched to FDD0 (Drive A)"
                                    : "Target switched to FDD1 (Drive B)");
    }
}

static bool KeyEdge(const uint8_t *now, const uint8_t *prev, unsigned id)
{
    return id < RETROK_LAST && now[id] && !prev[id];
}

void CFilePicker::RunBaremetal()
{
    for (int d = 0; d < 2; d++) {
        if (s_current_fdd[d].empty() && Config.FDDImage[d][0])
            s_current_fdd[d] = Config.FDDImage[d];
    }
    if (current_dir.empty() || current_dir == "." || current_dir == "SD:")
        current_dir = "SD:/";

    printf("[px68k] Disk selector: %s\n", current_dir.c_str());
    nav_depth = 0;
    ScanDirectory();

    const int max_visible = 21;
    bool modal = true;
    uint8_t prev_key[RETROK_LAST];
    memset(prev_key, 0, sizeof(prev_key));
    for (unsigned id = 0; id < RETROK_LAST; id++)
        prev_key[id] = CircleInputGetState(0, RETRO_DEVICE_KEYBOARD, 0, id) ? 1 : 0;

    uint16_t prev_pad = 0;
    bool ignore_f12 = prev_key[RETROK_F12] != 0;
    uint32_t repeat_at = 0;

    while (modal) {
        uint8_t now_key[RETROK_LAST];
        memset(now_key, 0, sizeof(now_key));
        for (unsigned id = 0; id < RETROK_LAST; id++)
            now_key[id] = CircleInputGetState(0, RETRO_DEVICE_KEYBOARD, 0, id) ? 1 : 0;

        uint16_t pad = (uint16_t)CircleInputGetState(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
        bool alt = now_key[RETROK_LALT] || now_key[RETROK_RALT];
        uint32_t ms = DiskMenuNowMs();

        if (confirm_disk) {
            auto pad_edge = [&](unsigned bit) {
                uint16_t mask = (uint16_t)(1u << bit);
                return (pad & mask) && !(prev_pad & mask);
            };
            if (KeyEdge(now_key, prev_key, RETROK_LEFT) || KeyEdge(now_key, prev_key, RETROK_1) ||
                pad_edge(RETRO_DEVICE_ID_JOYPAD_LEFT))
                confirm_choice = 0;
            if (KeyEdge(now_key, prev_key, RETROK_RIGHT) || KeyEdge(now_key, prev_key, RETROK_2) ||
                pad_edge(RETRO_DEVICE_ID_JOYPAD_RIGHT))
                confirm_choice = 1;

            bool accept = KeyEdge(now_key, prev_key, RETROK_RETURN) ||
                          KeyEdge(now_key, prev_key, RETROK_KP_ENTER) ||
                          KeyEdge(now_key, prev_key, RETROK_SPACE) ||
                          pad_edge(RETRO_DEVICE_ID_JOYPAD_A) ||
                          pad_edge(RETRO_DEVICE_ID_JOYPAD_START);
            bool back = KeyEdge(now_key, prev_key, RETROK_ESCAPE) ||
                        pad_edge(RETRO_DEVICE_ID_JOYPAD_B);
            if (back || (accept && confirm_choice == 0)) {
                confirm_disk = false;
                ApplyCursor();
            } else if (accept && confirm_choice == 1) {
                confirm_disk = false;
                RememberCursor();
                WinX68k_Reset();
                modal = false;
                break;
            }

            RenderUI();
            CircleVideoUpdate(s_ui_buffer, 800, 600, 800 * sizeof(uint16_t));
            memcpy(prev_key, now_key, sizeof(prev_key));
            prev_pad = pad;
            CTimer::SimpleusDelay(16000);
            continue;
        }

        if (ignore_f12) {
            if (!now_key[RETROK_F12]) ignore_f12 = false;
        } else if (KeyEdge(now_key, prev_key, RETROK_F12) || KeyEdge(now_key, prev_key, RETROK_ESCAPE)) {
            modal = false;
        }

        bool nav_up = KeyEdge(now_key, prev_key, RETROK_UP);
        bool nav_down = KeyEdge(now_key, prev_key, RETROK_DOWN);
        if ((now_key[RETROK_UP] || now_key[RETROK_DOWN]) && ms >= repeat_at && repeat_at != 0) {
            if (now_key[RETROK_UP]) nav_up = true;
            if (now_key[RETROK_DOWN]) nav_down = true;
            repeat_at = ms + 80;
        } else if (nav_up || nav_down) {
            repeat_at = ms + 350;
        } else if (!now_key[RETROK_UP] && !now_key[RETROK_DOWN]) {
            repeat_at = 0;
        }

        if (nav_up) MoveSelection(*this, -1, max_visible);
        if (nav_down) MoveSelection(*this, 1, max_visible);
        if (KeyEdge(now_key, prev_key, RETROK_PAGEUP)) MoveSelection(*this, -max_visible, max_visible);
        if (KeyEdge(now_key, prev_key, RETROK_PAGEDOWN)) MoveSelection(*this, max_visible, max_visible);
        if (KeyEdge(now_key, prev_key, RETROK_HOME)) {
            selected_idx = 0;
            scroll_offset = 0;
        }
        if (KeyEdge(now_key, prev_key, RETROK_END)) {
            int total = (int)entries.size();
            selected_idx = total > 0 ? total - 1 : 0;
            scroll_offset = total > max_visible ? total - max_visible : 0;
        }
        if (KeyEdge(now_key, prev_key, RETROK_BACKSPACE) || KeyEdge(now_key, prev_key, RETROK_LEFT))
            NavigateToParent();
        if (KeyEdge(now_key, prev_key, RETROK_RETURN) || KeyEdge(now_key, prev_key, RETROK_KP_ENTER) ||
            KeyEdge(now_key, prev_key, RETROK_SPACE) || KeyEdge(now_key, prev_key, RETROK_RIGHT))
            ActivateSelection(*this, modal);

        if (KeyEdge(now_key, prev_key, RETROK_1)) {
            if (alt) InsertSelection(*this, 0, modal);
            else {
                target_drive = 0;
                SetStatus("Target switched to FDD0 (Drive A)");
            }
        }
        if (KeyEdge(now_key, prev_key, RETROK_2)) {
            if (alt) InsertSelection(*this, 1, modal);
            else {
                target_drive = 1;
                SetStatus("Target switched to FDD1 (Drive B)");
            }
        }
        if (alt && KeyEdge(now_key, prev_key, RETROK_s)) {
            SwapMountedFdds(true);
            SyncConfigImage(0, s_current_fdd[0]);
            SyncConfigImage(1, s_current_fdd[1]);
            SetStatus("Swapped FDD0 and FDD1");
        }
        if ((alt && KeyEdge(now_key, prev_key, RETROK_z)) || KeyEdge(now_key, prev_key, RETROK_F11))
            OpenLoadedZip();
        if (KeyEdge(now_key, prev_key, RETROK_e)) {
            FDD_EjectFD(target_drive);
            s_current_fdd[target_drive].clear();
            SyncConfigImage(target_drive, "");
            FDD_SetFDInt();
            char msg[64];
            snprintf(msg, sizeof(msg), "FDD%d Ejected", target_drive);
            SetStatus(msg);
        }
        if (KeyEdge(now_key, prev_key, RETROK_r)) {
            RememberCursor();
            WinX68k_Reset();
            modal = false;
            break;
        }

        auto pad_edge = [&](unsigned bit) {
            uint16_t mask = (uint16_t)(1u << bit);
            return (pad & mask) && !(prev_pad & mask);
        };
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_UP)) MoveSelection(*this, -1, max_visible);
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_DOWN)) MoveSelection(*this, 1, max_visible);
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_LEFT) || pad_edge(RETRO_DEVICE_ID_JOYPAD_B))
            NavigateToParent();
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_RIGHT) || pad_edge(RETRO_DEVICE_ID_JOYPAD_A) ||
            pad_edge(RETRO_DEVICE_ID_JOYPAD_START))
            ActivateSelection(*this, modal);
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_L) || pad_edge(RETRO_DEVICE_ID_JOYPAD_X))
            InsertSelection(*this, 0, modal);
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_R) || pad_edge(RETRO_DEVICE_ID_JOYPAD_Y))
            InsertSelection(*this, 1, modal);
        if (pad_edge(RETRO_DEVICE_ID_JOYPAD_SELECT))
            modal = false;

        if (!modal) break;

        RememberCursor();
        RenderUI();
        CircleVideoUpdate(s_ui_buffer, 800, 600, 800 * sizeof(uint16_t));
        memcpy(prev_key, now_key, sizeof(prev_key));
        prev_pad = pad;
        CTimer::SimpleusDelay(16000);
    }

    ShowDiskPopup();
    CircleVideoClear(0x0000);
}

extern "C" void Px68k_RunHostDiskMenu(void)
{
    s_picker.RunBaremetal();
}
