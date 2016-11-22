#include "filecache.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <alpm.h>

#include "package.h"
#include "pkghash.h"
#include "filters.h"
#include "util.h"

static inline bool is_file(const struct dirent *dp)
{
#ifdef __QNX__
    struct dirent_extra* extra;
    for( extra = _DEXTRA_FIRST(dp);
    _DEXTRA_VALID(extra, dp);
    extra = _DEXTRA_NEXT(extra)) {
      switch(extra->d_type) {
        /* No data */
         case _DTYPE_NONE  :
            break;
         /* Data includes information as returned by stat() */
         case _DTYPE_STAT  :
            {
                struct dirent_extra_stat* ex = (struct dirent_extra_stat*) extra;
                return S_ISREG(ex->d_stat.st_mode);
            }
            break;
         /* Data includes information as returned by lstat() */
         case _DTYPE_LSTAT :
            {
                struct dirent_extra_stat* ex = (struct dirent_extra_stat*) extra;
                return S_ISREG(ex->d_stat.st_mode);
            }
            break;
      }
    }
    return false;
#else
    return dp->d_type == DT_REG || d_type == DT_UNKNOWN;
#endif
}

static inline alpm_pkghash_t *pkgcache_add(alpm_pkghash_t *cache, struct pkg *pkg)
{
    struct pkg *old = _alpm_pkghash_find(cache, pkg->name);
    if (!old) {
        return _alpm_pkghash_add(cache, pkg);
    }

    int vercmp = alpm_pkg_vercmp(pkg->version, old->version);
    if (vercmp == 0 || vercmp == 1) {
        return _alpm_pkghash_replace(cache, pkg, old);
    }

    return cache;
}

static size_t get_filecache_size(DIR *dirp)
{
    struct dirent *dp;
    size_t size = 0;

    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
        if (is_file(dp))
            ++size;
    }

    rewinddir(dirp);
    return size;
}

static struct pkg *load_from_file(int dirfd, const char *filename)
{
    _cleanup_close_ int pkgfd = openat(dirfd, filename, O_RDONLY);
    check_posix(pkgfd, "failed to open %s", filename);

    struct pkg *pkg = malloc(sizeof(pkg_t));
    *pkg = (struct pkg){ .filename = strdup(filename) };

    if (load_package(pkg, pkgfd) < 0) {
        package_free(pkg);
        return NULL;
    }

    if (load_package_signature(pkg, dirfd) < 0 && errno != ENOENT) {
        package_free(pkg);
        return NULL;
    }

    return pkg;
}

static alpm_pkghash_t *scan_for_targets(alpm_pkghash_t *cache, int dirfd, DIR *dirp,
                                        alpm_list_t *targets, const char *arch)
{
    const struct dirent *dp;

    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
        if (!is_file(dp))
            continue;

        struct pkg *pkg = load_from_file(dirfd, dp->d_name);
        if (!pkg)
            continue;

        if (targets && !match_targets(pkg, targets)) {
            package_free(pkg);
            continue;
        }

        if (arch && !match_arch(pkg, arch)) {
            package_free(pkg);
            continue;
        }

        cache = pkgcache_add(cache, pkg);
    }

    return cache;
}

alpm_pkghash_t *get_filecache(int dirfd, alpm_list_t *targets, const char *arch)
{
    int dupfd = dup(dirfd);
    check_posix(dupfd, "failed to duplicate fd");
    check_posix(lseek(dupfd, 0, SEEK_SET), "failed to lseek");

    _cleanup_closedir_ DIR *dirp = fdopendir(dupfd);
    check_null(dirp, "fdopendir failed");

#ifdef __QNX__
    dircntl(dirp, D_SETFLAG, D_FLAG_STAT);
#endif

    size_t size = get_filecache_size(dirp);
    alpm_pkghash_t *cache = _alpm_pkghash_create(size);

    return scan_for_targets(cache, dirfd, dirp, targets, arch);
}
