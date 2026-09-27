#pragma once
// Shared FDD file picker. Linux runs RunModal from x68k_sdl.cpp.
// Baremetal runs RunBaremetal from Circle/DiskMenuRun.cpp.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <sys/stat.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <algorithm>
#include <ctype.h>
#include <dirent.h>

#include "zip_helper.h"
#include "font8x16.h"

#ifdef _WIN32
#include <direct.h>
#ifndef PATH_SEP
#define PATH_SEP '\\'
#endif
#else
#include <unistd.h>
#ifndef PATH_SEP
#define PATH_SEP '/'
#endif
#endif

#ifndef ARM
#include <SDL2/SDL.h>
#endif

extern "C" {
void FDD_SetFD(int drive, const char *filename, int readonly);
void FDD_EjectFD(int drive);
void FDD_SetFDInt(void);
void WinX68k_Reset(void);
#ifdef ARM
void CircleVideoUpdate(const void *data, unsigned width, unsigned height, size_t pitch_bytes);
#endif
}

uint32_t DiskMenuNowMs();

extern std::string s_current_fdd[2];
extern std::string s_loaded_zip_path;

// Software RGB565 frame. One copy per host translation unit.
static uint16_t s_ui_buffer[800 * 600];

// "SD:/" and "C:\" stay intact. A trailing slash after a drive colon is the root.
static inline void TrimTrailingSlash(std::string &path) {
    while (path.length() > 1 && (path.back() == '/' || path.back() == '\\')) {
#ifdef _WIN32
        if (path.length() == 3 && path[1] == ':') break;
#endif
        if (path.size() >= 3 && path[path.size() - 2] == ':') break;
        path.pop_back();
    }
    if (path.size() >= 2 && path.back() == ':') path.push_back(PATH_SEP);
}

static inline bool IsDiskRoot(const std::string &path) {
    if (path.empty() || path == "/" || path == "\\") return true;
    size_t colon = path.find(':');
    if (colon == std::string::npos) return false;
    std::string rest = path.substr(colon + 1);
    while (!rest.empty() && (rest.back() == '/' || rest.back() == '\\')) rest.pop_back();
    return rest.empty();
}

static inline std::string GetBasename(const std::string &path)
{
    if (path.empty()) return "<Empty>";
    size_t slash = path.find_last_of("/\\");
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}

static inline void SwapMountedFdds(bool apply_hardware)
{
    std::string tmp = s_current_fdd[0];
    s_current_fdd[0] = s_current_fdd[1];
    s_current_fdd[1] = tmp;
    if (!apply_hardware) return;
    if (s_current_fdd[0].empty()) FDD_EjectFD(0);
    else FDD_SetFD(0, (char *)s_current_fdd[0].c_str(), 0);
    if (s_current_fdd[1].empty()) FDD_EjectFD(1);
    else FDD_SetFD(1, (char *)s_current_fdd[1].c_str(), 0);
    FDD_SetFDInt();
}

static const char *kLastDiskDirFile = "keropi/last_disk_dir.txt";

static inline bool IsDirectoryPath(const std::string &path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static inline std::string DirnameOf(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
#ifdef _WIN32
    if (slash == 2 && path[1] == ':') return path.substr(0, 3);
#endif
    std::string out = path.substr(0, slash);
    if (out.size() >= 2 && out.back() == ':') out.push_back(PATH_SEP);
    return out;
}

static inline std::string ResolveExistingDir(const std::string &path)
{
    if (!IsDirectoryPath(path)) return "";
#ifdef _WIN32
    char full[1024];
    if (_fullpath(full, path.c_str(), sizeof(full))) return full;
    return path;
#elif defined(ARM)
    return path;
#else
    char *resolved = realpath(path.c_str(), NULL);
    if (!resolved) return "";
    std::string out = resolved;
    free(resolved);
    return out;
#endif
}

static inline std::string LoadLastDiskDir()
{
    FILE *f = fopen(kLastDiskDirFile, "rb");
    if (!f) return "";
    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return "";
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = 0;
    }
    return ResolveExistingDir(buf);
}

