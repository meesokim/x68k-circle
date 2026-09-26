#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <fatfs/ff.h>
#include "dirent.h"

// Forward declaration of Circle timer delay to avoid pulling in circle/types.h
class CTimer {
public:
    static void SimpleusDelay(unsigned nMicroSeconds);
    static unsigned GetClockTicks(void);
};

#define MAX_FATFS_FDS 32

struct FatFsFd {
    bool in_use;
    FIL fil;
};

static struct FatFsFd s_fatfs_fds[MAX_FATFS_FDS];
static volatile int s_fdTableLock = 0;

static void FdTableLock(void) {
    while (__sync_lock_test_and_set(&s_fdTableLock, 1)) {
    }
}

static void FdTableUnlock(void) {
    __sync_lock_release(&s_fdTableLock);
}

extern "C" {

void LogOutput(const char *str, size_t len) {
    (void)str;
    (void)len;
}

int _open(const char *name, int flags, int mode) {
    (void)mode;
    if (!name || !*name) {
        errno = ENOENT;
        return -1;
    }

    FdTableLock();
    int fd_idx = -1;
    for (int i = 0; i < MAX_FATFS_FDS; i++) {
        if (!s_fatfs_fds[i].in_use) {
            fd_idx = i;
            s_fatfs_fds[i].in_use = true;
            break;
        }
    }
    FdTableUnlock();

    if (fd_idx < 0) {
        errno = EMFILE;
        return -1;
    }

    char fatPath[512];
    if (strchr(name, ':') != nullptr) {
        strncpy(fatPath, name, sizeof(fatPath) - 1);
    } else if (name[0] == '/') {
        snprintf(fatPath, sizeof(fatPath), "SD:%s", name);
    } else if (strncmp(name, "./", 2) == 0) {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", name + 2);
    } else {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", name);
    }
    fatPath[sizeof(fatPath) - 1] = '\0';

    BYTE fatFlags = 0;
    int accMode = (flags & (O_RDONLY | O_WRONLY | O_RDWR));
    if (accMode == O_WRONLY) fatFlags |= FA_WRITE;
    else if (accMode == O_RDWR) fatFlags |= (FA_READ | FA_WRITE);
    else fatFlags |= FA_READ;

    if (flags & O_CREAT) {
        fatFlags |= FA_WRITE;
        if (flags & O_TRUNC) {
            fatFlags |= FA_CREATE_ALWAYS;
        } else if (flags & O_APPEND) {
            fatFlags |= FA_OPEN_APPEND;
        } else {
            fatFlags |= FA_OPEN_ALWAYS;
        }
    } else if (flags & O_TRUNC) {
        fatFlags |= (FA_WRITE | FA_CREATE_ALWAYS);
    } else {
        fatFlags |= FA_OPEN_EXISTING;
    }

    FRESULT res = f_open(&s_fatfs_fds[fd_idx].fil, fatPath, fatFlags);
    if (res != FR_OK) {
        res = f_open(&s_fatfs_fds[fd_idx].fil, name, fatFlags);
    }

    if (res != FR_OK) {
        FdTableLock();
        s_fatfs_fds[fd_idx].in_use = false;
        FdTableUnlock();
        errno = (res == FR_NO_FILE || res == FR_NO_PATH) ? ENOENT : EIO;
        return -1;
    }

    return fd_idx + 3;
}

int _close(int file) {
    if (file >= 3 && file < 3 + MAX_FATFS_FDS) {
        int idx = file - 3;
        FdTableLock();
        bool was_in_use = s_fatfs_fds[idx].in_use;
        s_fatfs_fds[idx].in_use = false;
        FdTableUnlock();
        if (was_in_use) {
            f_close(&s_fatfs_fds[idx].fil);
        }
    }
    return 0;
}

ssize_t _read(int file, void *ptr, size_t len) {
    if (file >= 3 && file < 3 + MAX_FATFS_FDS) {
        int idx = file - 3;
        if (!s_fatfs_fds[idx].in_use) {
            errno = EBADF;
            return -1;
        }
        UINT bytesRead = 0;
        FRESULT res = f_read(&s_fatfs_fds[idx].fil, ptr, (UINT)len, &bytesRead);
        if (res != FR_OK) {
            errno = EIO;
            return -1;
        }
        return (ssize_t)bytesRead;
    }
    return 0;
}

ssize_t _write(int file, const void *ptr, size_t len) {
    if (file == 1 || file == 2) {
        LogOutput((const char*)ptr, len);
        return (ssize_t)len;
    }
    if (file >= 3 && file < 3 + MAX_FATFS_FDS) {
        int idx = file - 3;
        if (!s_fatfs_fds[idx].in_use) {
            errno = EBADF;
            return -1;
        }
        UINT bytesWritten = 0;
        FRESULT res = f_write(&s_fatfs_fds[idx].fil, ptr, (UINT)len, &bytesWritten);
        if (res != FR_OK) {
            errno = EIO;
            return -1;
        }
        return (ssize_t)bytesWritten;
    }
    return len;
}

off_t _lseek(int file, off_t offset, int whence) {
    if (file >= 3 && file < 3 + MAX_FATFS_FDS) {
        int idx = file - 3;
        if (!s_fatfs_fds[idx].in_use) {
            errno = EBADF;
            return -1;
        }
        FSIZE_t targetPos = 0;
        if (whence == SEEK_SET) {
            targetPos = (FSIZE_t)offset;
        } else if (whence == SEEK_CUR) {
            targetPos = f_tell(&s_fatfs_fds[idx].fil) + offset;
        } else if (whence == SEEK_END) {
            targetPos = f_size(&s_fatfs_fds[idx].fil) + offset;
        }
        FRESULT res = f_lseek(&s_fatfs_fds[idx].fil, targetPos);
        if (res != FR_OK) {
            errno = EIO;
            return -1;
        }
        return (off_t)f_tell(&s_fatfs_fds[idx].fil);
    }
    return 0;
}

int _fstat(int file, struct stat *st) {
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    memset(st, 0, sizeof(struct stat));
    if (file < 3) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    if (file >= 3 && file < 3 + MAX_FATFS_FDS) {
        int idx = file - 3;
        FdTableLock();
        bool in_use = s_fatfs_fds[idx].in_use;
        FSIZE_t sz = in_use ? f_size(&s_fatfs_fds[idx].fil) : 0;
        FdTableUnlock();
        if (in_use) {
            st->st_mode = S_IFREG | 0666;
            st->st_size = sz;
            return 0;
        }
    }
    st->st_mode = S_IFREG;
    return 0;
}

int _stat(const char *path, struct stat *st) {
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    memset(st, 0, sizeof(struct stat));

    char fatPath[512];
    if (strchr(path, ':') != nullptr) {
        strncpy(fatPath, path, sizeof(fatPath) - 1);
    } else if (path[0] == '/') {
        snprintf(fatPath, sizeof(fatPath), "SD:%s", path);
    } else if (strncmp(path, "./", 2) == 0) {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", path + 2);
    } else {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", path);
    }
    fatPath[sizeof(fatPath) - 1] = '\0';

    FILINFO fno;
    FRESULT res = f_stat(fatPath, &fno);
    if (res != FR_OK) {
        res = f_stat(path, &fno);
    }
    if (res != FR_OK) {
        errno = ENOENT;
        return -1;
    }
    if (fno.fattrib & AM_DIR) {
        st->st_mode = S_IFDIR | 0777;
    } else {
        st->st_mode = S_IFREG | 0666;
    }
    st->st_size = fno.fsize;
    return 0;
}

int _isatty(int file) {
    return (file < 3) ? 1 : 0;
}

void _exit(int status) {
    (void)status;
    while (1) {}
}

int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        unsigned ticks = CTimer::GetClockTicks();
        tv->tv_sec = ticks / 1000000;
        tv->tv_usec = ticks % 1000000;
    }
    return 0;
}

