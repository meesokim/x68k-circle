#ifndef ZIP_HELPER_H
#define ZIP_HELPER_H

#include <string>
#include <vector>
#include <stdint.h>

struct ZipEntryInfo {
    std::string name;
    uint64_t size;
    uint32_t file_index;
};

// Check if filename has .zip extension
bool IsZipFilename(const std::string &filename);

// Check if filename has supported X68000 disk/rom extension
bool IsDiskFilename(const std::string &filename);

// List all disk images contained inside a ZIP archive
std::vector<ZipEntryInfo> ListZipDisks(const std::string &zip_path);

// Extract a single entry from a ZIP archive by its zip file_index
std::string ExtractZipEntry(const std::string &zip_path, uint32_t file_index, const std::string &cache_dir = "keropi/cache");

// Extract all disk images found in a ZIP archive
std::vector<std::string> ExtractAllDisksFromZip(const std::string &zip_path, const std::string &cache_dir = "keropi/cache");

#endif // ZIP_HELPER_H
