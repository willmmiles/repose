#include "util.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>

#define WHITESPACE " \t\n\r"

static int oflags(const char *mode)
{
    int m, o;

    switch (mode[0]) {
    case 'r':
        m = O_RDONLY;
        o = 0;
        break;
    case 'w':
        m = O_WRONLY;
        o = O_CREAT | O_TRUNC;
        break;
    case 'a':
        m = O_WRONLY;
        o = O_CREAT | O_APPEND;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    while (*++mode) {
        switch (*mode) {
        case '+':
            m = (m & ~O_ACCMODE) | O_RDWR;
            break;
        }
    }

    return m | o;
}

FILE *fopenat(int dirfd, const char *path, const char *mode)
{
    int flags = oflags(mode);
    if (flags < 0)
        return NULL;

    int fd = openat(dirfd, path, flags);
    if (_unlikely_(fd < 0))
        return NULL;
    return fdopen(fd, mode);
}

void check_posix(intmax_t rc, const char *fmt, ...)
{
    if (_unlikely_(rc == -1)) {
        va_list args;
        va_start(args, fmt);
        verr(EXIT_FAILURE, fmt, args);
        va_end(args);
    }
}

void check_null(const void *ptr, const char *fmt, ...)
{
    if (_unlikely_(!ptr)) {
        va_list args;
        va_start(args, fmt);
        verr(EXIT_FAILURE, fmt, args);
        va_end(args);
    }
}

char *joinstring(const char *root, ...)
{
    size_t len;
    char *ret = NULL, *p;
    const char *temp;
    va_list ap;

    if (!root)
        return NULL;

    len = strlen(root);

    va_start(ap, root);
    while ((temp = va_arg(ap, const char *))) {
        size_t temp_len = strlen(temp);

        if (temp_len > ((size_t) -1) - len) {
            va_end(ap);
            return NULL;
        }

        len += temp_len;
    }
    va_end(ap);

    ret = malloc(len + 1);
    if (ret) {
        p = stpcpy(ret, root);

        va_start(ap, root);
        while ((temp = va_arg(ap, const char *)))
            p = stpcpy(p, temp);
        va_end(ap);
    }

    return ret;
}

static int xstrtoul(const char *str, unsigned long *out)
{
    char *end = NULL;
    errno = 0;

    if (!str || !str[0]) {
        errno = EINVAL;
        return -1;
    }

    *out = strtoul(str, &end, 10);
    if (errno) {
        return -1;
    } else if (str == end || (end && *end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int parse_size(const char *str, size_t *out)
{
    unsigned long value = 0;
    if (xstrtoul(str, &value) < 0)
        return -1;

    if (value > SIZE_MAX) {
        errno = ERANGE;
        return -1;
    }

    *out = (size_t)value;
    return 0;
}

int parse_time(const char *str, time_t *out)
{
    unsigned long value = 0;
    if (xstrtoul(str, &value) < 0)
        return -1;

    if (value > INT_MAX) {
        errno = ERANGE;
        return -1;
    }

    *out = (time_t)value;
    return 0;
}

char *hex_representation(unsigned char *bytes, size_t size)
{
    static const char *hex_digits = "0123456789abcdef";
    char *str = malloc(2 * size + 1);
    size_t i;

    for(i = 0; i < size; i++) {
        str[2 * i] = hex_digits[bytes[i] >> 4];
        str[2 * i + 1] = hex_digits[bytes[i] & 0x0f];
    }

    str[2 * size] = '\0';
    return str;
}

char *strstrip(char *s)
{
    char *e;
    s += strspn(s, WHITESPACE);

    for (e = strchr(s, 0); e > s; --e) {
        if (!strchr(WHITESPACE, e[-1]))
            break;
    }

    *e = 0;
    return s;
}


#ifdef __QNX__

// QNX shims for glibc and posix 2008.1 functions
// efficiency not guaranteed
#include <sys/iomgr.h>

static const char* getfdpath(int dirfd, const char* newpath) {
    if (newpath[0] == '/') return newpath;

    char dir_path[PATH_MAX];

    int path_size = iofdinfo(dirfd,0,NULL,dir_path,PATH_MAX);
    if (path_size < 1) return NULL;
    --path_size;    // includes null byte
    //fprintf(stderr,"gfdp: %d %s\n",path_size,dir_path);

    // Append 'newpath'
    if (dir_path[path_size-1] != '/') {
        dir_path[path_size] = '/';
        ++path_size;
    }
    strncpy(dir_path + path_size, newpath, sizeof(dir_path) - (path_size+1));

    //fprintf(stderr,"gfdp: %d %s\n",PATH_MAX - (path_size+1),dir_path);

    return strdup(dir_path);
    //return realpath(dir_path, NULL);
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    va_list x_arg;
    int open_opts;
    int result = -1;
    const char* tgtpath = getfdpath(dirfd, pathname);
    if (tgtpath) {
        // Retrieve the last argument
        va_start(x_arg, flags);
        open_opts = va_arg(x_arg, int);
        va_end(x_arg);

        result = open(tgtpath, flags, open_opts);
        //fprintf(stderr,"Openat: %s -> %s %d %d -> %d\n",pathname, tgtpath,flags,open_opts, result);

        if (tgtpath != pathname) {
            int etmp = errno;
            free((void*)tgtpath);
            errno = etmp;
        }
    };

    return result;
}

int symlinkat(const char *oldpath, int newdirfd, const char *newpath) {
    int result = -1;
    const char* tgtpath = getfdpath(newdirfd, newpath);
    if (tgtpath) {
        result = symlink(oldpath,tgtpath);
        if (tgtpath != newpath) {
            int etmp = errno;
            free((void*)tgtpath);
            errno = etmp;
        }
    };
    return result;
}

int fstatat(int fd, const char *restrict path, struct stat *restrict buf, int flag) {
    int result = -1;
    const char* tgtpath = getfdpath(fd, path);
    if (tgtpath) {
        if (flag & AT_SYMLINK_NOFOLLOW) {
            result = lstat(tgtpath, buf);
        } else {
            result = stat(tgtpath, buf);
        }
        if (tgtpath != path) {
            int etmp = errno;
            free((void*)tgtpath);
            errno = etmp;
        }
    };
    return result;
}

int faccessat(int fd, const char *path, int mode, int flags) {
    // NB: FLAGS ARE IGNORED
    (void) flags;
    int result = -1;
    const char* tgtpath = getfdpath(fd, path);
    if (tgtpath) {
        result = access(tgtpath, mode);
        if (tgtpath != path) {
            int etmp = errno;
            free((void*)tgtpath);
            errno = etmp;
        }
    };
    return result;
}


int unlinkat(int fd, const char *path, int flags) {
    int result = -1;
    const char* tgtpath = getfdpath(fd, path);
    if (tgtpath) {
        result = unlink(tgtpath);
        if (tgtpath != path) {
            int etmp = errno;
            free((void*)tgtpath);
            errno = etmp;
        }
    };
    return result;
}

DIR *fdopendir(int fd) {
    char dir_path[PATH_MAX];
    int path_size = iofdinfo(fd,0,NULL,dir_path,PATH_MAX);
    if (path_size < 1) return NULL;

    return opendir(dir_path);
}

int copy_file(int dest, int src) {
	int32_t			r, w, ww;
	size_t len;
	char* bptr = NULL;

	errno = 0;

	for (len=(16*1024)+(4*1024);(!bptr) && (len>(4*1024));) {
		bptr = malloc(len);
		if (!bptr) len-=1024;
	}

    if (!bptr) return -1;

	for (r=read(src,bptr,len);r!=-1 && r>0;r=read(src,bptr,len)) {
		w=0;
		#ifdef DIAG
		fprintf(stderr,"read %d bytes\n",r);
		#endif

		do {
			#ifdef DIAG
			fprintf(stderr,"trying to write %d bytes\n",r-w);
			#endif
			w+=(ww=write(dest,bptr+w,r-w));
			#ifdef DIAG
			fprintf(stderr,"wrote %d bytes (%d now of %d)\n",ww,w,r);
			#endif
			if (ww==-1) break;
		} while (w<r);

		if (ww==-1) {
		    r = -1;
		    break;
		}
	} /* loop */

    free(bptr);

	return r;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    ssize_t count = 0;
    char* buf = NULL;
    ssize_t buf_size = 0;
    int err = 0;

    if (!lineptr || !n) return -EINVAL;

    buf = *lineptr;
    buf_size = *n;

    for(;;) {
        int ch = fgetc(stream);

        if (ch == EOF) break;

        if ((count+2) > buf_size) {
            size_t new_buf_size = buf_size ? (buf_size * 2) : 32;
            buf = realloc(buf, new_buf_size);
            if (!buf) { err = -ENOMEM; break; }
            buf_size = new_buf_size;
        };

        buf[count++] = ch;
        buf[count] = 0;

        if (ch == '\n') break;
        if (count > SSIZE_MAX) {
            err = -EOVERFLOW;
            break;
        }
    };

    // Update output
    *lineptr = buf;
    *n = count;

    if (err != 0) return err;
    if (ferror(stream)) return 0;
    if (feof(stream) && (count == 0)) {
        if (!buf) buf = malloc(4);
        if (!buf) return -ENOMEM;
        buf[0] = 0;
        *lineptr = buf;
    }
/*
    if (count) {
        fprintf(stderr,"getline: %d %s\n",count,buf);
    }
*/
    return count;
}

char *strchrnul(const char *s, int c) {
    char* r = strchr(s,c);
    if (!r) r = (char*) (s + strlen(s));
    return r;
}

void *memrchr(const void *s, int c, size_t n) {
    const char* p;
    for (p = (const char*)s + (n-1); p >= (const char*) s; --p) {
        if (*p == c) return (void*) p;
    }
    return NULL;
}

char *strndup(const char *s, size_t n) {
    char* r = malloc(n+1);

    if (r) {
        char* p = r;
        r[n] = 0;
        for(p = r; (*s != 0) && (n > 0); ++s, ++p, --n) {
            *p = *s;
        }
        // Copy null terminator
        *p = *s;
    }
    return r;
}

char *stpcpy(char *dest, const char *src) {
    char* c = strcpy(dest,src);
    return c+strlen(dest);
}


#endif
