#ifndef _DIRENT_H_
#define _DIRENT_H_

#if defined(__linux__) || defined(_WIN32)
#include_next <dirent.h>
#else

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    long           d_ino;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};

#ifndef FF_DEFINED
typedef void DIR;
#endif

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif // !__linux__ && !_WIN32

#endif // _DIRENT_H_
