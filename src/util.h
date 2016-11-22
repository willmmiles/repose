#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#define _unlikely_(x)       __builtin_expect(!!(x), 1)
#define _unused_            __attribute__((unsused))
#define _noreturn_          __attribute__((noreturn))
#define _cleanup_(x)        __attribute__((cleanup(x)))
#define _printf_(a,b)       __attribute__((format (printf, a, b)))
#define _sentinel_          __attribute__((sentinel))
#define _cleanup_free_      _cleanup_(freep)
#define _cleanup_fclose_    _cleanup_(fclosep)
#define _cleanup_closedir_  _cleanup_(closedirp)
#define _cleanup_close_     _cleanup_(closep)

/* XXX: clang does not have generic builtin */
#if __clang__
#define __builtin_add_overflow(a, b, r) _Generic((a), \
    int: __builtin_sadd_overflow, \
    long int: __builtin_saddl_overflow, \
    long long: __builtin_saddll_overflow, \
    unsigned int: __builtin_uadd_overflow, \
    unsigned long int: __builtin_uaddl_overflow, \
    unsigned long long: __builtin_uaddll_overflow)(a, b, r)
#endif

static inline void freep(void *p)      { free(*(void **)p); }
static inline void fclosep(FILE **fp)  { if (*fp) fclose(*fp); }
static inline void closedirp(DIR **dp) { if (*dp) closedir(*dp); }
static inline void closep(int *fd)     { if (*fd >= 0) close(*fd); }

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t len) { return strncmp(s1, s2, len) == 0; }

void check_posix(intmax_t rc, const char *fmt, ...) _printf_(2, 3);
void check_null(const void *ptr, const char *fmt, ...) _printf_(2, 3);

FILE *fopenat(int dirfd, const char *path, const char *mode);

char *joinstring(const char *root, ...) _sentinel_;

int parse_size(const char *str, size_t *out);
int parse_time(const char *str, time_t *out);

char *strstrip(char *s);
char *hex_representation(unsigned char *bytes, size_t size);

#ifdef __QNX__

#define O_DIRECTORY 0

#define AT_SYMLINK_NOFOLLOW 1
int openat(int dirfd, const char *pathname, int flags, ...);
int symlinkat(const char *oldpath, int newdirfd, const char *newpath);
int fstatat(int fd, const char *restrict path, struct stat *restrict buf, int flag);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
int unlinkat(int dirfd, const char *pathname, int flags);

DIR *fdopendir(int fd);

int copy_file(int dest, int src);
#define canonicalize_file_name(fname) realpath(fname,NULL);

ssize_t getline(char **lineptr, size_t *n, FILE *stream);

char *strchrnul(const char *s, int c);
void *memrchr(const void *s, int c, size_t n);
char *strndup(const char *s, size_t n);
char *stpcpy(char *dest, const char *src);

#else /* ! QNX */

#define copy_file(dest, src) ioctl(dest, BTRFS_IOC_CLONE, src);

#endif

