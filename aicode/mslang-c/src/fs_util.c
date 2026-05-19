#include "ms/fs_util.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>  /* _mkdir  */
#include <io.h>      /* _unlink */

bool ms_fs_stat(const char* path, MsFileMeta* out) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return false;
    ULARGE_INTEGER sz;
    sz.LowPart  = d.nFileSizeLow;
    sz.HighPart = d.nFileSizeHigh;
    out->size = sz.QuadPart;
    ULARGE_INTEGER ft;
    ft.LowPart  = d.ftLastWriteTime.dwLowDateTime;
    ft.HighPart = d.ftLastWriteTime.dwHighDateTime;
    /* FILETIME: 100-ns ticks since 1601-01-01; 116444736000000000 ticks to Unix epoch */
    out->mtime_ns = (int64_t)(ft.QuadPart - 116444736000000000ULL) * 100LL;
    return true;
}

bool ms_fs_mkdir(const char* path) {
    return _mkdir(path) == 0 || errno == EEXIST;
}

bool ms_fs_atomic_rename(const char* tmp, const char* dst) {
    return MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING) != 0;
}

bool ms_fs_unlink(const char* path) {
    return _unlink(path) == 0;
}

#else
#include <sys/stat.h>
#include <unistd.h>

bool ms_fs_stat(const char* path, MsFileMeta* out) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    out->size = (uint64_t)st.st_size;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    out->mtime_ns = (int64_t)st.st_mtimespec.tv_sec * 1000000000LL
                  + (int64_t)st.st_mtimespec.tv_nsec;
#else
    out->mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000LL
                  + (int64_t)st.st_mtim.tv_nsec;
#endif
    return true;
}

bool ms_fs_mkdir(const char* path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool ms_fs_atomic_rename(const char* tmp, const char* dst) {
    return rename(tmp, dst) == 0;
}

bool ms_fs_unlink(const char* path) {
    return unlink(path) == 0;
}
#endif

/* ---- ms_string_split ---- */

int ms_string_split(const char* s, char delim, char*** out_parts) {
    *out_parts = NULL;
    if (!s || !*s) return 0;

    /* Count tokens */
    int count = 1;
    for (const char* p = s; *p; p++) {
        if (*p == delim) count++;
    }

    char** parts = (char**)malloc((size_t)count * sizeof(char*));
    if (!parts) return 0;

    int idx = 0;
    const char* start = s;
    const char* p = s;
    for (;;) {
        if (*p == delim || *p == '\0') {
            size_t len = (size_t)(p - start);
            parts[idx] = (char*)malloc(len + 1);
            if (!parts[idx]) {
                /* cleanup on OOM */
                for (int i = 0; i < idx; i++) free(parts[i]);
                free(parts);
                return 0;
            }
            memcpy(parts[idx], start, len);
            parts[idx][len] = '\0';
            idx++;
            if (*p == '\0') break;
            start = p + 1;
        }
        p++;
    }
    *out_parts = parts;
    return idx;
}