// Remember the folder a disk or ZIP was chosen from. Cache extracts are ignored.
static inline void RememberDiskDirectory(const std::string &file_or_dir)
{
    if (file_or_dir.empty()) return;
    std::string dir = IsDirectoryPath(file_or_dir) ? file_or_dir : DirnameOf(file_or_dir);
    if (dir.empty()) return;
    if (dir.find("keropi/cache") != std::string::npos || dir.find("keropi\\cache") != std::string::npos) return;

    std::string resolved = ResolveExistingDir(dir);
    if (resolved.empty()) return;

#ifdef _WIN32
    _mkdir("keropi");
#else
    mkdir("keropi", 0755);
#endif
    FILE *f = fopen(kLastDiskDirFile, "wb");
    if (!f) return;
    fprintf(f, "%s\n", resolved.c_str());
    fclose(f);
    printf("[px68k] Saved disk directory: %s\n", resolved.c_str());
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

static inline bool HasDiskExtension(const char *name)
{
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot || !dot[1]) return false;
    static const char *exts[] = {
        ".xdf", ".dim", ".d88", ".88d", ".hdm", ".dup",
        ".2hd", ".img", ".hds", ".hdf", ".cmd", ".m3u",
        ".fd", ".fdi", ".zip", NULL
    };
    for (int i = 0; exts[i]; i++) {
        if (strcasecmp(dot, exts[i]) == 0) return true;
    }
    return false;
}

// Sort by name without copying each entry once per swap. The baremetal
// string type allocates on every assignment, so an insertion sort of a
// large folder stalls the menu.
static inline void SortFileEntries(std::vector<FileEntry> &items)
{
    const int n = (int)items.size();
    if (n < 2) return;
    int *idx = (int *)malloc((size_t)n * sizeof(int));
    if (!idx) return;
    for (int i = 0; i < n; i++) idx[i] = i;

    auto less = [&](int a, int b) {
        return strcasecmp(items[a].name.c_str(), items[b].name.c_str()) < 0;
    };
    auto sift = [&](int root, int count) {
        for (;;) {
            int child = root * 2 + 1;
            if (child >= count) break;
            if (child + 1 < count && less(idx[child], idx[child + 1])) child++;
            if (!less(idx[root], idx[child])) break;
            int tmp = idx[root];
            idx[root] = idx[child];
            idx[child] = tmp;
            root = child;
        }
    };
    for (int start = n / 2 - 1; start >= 0; --start) sift(start, n);
    for (int end = n - 1; end > 0; --end) {
        int tmp = idx[end];
        idx[end] = idx[0];
        idx[0] = tmp;
        sift(0, end);
    }

    std::vector<FileEntry> ordered;
    for (int i = 0; i < n; i++) ordered.push_back(items[idx[i]]);
    items = ordered;
    free(idx);
}

class CFilePicker {
public:
    std::string current_dir;
    std::string current_zip_path;
    std::vector<FileEntry> entries;
    int selected_idx;
    int scroll_offset;
    int target_drive; // 0 for FDD0, 1 for FDD1
    int num_subdirs;
    int num_disk_files;
    std::string status_msg;
    uint32_t status_timer;
    bool confirm_disk;
    int confirm_choice; // 0: back to the menu, 1: reset
    std::string confirm_label;
    struct CursorMark {
        std::string key;
        std::string name;
    };
    CursorMark cursor_mark[24];
    int cursor_mark_count;
    struct NavFrame {
        std::string name;
        int selected_idx;
        int scroll_offset;
    };
    NavFrame nav_stack[32];
    int nav_depth;

    CFilePicker() : selected_idx(0), scroll_offset(0), target_drive(0),
                    num_subdirs(0), num_disk_files(0), status_timer(0),
                    confirm_disk(false), confirm_choice(1), cursor_mark_count(0),
                    nav_depth(0) {
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
#elif !defined(ARM)
        char *resolved = realpath(current_dir.c_str(), NULL);
        if (resolved) {
            current_dir = resolved;
            free(resolved);
        }
#endif
        std::string saved = LoadLastDiskDir();
        if (!saved.empty()) {
            current_dir = saved;
            printf("[px68k] Restored disk directory: %s\n", current_dir.c_str());
        }
    }

