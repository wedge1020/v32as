//
// heapstat.h: v32as's own heap accounting, for --stats
//
// The sources include this through v32as.h. Where the C library can tell how big an allocated block is (glibc and
// musl: malloc_usable_size, macOS: malloc_size), malloc, calloc, realloc, free and strdup are routed through counting
// wrappers (heapstat.c) that keep the live total and its high-water mark. Elsewhere the calls stay the library's own
// and --stats says the figures are not available. The wrappers do not move or pad a block, so a pointer from a library
// that allocates for itself (readline, realpath) may still be freed with free(): its size is then taken off the live
// total without having been added (the figure is clamped at 0; only the REPL does this, and only for a line).
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef V32AS_HEAPSTAT_H
#define V32AS_HEAPSTAT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if !defined(V32AS_NO_HEAPSTAT) && (defined(__GLIBC__) || defined(__APPLE__) || defined(__linux__))
#define V32AS_HEAPSTAT 1
#else
#define V32AS_HEAPSTAT 0
#endif

typedef struct
{
    int           available;        // 0: this build cannot count (the other fields are 0)
    unsigned long live;             // bytes allocated now
    unsigned long peak;             // the most that was ever allocated at once, in bytes
    unsigned long mark_live;        // live and peak at the last heapstat_mark (the end of preprocessing), in bytes
    unsigned long mark_peak;
    unsigned long allocs;           // calls that allocated (malloc, calloc, strdup, realloc to a new block)
    unsigned long frees;
}
heapstat_t;

heapstat_t  heapstat_get   (void);
void        heapstat_mark  (void);      // note live and peak now (a later mark replaces an earlier one)

#if V32AS_HEAPSTAT
void   *v32_malloc  (size_t n);
void   *v32_calloc  (size_t n, size_t m);
void   *v32_realloc (void *p, size_t n);
void    v32_free    (void *p);
char   *v32_strdup  (const char *s);

#define malloc(n)       v32_malloc  (n)
#define calloc(n, m)    v32_calloc  (n, m)
#define realloc(p, n)   v32_realloc (p, n)
#define free(p)         v32_free    (p)
#define strdup(s)       v32_strdup  (s)
#endif

#endif
