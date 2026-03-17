#ifndef QUICKJS_WIN32_H
#define QUICKJS_WIN32_H

#if defined(_WIN32)
#include <windows.h>
#include <stdint.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <malloc.h>
#include <fcntl.h>

#if !defined(_TIMEVAL_DEFINED) && !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

static inline int qjs_gettimeofday(struct timeval *tv, void *tz)
{
    FILETIME ft;
    ULARGE_INTEGER uli;
    uint64_t t;

    (void)tz;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    t = uli.QuadPart;
    /* convert from 1601-01-01 (100ns) to 1970-01-01 (us) */
    t -= 116444736000000000ULL;
    tv->tv_sec = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t / 10ULL) % 1000000ULL);
    return 0;
}

#define gettimeofday qjs_gettimeofday

#ifndef open
#define open _open
#endif
#ifndef close
#define close _close
#endif
#ifndef read
#define read _read
#endif
#ifndef write
#define write _write
#endif
#ifndef lseek
#define lseek _lseeki64
#endif
#ifndef isatty
#define isatty _isatty
#endif
#ifndef dup
#define dup _dup
#endif
#ifndef dup2
#define dup2 _dup2
#endif
#ifndef chdir
#define chdir _chdir
#endif
#ifndef getcwd
#define getcwd _getcwd
#endif
#ifndef mkdir
#define mkdir _mkdir
#endif
#ifndef rmdir
#define rmdir _rmdir
#endif
#ifndef unlink
#define unlink _unlink
#endif
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#ifndef fdopen
#define fdopen _fdopen
#endif
#ifndef fileno
#define fileno _fileno
#endif
#ifndef ftello
#define ftello _ftelli64
#endif
#ifndef fseeko
#define fseeko _fseeki64
#endif

static inline int qjs_pipe(int fds[2])
{
    return _pipe(fds, 256, _O_BINARY);
}
#define pipe qjs_pipe

#ifndef S_IFMT
#define S_IFMT _S_IFMT
#endif
#ifndef S_IFREG
#define S_IFREG _S_IFREG
#endif
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#ifndef S_IFCHR
#define S_IFCHR _S_IFCHR
#endif
#ifndef S_IFIFO
#define S_IFIFO _S_IFIFO
#endif
#ifndef S_IFBLK
#ifndef _S_IFBLK
#define _S_IFBLK 0
#endif
#define S_IFBLK _S_IFBLK
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifndef environ
#define environ _environ
#endif

#endif /* _WIN32 */

#endif /* QUICKJS_WIN32_H */
