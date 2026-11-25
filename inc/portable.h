// portable.h: the few POSIX functions that Windows (MinGW-w64) lacks
#ifndef V32_PORTABLE_H
#define V32_PORTABLE_H

#include <stdlib.h>

// MinGW's plain 'printf' format archetype knows no %zu / %lld; its stdio (with __USE_MINGW_ANSI_STDIO, the gnu11 default) does
#ifdef __MINGW32__
#define V32_PRINTF  gnu_printf
#else
#define V32_PRINTF  printf
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdio.h>

// Windows opens "w" files and stdout in text mode (LF -> CRLF): that corrupts a VBIN written to STDOUT and makes the
// .debug/.sym/relocs/-E output differ from the other platforms. Call first thing in main.
static inline void  v32_binary_mode (void)
{
    _fmode = _O_BINARY;
    _setmode (_fileno (stdin),  _O_BINARY);
    _setmode (_fileno (stdout), _O_BINARY);
}

// realpath (path, NULL) -> malloc'ed absolute path (_fullpath does not check that the file exists)
static inline char  *v32_realpath (const char *path, char *resolved)
{
    return (resolved != NULL ? _fullpath (resolved, path, 4096) : _fullpath (NULL, path, 0));
}
#else
#define v32_binary_mode()  ((void) 0)
#define v32_realpath  realpath
#endif

#endif
