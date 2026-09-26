#include "zip_helper.h"
#include "miniz.h"
#include "miniz_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR_ONE(p) _mkdir(p)
#else
#define MKDIR_ONE(p) mkdir(p, 0755)
#endif

static bool MakeDirectories(const std::string &path)
{
    if (path.empty()) return false;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path.c_str());
    size_t len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[len - 1] = 0;
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char orig = *p;
            *p = 0;
            MKDIR_ONE(tmp);
            *p = orig;
        }
    }
    MKDIR_ONE(tmp);
    return true;
}

bool IsZipFilename(const std::string &filename)
{
    const char *dot = strrchr(filename.c_str(), '.');
    if (!dot) return false;
    return (strcasecmp(dot, ".zip") == 0);
}

bool IsDiskFilename(const std::string &filename)
{
    const char *dot = strrchr(filename.c_str(), '.');
    if (!dot) return false;
    return (strcasecmp(dot, ".xdf") == 0 || strcasecmp(dot, ".dim") == 0 ||
            strcasecmp(dot, ".d88") == 0 || strcasecmp(dot, ".88d") == 0 ||
            strcasecmp(dot, ".hdm") == 0 || strcasecmp(dot, ".dup") == 0 ||
            strcasecmp(dot, ".2hd") == 0 || strcasecmp(dot, ".img") == 0 ||
            strcasecmp(dot, ".hds") == 0 || strcasecmp(dot, ".hdf") == 0 ||
            strcasecmp(dot, ".cmd") == 0 || strcasecmp(dot, ".m3u") == 0 ||
            strcasecmp(dot, ".fd")  == 0 || strcasecmp(dot, ".fdi") == 0);
}

static std::string GetZipStem(const std::string &zip_path)
{
    const char *p = zip_path.c_str();
    const char *slash1 = strrchr(p, '/');
    const char *slash2 = strrchr(p, '\\');
    const char *start = p;
    if (slash1 && slash1 + 1 > start) start = slash1 + 1;
    if (slash2 && slash2 + 1 > start) start = slash2 + 1;
    std::string stem = start;
    const char *dot = strrchr(stem.c_str(), '.');
    if (dot) {
        stem = stem.substr(0, (size_t)(dot - stem.c_str()));
    }
    return stem;
}

std::vector<ZipEntryInfo> ListZipDisks(const std::string &zip_path)
{
    std::vector<ZipEntryInfo> result;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        return result;
    }

    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st)) {
            if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

            std::string fname = st.m_filename;
            if (strstr(fname.c_str(), "__MACOSX") != NULL) continue;

            const char *p = fname.c_str();
            const char *slash1 = strrchr(p, '/');
            const char *slash2 = strrchr(p, '\\');
            const char *bname = p;
            if (slash1 && slash1 + 1 > bname) bname = slash1 + 1;
            if (slash2 && slash2 + 1 > bname) bname = slash2 + 1;

            if (bname[0] == 0 || bname[0] == '.') continue;

            if (IsDiskFilename(bname)) {
                ZipEntryInfo entry;
                entry.name = bname;
                entry.size = st.m_uncomp_size;
                entry.file_index = i;
                result.push_back(entry);
            }
        }
    }

    // Sort alphabetically by filename
    for (size_t i = 0; i < result.size(); i++) {
        for (size_t j = i + 1; j < result.size(); j++) {
            if (strcasecmp(result[i].name.c_str(), result[j].name.c_str()) > 0) {
                ZipEntryInfo tmp = result[i];
                result[i] = result[j];
                result[j] = tmp;
            }
        }
    }

    mz_zip_reader_end(&zip);
    return result;
}

std::string ExtractZipEntry(const std::string &zip_path, uint32_t file_index, const std::string &cache_dir)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        fprintf(stderr, "[zip] Failed to open zip file '%s'\n", zip_path.c_str());
        return "";
    }

    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, file_index, &st)) {
        mz_zip_reader_end(&zip);
        return "";
    }

    std::string fname = st.m_filename;
    const char *p = fname.c_str();
    const char *slash1 = strrchr(p, '/');
    const char *slash2 = strrchr(p, '\\');
    const char *bname = p;
    if (slash1 && slash1 + 1 > bname) bname = slash1 + 1;
    if (slash2 && slash2 + 1 > bname) bname = slash2 + 1;

    std::string stem = GetZipStem(zip_path);
    std::string target_dir = cache_dir + "/" + stem;
    MakeDirectories(target_dir);

    std::string out_path = target_dir + "/" + bname;

    // Check if target file already exists with identical size
    struct stat exist_st;
    if (stat(out_path.c_str(), &exist_st) == 0 && (uint64_t)exist_st.st_size == st.m_uncomp_size) {
        printf("[zip] Cache hit: '%s' (%llu bytes)\n", out_path.c_str(), (unsigned long long)st.m_uncomp_size);
        mz_zip_reader_end(&zip);
        return out_path;
    }

    printf("[zip] Extracting '%s' -> '%s' (%llu bytes)...\n",
           fname.c_str(), out_path.c_str(), (unsigned long long)st.m_uncomp_size);

    mz_bool ok = mz_zip_reader_extract_to_file(&zip, file_index, out_path.c_str(), 0);
    mz_zip_reader_end(&zip);

    if (!ok) {
        fprintf(stderr, "[zip] Extraction failed for '%s'\n", fname.c_str());
        return "";
    }

    return out_path;
}

std::vector<std::string> ExtractAllDisksFromZip(const std::string &zip_path, const std::string &cache_dir)
{
    std::vector<std::string> result;
    auto entries = ListZipDisks(zip_path);
    for (size_t i = 0; i < entries.size(); i++) {
        std::string p = ExtractZipEntry(zip_path, entries[i].file_index, cache_dir);
        if (!p.empty()) {
            result.push_back(p);
        }
    }
    return result;
}