    void SetStatus(const std::string &msg) {
        status_msg = msg;
        status_timer = DiskMenuNowMs() + 3000;
    }

    void BeginDiskConfirm(const std::string &name) {
        RememberCursor();
        confirm_disk = true;
        confirm_choice = 1;
        confirm_label = name;
    }

    std::string LocationKey() const {
        if (!current_zip_path.empty()) return std::string("zip:") + current_zip_path;
        std::string key = current_dir;
        TrimTrailingSlash(key);
        return key;
    }

    void SaveCursor(const std::string &key, const std::string &name) {
        if (key.empty() || name.empty()) return;
        for (int i = 0; i < cursor_mark_count; i++) {
            if (cursor_mark[i].key == key) {
                cursor_mark[i].name = name;
                return;
            }
        }
        if (cursor_mark_count < 24) {
            cursor_mark[cursor_mark_count].key = key;
            cursor_mark[cursor_mark_count].name = name;
            cursor_mark_count++;
            return;
        }
        for (int i = 1; i < 24; i++) cursor_mark[i - 1] = cursor_mark[i];
        cursor_mark[23].key = key;
        cursor_mark[23].name = name;
    }

    void RememberCursor() {
        if (selected_idx < 0 || selected_idx >= (int)entries.size()) return;
        SaveCursor(LocationKey(), entries[selected_idx].name);
    }

    void ApplyCursor() {
        std::string key = LocationKey();
        std::string name;
        for (int i = 0; i < cursor_mark_count; i++) {
            if (cursor_mark[i].key == key) {
                name = cursor_mark[i].name;
                break;
            }
        }
        if (name.empty()) return;
        int total = (int)entries.size();
        for (int i = 0; i < total; i++) {
            if (entries[i].name != name) continue;
            selected_idx = i;
            const int max_visible = 21;
            if (selected_idx < scroll_offset || selected_idx >= scroll_offset + max_visible) {
                scroll_offset = selected_idx - max_visible / 2;
                if (scroll_offset < 0) scroll_offset = 0;
                int max_off = total > max_visible ? total - max_visible : 0;
                if (scroll_offset > max_off) scroll_offset = max_off;
            }
            return;
        }
    }

    void PushCursor() {
        if (nav_depth >= 32) return;
        if (selected_idx < 0 || selected_idx >= (int)entries.size()) return;
        nav_stack[nav_depth].name = entries[selected_idx].name;
        nav_stack[nav_depth].selected_idx = selected_idx;
        nav_stack[nav_depth].scroll_offset = scroll_offset;
        nav_depth++;
    }