int mkdir(const char *pathname, mode_t mode) {
    (void)mode;
    if (!pathname || !*pathname) return -1;
    char fatPath[512];
    if (strncmp(pathname, "SD:", 3) == 0 || strncmp(pathname, "SD:/", 4) == 0) {
        strncpy(fatPath, pathname, sizeof(fatPath) - 1);
    } else {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", pathname);
    }
    fatPath[sizeof(fatPath) - 1] = '\0';
    return (f_mkdir(fatPath) == FR_OK || f_mkdir(pathname) == FR_OK) ? 0 : -1;
}

int unlink(const char *path) {
    if (!path) return -1;
    char fatPath[512];
    if (strchr(path, ':') != nullptr) {
        strncpy(fatPath, path, sizeof(fatPath) - 1);
    } else {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", path);
    }
    fatPath[sizeof(fatPath) - 1] = '\0';
    return (f_unlink(fatPath) == FR_OK || f_unlink(path) == FR_OK) ? 0 : -1;
}

int _unlink(const char *path) {
    return unlink(path);
}

int chdir(const char *path) {
    (void)path;
    return 0;
}

char *getcwd(char *buf, size_t size) {
    if (buf && size > 4) {
        strncpy(buf, "SD:/", size);
    }
    return buf;
}

int usleep(useconds_t usec) {
    CTimer::SimpleusDelay((unsigned)usec);
    return 0;
}

time_t time(time_t *tloc) {
    time_t t = (time_t)(CTimer::GetClockTicks() / 1000000);
    if (tloc) *tloc = t;
    return t;
}

struct tm *localtime(const time_t *timep) {
    static struct tm t;
    memset(&t, 0, sizeof(t));
    (void)timep;
    return &t;
}

time_t mktime(struct tm *tm) {
    (void)tm;
    return 0;
}

DIR *opendir(const char *name) {
    if (!name || !*name) return NULL;
    char fatPath[512];
    if (strchr(name, ':') != nullptr) {
        strncpy(fatPath, name, sizeof(fatPath) - 1);
    } else if (name[0] == '/') {
        snprintf(fatPath, sizeof(fatPath), "SD:%s", name);
    } else {
        snprintf(fatPath, sizeof(fatPath), "SD:/%s", name);
    }
    fatPath[sizeof(fatPath) - 1] = '\0';

    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) return NULL;
    if (f_opendir(dir, fatPath) == FR_OK || f_opendir(dir, name) == FR_OK) {
        return dir;
    }
    free(dir);
    return NULL;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) return NULL;
    static struct dirent de;
    FILINFO fno;
    if (f_readdir(dirp, &fno) == FR_OK && fno.fname[0] != 0) {
        memset(&de, 0, sizeof(de));
        strncpy(de.d_name, fno.fname, sizeof(de.d_name) - 1);
        de.d_type = (fno.fattrib & AM_DIR) ? DT_DIR : DT_REG;
        return &de;
    }
    return NULL;
}

int closedir(DIR *dirp) {
    if (!dirp) return 0;
    f_closedir(dirp);
    free(dirp);
    return 0;
}

void rewinddir(DIR *dirp) {
    if (!dirp) return;
    f_readdir(dirp, NULL);
}

void _fini(void) {}

} // extern "C"