    void PopCursor() {
        if (nav_depth <= 0) return;
        nav_depth--;
        int idx = nav_stack[nav_depth].selected_idx;
        int scroll = nav_stack[nav_depth].scroll_offset;
        const std::string name = nav_stack[nav_depth].name;
        int total = (int)entries.size();
        if (idx < 0 || idx >= total || entries[idx].name != name) {
            idx = -1;
            for (int i = 0; i < total; i++) {
                if (entries[i].name == name) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0) return;
        selected_idx = idx;
        const int max_visible = 21;
        int max_off = total > max_visible ? total - max_visible : 0;
        if (scroll < 0) scroll = 0;
        if (scroll > max_off) scroll = max_off;
        if (idx < scroll || idx >= scroll + max_visible)
            scroll = idx < max_visible / 2 ? 0 : idx - max_visible / 2;
        if (scroll > max_off) scroll = max_off;
        scroll_offset = scroll;
    }

    void EnterDirectory(const std::string &path) {
        PushCursor();
        RememberCursor();
        current_dir = path;
        current_zip_path.clear();
        ScanDirectory();
    }

    void EnterZip(const std::string &path) {
        PushCursor();
        RememberCursor();
        current_zip_path = path;
        ScanZipArchive();
    }

    void ScanZipArchive() {
        entries.clear();
        selected_idx = 0;
        scroll_offset = 0;

        // Parent directory ".." to exit ZIP archive
        FileEntry parent;
        parent.name = "..";
        parent.full_path = "";
        parent.is_dir = true;
        parent.size = 0;
        entries.push_back(parent);

        auto zip_disks = ListZipDisks(current_zip_path);
        for (const auto &zd : zip_disks) {
            FileEntry fe;
            fe.name = zd.name;
            fe.full_path = current_zip_path + "#" + std::to_string(zd.file_index) + "#" + zd.name;
            fe.is_dir = false;
            fe.size = zd.size;
            entries.push_back(fe);
        }

        num_subdirs = 0;
        num_disk_files = (int)zip_disks.size();

        // Default selection to currently mounted disk or first disk (index 1)
        if (entries.size() > 1) {
            selected_idx = 1;
            std::string curDisk = GetBasename(s_current_fdd[target_drive]);
            for (size_t i = 1; i < entries.size(); i++) {
                if (entries[i].name == curDisk) {
                    selected_idx = (int)i;
                    break;
                }
            }
        }

        printf("[px68k] Scanned ZIP '%s': %d disks\n", current_zip_path.c_str(), num_disk_files);
        ApplyCursor();
    }

    void PresentNow() {
#ifdef ARM
        RenderUI();
        CircleVideoUpdate(s_ui_buffer, 800, 600, 800 * sizeof(uint16_t));
#endif
    }

    void ScanDirectory() {
#ifdef ARM
        SetStatus("Scanning...");
        PresentNow();
#endif
        current_zip_path.clear();
        entries.clear();
        selected_idx = 0;
        scroll_offset = 0;

        TrimTrailingSlash(current_dir);
        bool is_root = IsDiskRoot(current_dir);

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
            int seen = 0;
            while ((de = readdir(dir)) != NULL) {
                // Ignore dot and hidden files/directories (starting with '.')
                if (de->d_name[0] == '.') continue;
                seen++;
#ifdef ARM
                if ((seen & 31) == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Scanning... %d", seen);
                    SetStatus(msg);
                    PresentNow();
                }
#endif

                bool is_dir_ent = false;
                uint64_t sz = 0;
#ifdef ARM
                // readdir already reported the FAT attribute and size.
                // stat() walks the whole directory again, so a large folder
                // freezes the menu.
                if (de->d_type == DT_DIR) {
                    is_dir_ent = true;
                } else if (de->d_type == DT_REG) {
                    if (!HasDiskExtension(de->d_name)) continue;
                    sz = (uint64_t)de->d_size;
                } else {
                    continue;
                }
#else
#ifndef _WIN32
                if (de->d_type == DT_REG && !HasDiskExtension(de->d_name)) continue;
#endif
                std::string probe = current_dir;
                if (!probe.empty() && probe.back() != '/' && probe.back() != '\\') probe += PATH_SEP;
                probe += de->d_name;
                struct stat st;
                memset(&st, 0, sizeof(st));
                bool have = stat(probe.c_str(), &st) == 0;
                is_dir_ent = have && S_ISDIR(st.st_mode);
#ifndef _WIN32
                if (!have && de->d_type == DT_DIR) {
                    have = true;
                    is_dir_ent = true;
                }
#endif
                if (!have) continue;
                if (!is_dir_ent && !HasDiskExtension(de->d_name)) continue;
                if (!is_dir_ent) sz = (uint64_t)st.st_size;
#endif

                std::string full = current_dir;
                if (!full.empty() && full.back() != '/' && full.back() != '\\') {
                    full += PATH_SEP;
                }
                full += de->d_name;

                FileEntry fe;
                fe.name = de->d_name;
                fe.full_path = full;
                fe.is_dir = is_dir_ent;
                fe.size = is_dir_ent ? 0 : sz;
                if (is_dir_ent) dirs.push_back(fe);
                else files.push_back(fe);
            }
            closedir(dir);
#ifdef ARM
            status_msg.clear();
            status_timer = 0;
#endif
        } else {
            SetStatus("Cannot open directory");
            printf("[px68k] Cannot open directory: %s\n", current_dir.c_str());
        }

        SortFileEntries(dirs);
        SortFileEntries(files);

        num_subdirs = (int)dirs.size();
        num_disk_files = (int)files.size();

        for (auto &d : dirs) entries.push_back(d);
        for (auto &f : files) entries.push_back(f);

        printf("[px68k] Scanned '%s': %d subdirs, %d disks/archives (total %zu entries)\n",
               current_dir.c_str(), num_subdirs, num_disk_files, entries.size());
        ApplyCursor();
    }

    void NavigateToParent() {
        if (!current_zip_path.empty()) {
            current_zip_path.clear();
            ScanDirectory();
            PopCursor();
            return;
        }

        TrimTrailingSlash(current_dir);
        if (IsDiskRoot(current_dir)) return;
        size_t last_sep = current_dir.find_last_of("/\\");
        if (last_sep != std::string::npos) {
            if (last_sep == 0) current_dir = "/";
#ifdef _WIN32
            else if (last_sep == 2 && current_dir[1] == ':') current_dir = current_dir.substr(0, 3);
#endif
            else current_dir = current_dir.substr(0, last_sep);
        }
        TrimTrailingSlash(current_dir);
        ScanDirectory();
        PopCursor();
    }

    // Open the disk list of the ZIP the running game was loaded from.
    // Returns false when there is no ZIP, or it contains no disk images.
    bool OpenLoadedZip() {
        if (s_loaded_zip_path.empty()) {
            SetStatus("No game ZIP loaded");
            return false;
        }
        auto disks = ListZipDisks(s_loaded_zip_path);
        if (disks.empty()) {
            SetStatus("No disk images inside the game ZIP");
            return false;
        }
        EnterZip(s_loaded_zip_path);
        char msg[128];
        snprintf(msg, sizeof(msg), "Game ZIP: %d disk image(s). ALT+1/2 insert, Enter loads.", num_disk_files);
        SetStatus(msg);
        return true;
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

        // Currently loaded disks. Each name keeps its own half of the bar so a
        // long Hangul title cannot push the other drive off the screen.
        std::string cur0 = GetBasename(s_current_fdd[0]);
        std::string cur1 = GetBasename(s_current_fdd[1]);
        const char *n0 = cur0.empty() ? "-" : cur0.c_str();
        const char *n1 = cur1.empty() ? "-" : cur1.c_str();
        int x = DrawTextRGB565(s_ui_buffer, 800, 20, 46, "FDD0: ", 0xCE79, 0x10A4, false, 390);
        DrawTextRGB565(s_ui_buffer, 800, x, 46, n0, 0xCE79, 0x10A4, false, 390);
        x = DrawTextRGB565(s_ui_buffer, 800, 408, 46, "FDD1: ", 0xCE79, 0x10A4, false, 790);
        DrawTextRGB565(s_ui_buffer, 800, x, 46, n1, 0xCE79, 0x10A4, false, 790);

        // Directory Path Bar (Y: 72..96)
        FillBoxRGB565(s_ui_buffer, 800, 10, 72, 780, 24, 0x18C6);
        DrawBoxRGB565(s_ui_buffer, 800, 10, 72, 780, 24, 0x4228);

        const bool show_game_zip = !s_loaded_zip_path.empty() && current_zip_path != s_loaded_zip_path;
        const int path_limit = show_game_zip ? 582 : 698;
        if (!current_zip_path.empty()) {
            std::string zname = GetBasename(current_zip_path);
            x = DrawTextRGB565(s_ui_buffer, 800, 18, 76, "ZIP: ", 0xFD20, 0x18C6, false, path_limit);
            x = DrawTextRGB565(s_ui_buffer, 800, x, 76, zname.c_str(), 0xFD20, 0x18C6, false, path_limit - 176);
            char tail[64];
            snprintf(tail, sizeof(tail), "  [%d disks inside]", num_disk_files);
            DrawTextRGB565(s_ui_buffer, 800, x, 76, tail, 0xFD20, 0x18C6, false, path_limit);
        } else {
            x = DrawTextRGB565(s_ui_buffer, 800, 18, 76, "Path: ", 0xFFE0, 0x18C6, false, path_limit);
            x = DrawTextRGB565(s_ui_buffer, 800, x, 76, current_dir.c_str(), 0xFFE0, 0x18C6, false, path_limit - 216);
            char tail[64];
            snprintf(tail, sizeof(tail), "  [%d subdirs, %d disks]", num_subdirs, num_disk_files);
            DrawTextRGB565(s_ui_buffer, 800, x, 76, tail, 0xFFE0, 0x18C6, false, path_limit);
        }

        // [ GAME ZIP ] button if a game ZIP is loaded and not currently viewing it
        if (show_game_zip) {
            FillBoxRGB565(s_ui_buffer, 800, 590, 74, 110, 20, 0x4180);
            DrawBoxRGB565(s_ui_buffer, 800, 590, 74, 110, 20, 0xFD20);
            DrawTextRGB565(s_ui_buffer, 800, 596, 76, "[ GAME ZIP ]", 0xFFFF, 0x4180);
        }

        // Clickable [ UP .. ] / [ EXIT ] button on Directory Bar
        FillBoxRGB565(s_ui_buffer, 800, 705, 74, 80, 20, 0x2A69);
        DrawBoxRGB565(s_ui_buffer, 800, 705, 74, 80, 20, 0x07FF);
        const char *upBtnText = !current_zip_path.empty() ? "[ EXIT ]" : "[ UP .. ]";
        DrawTextRGB565(s_ui_buffer, 800, 713, 76, upBtnText, 0xFFFF, 0x2A69);

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
                    if (!current_zip_path.empty()) {
                        DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, "[UP-ZIP] ..  (Exit ZIP Archive)", is_sel ? 0xFFFF : 0xFFE0, bg);
                        DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, "[ EXIT ]", is_sel ? 0xFFFF : 0x07FF, bg);
                    } else {
                        DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, "[UP-DIR] ..  (Parent Directory)", is_sel ? 0xFFFF : 0xFFE0, bg);
                        DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, "[ GO UP ]", is_sel ? 0xFFFF : 0x07FF, bg);
                    }
                } else if (e.is_dir) {
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, "[DIR]   ", is_sel ? 0xFFFF : 0x07FF, bg);
                    std::string dname = e.name + "/";
                    DrawTextRGB565(s_ui_buffer, 800, 84, y_pos, dname.c_str(), is_sel ? 0xFFFF : 0xFFE0, bg, false, 668);
                    DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, "[ OPEN > ]", is_sel ? 0xFFFF : 0x07E0, bg);
                } else if (!current_zip_path.empty()) {
                    // Item inside a ZIP archive!
                    bool in_fdd0 = (!s_current_fdd[0].empty() && GetBasename(s_current_fdd[0]) == e.name);
                    bool in_fdd1 = (!s_current_fdd[1].empty() && GetBasename(s_current_fdd[1]) == e.name);

                    uint16_t tag_color = in_fdd0 ? 0x07E0 : (in_fdd1 ? 0x07FF : 0xFFE0);
                    const char *tag = in_fdd0 ? "[FDD 0] " : (in_fdd1 ? "[FDD 1] " : "[DISK ] ");
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, tag, is_sel ? 0xFFFF : tag_color, bg);

                    DrawTextRGB565(s_ui_buffer, 800, 84, y_pos, e.name.c_str(), 0xFFFF, bg, false, 668);

                    char actionStr[32];
                    if (in_fdd0) {
                        snprintf(actionStr, sizeof(actionStr), "[ IN FDD0 ]");
                    } else if (in_fdd1) {
                        snprintf(actionStr, sizeof(actionStr), "[ IN FDD1 ]");
                    } else {
                        snprintf(actionStr, sizeof(actionStr), "[ -> FDD%d ]", target_drive);
                    }
                    uint16_t act_color = in_fdd0 ? 0x07E0 : (in_fdd1 ? 0x07FF : 0x07E0);
                    DrawTextRGB565(s_ui_buffer, 800, 675, y_pos, actionStr, is_sel ? 0xFFFF : act_color, bg);
                } else {
                    bool is_zip = IsZipFilename(e.name);
                    bool is_hdd = (e.name.find(".hdf") != std::string::npos || e.name.find(".hds") != std::string::npos);
                    uint16_t tag_color = is_zip ? 0xFD20 : (is_hdd ? 0x07E0 : 0x07FF);
                    const char *tag = is_zip ? "[ZIP]   " : (is_hdd ? "[HDD]   " : "[FDD]   ");
                    DrawTextRGB565(s_ui_buffer, 800, 20, y_pos, tag, is_sel ? 0xFFFF : tag_color, bg);

                    DrawTextRGB565(s_ui_buffer, 800, 84, y_pos, e.name.c_str(), 0xFFFF, bg, false, 668);

                    // File size or action
                    char sizeStr[32];
                    if (is_zip) {
                        snprintf(sizeStr, sizeof(sizeStr), "[ VIEW > ]");
                    } else if (e.size >= 1024 * 1024) {
                        snprintf(sizeStr, sizeof(sizeStr), "%5.1f MB", (double)e.size / (1024.0 * 1024.0));
                    } else {
                        snprintf(sizeStr, sizeof(sizeStr), "%5u KB", (unsigned)(e.size / 1024));
                    }
                    DrawTextRGB565(s_ui_buffer, 800, 680, y_pos, sizeStr, is_sel ? 0xFFFF : (is_zip ? 0x07E0 : 0x9CE7), bg);
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
        if (DiskMenuNowMs() < status_timer && !status_msg.empty()) {
            FillBoxRGB565(s_ui_buffer, 800, 150, 485, 500, 30, 0x2124);
            DrawBoxRGB565(s_ui_buffer, 800, 150, 485, 500, 30, 0x07FF);
            DrawTextRGB565(s_ui_buffer, 800, 170, 492, status_msg.c_str(), 0xFFFF, 0x2124, false, 640);
        }

        // Footer Bar (Y: 532..596)
        FillBoxRGB565(s_ui_buffer, 800, 0, 532, 800, 68, 0x10A4);
        DrawBoxRGB565(s_ui_buffer, 800, 0, 532, 800, 68, 0x31C8);

        // Action Buttons (Y: 536..558)
        // 1. [ LOAD/SELECT ] (x: 12..120, w: 108)
        FillBoxRGB565(s_ui_buffer, 800, 12, 536, 108, 22, 0x0288);
        DrawBoxRGB565(s_ui_buffer, 800, 12, 536, 108, 22, 0x07E0);
        DrawTextRGB565(s_ui_buffer, 800, 15, 539, "[ SELECT/LOAD ]", 0xFFFF, 0x0288);

        // 2. [ UP .. ] / [ EXIT ] (x: 125..205, w: 80)
        FillBoxRGB565(s_ui_buffer, 800, 125, 536, 80, 22, 0x2128);
        DrawBoxRGB565(s_ui_buffer, 800, 125, 536, 80, 22, 0x07FF);
        DrawTextRGB565(s_ui_buffer, 800, 131, 539, !current_zip_path.empty() ? "[ EXIT ]" : "[ UP .. ]", 0xFFFF, 0x2128);

        // 3. [ FDD 0 ] (x: 210..290, w: 80)
        uint16_t d0_bg = (target_drive == 0) ? 0x035D : 0x18C6;
        FillBoxRGB565(s_ui_buffer, 800, 210, 536, 80, 22, d0_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 210, 536, 80, 22, (target_drive == 0) ? 0xFFFF : 0x4228);
        DrawTextRGB565(s_ui_buffer, 800, 219, 539, "[ FDD 0 ]", (target_drive == 0) ? 0xFFE0 : 0xCE79, d0_bg);

        // 4. [ FDD 1 ] (x: 295..375, w: 80)
        uint16_t d1_bg = (target_drive == 1) ? 0x035D : 0x18C6;
        FillBoxRGB565(s_ui_buffer, 800, 295, 536, 80, 22, d1_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 295, 536, 80, 22, (target_drive == 1) ? 0xFFFF : 0x4228);
        DrawTextRGB565(s_ui_buffer, 800, 304, 539, "[ FDD 1 ]", (target_drive == 1) ? 0xFFE0 : 0xCE79, d1_bg);

        // 5. [ SWAP 0<->1 ] (x: 380..485, w: 105)
        FillBoxRGB565(s_ui_buffer, 800, 380, 536, 105, 22, 0x4180);
        DrawBoxRGB565(s_ui_buffer, 800, 380, 536, 105, 22, 0xFD20);
        DrawTextRGB565(s_ui_buffer, 800, 386, 539, "[ SWAP 0<->1 ]", 0xFFFF, 0x4180);

        // 6. [ EJECT ] (x: 490..565, w: 75)
        FillBoxRGB565(s_ui_buffer, 800, 490, 536, 75, 22, 0x3100);
        DrawBoxRGB565(s_ui_buffer, 800, 490, 536, 75, 22, 0xF800);
        DrawTextRGB565(s_ui_buffer, 800, 498, 539, "[ EJECT ]", 0xFFFF, 0x3100);

        // 7. [ RESET ] (x: 570..645, w: 75)
        FillBoxRGB565(s_ui_buffer, 800, 570, 536, 75, 22, 0x3180);
        DrawBoxRGB565(s_ui_buffer, 800, 570, 536, 75, 22, 0xFD20);
        DrawTextRGB565(s_ui_buffer, 800, 578, 539, "[ RESET ]", 0xFFFF, 0x3180);

        // 8. [ CLOSE/ESC ] (x: 675..785, w: 110)
        FillBoxRGB565(s_ui_buffer, 800, 675, 536, 110, 22, 0x2124);
        DrawBoxRGB565(s_ui_buffer, 800, 675, 536, 110, 22, 0x8410);
        DrawTextRGB565(s_ui_buffer, 800, 678, 539, "[ CLOSE/ESC ]", 0xFFFF, 0x2124);

        // Hint lines
        DrawTextRGB565(s_ui_buffer, 800, 20, 563,
                       "Keys: [ALT+1]: FDD0 | [ALT+2]: FDD1 | [ALT+S]: Swap | [ALT+Z]: Game ZIP",
                       0xCE79, 0x10A4);
        DrawTextRGB565(s_ui_buffer, 800, 20, 579,
                       "      [F11]: Quick Disk Swap | Enter: Load | Backspace: Parent | Esc: Resume Game",
                       0x8410, 0x10A4);

        if (!confirm_disk) return;

        FillBoxRGB565(s_ui_buffer, 800, 110, 168, 580, 200, 0x0842);
        DrawBoxRGB565(s_ui_buffer, 800, 110, 168, 580, 200, 0x07FF);
        DrawTextRGB565(s_ui_buffer, 800, 130, 184, "디스크를 넣었습니다", 0xFFE0, 0x0842);
        DrawTextRGB565(s_ui_buffer, 800, 130, 210, confirm_label.c_str(), 0xFFFF, 0x0842, false, 660);
        DrawTextRGB565(s_ui_buffer, 800, 130, 240, "메뉴로 돌아갈까요, 리셋할까요?", 0x07FF, 0x0842);

        uint16_t menu_bg = (confirm_choice == 0) ? 0x0288 : 0x2124;
        uint16_t menu_fg = (confirm_choice == 0) ? 0xFFFF : 0xCE79;
        uint16_t menu_bd = (confirm_choice == 0) ? 0x07E0 : 0x4228;
        FillBoxRGB565(s_ui_buffer, 800, 140, 278, 250, 36, menu_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 140, 278, 250, 36, menu_bd);
        DrawTextRGB565(s_ui_buffer, 800, 160, 288, "[ 메뉴로 돌아가기 ]", menu_fg, menu_bg);

        uint16_t reset_bg = (confirm_choice == 1) ? 0x4000 : 0x2124;
        uint16_t reset_fg = (confirm_choice == 1) ? 0xFFFF : 0xCE79;
        uint16_t reset_bd = (confirm_choice == 1) ? 0xF800 : 0x4228;
        FillBoxRGB565(s_ui_buffer, 800, 420, 278, 240, 36, reset_bg);
        DrawBoxRGB565(s_ui_buffer, 800, 420, 278, 240, 36, reset_bd);
        DrawTextRGB565(s_ui_buffer, 800, 490, 288, "[ 리셋 ]", reset_fg, reset_bg);

        DrawTextRGB565(s_ui_buffer, 800, 130, 332, "← → 선택    Enter 결정    Esc 메뉴", 0x8410, 0x0842);
    }

#ifndef ARM
    bool RunModal(SDL_Window *win, SDL_Renderer *rend, SDL_Texture *&tex,
                  std::string &out_path, int &out_drive, bool is_startup = false,
                  bool open_game_zip = false);
#endif
    void RunBaremetal();
};

extern CFilePicker s_picker;
