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

// (glibc: __GLIBC__ once a library header is in, as stdlib.h above; macOS; musl and other Linux libraries have no marker of their own,
// so __linux__ counts only with a GNU-compatible compiler that is not v32cc.  v32cc's own builds, and any other, can say V32AS_NO_HEAPSTAT.)
#if !defined(V32AS_NO_HEAPSTAT) && !defined(__V32CC__) \
    && (defined(__GLIBC__) || defined(__APPLE__) || (defined(__linux__) && defined(__GNUC__)))
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

#if V32AS_HEAPSTAT
heapstat_t  heapstat_get   (void);
void        heapstat_mark  (void);      // note live and peak now (a later mark replaces an earlier one)
#else
// no counting here: stubs, so that heapstat.c need not be built or linked (v32nix builds a fixed list of sources without it)
static heapstat_t  heapstat_get (void)
{
    heapstat_t  h = { 0, 0, 0, 0, 0, 0, 0 };

    return (h);
}

#if !defined(__V32CC__)
static void  heapstat_mark (void)
{
}
#endif
#endif

// On the console (v32cc, v32nix), --stats reads the allocator of the C library instead (libc/stdlib.c), in words: v32_heap_info
// gives the words in use, free and in the largest hole (a walk over every block, so only at the end of preprocessing and at the end);
// the break (sbrk (0)) is the size of the heap including its holes and headers, which is what a v32nix process slot has to hold,
// and it is sampled after every allocation that v32as's own xrealloc makes (heapstat_sample): the library does not keep a
// high-water mark, so this is a lower bound (a stdio buffer or a lexer buffer allocated and trimmed between two samples is not
// seen).  The state lives in main.c (this file has no .c of its own there).
#if !V32AS_HEAPSTAT && defined(__V32CC__)
#define V32AS_HEAPWORDS 1

#include <unistd.h>

typedef struct
{
    int  on;                        // --stats was given: sample
    int  start;                     // the heap's first word (v32_heap_info)
    int  peak_break;                // the highest break seen, minus start, in words
    int  mark_used, mark_free, mark_largest, mark_break;    // at the end of preprocessing (heapstat_mark)
    int  peak_used;                 // the larger of the used words at the mark and at the end
}
heapwords_t;

extern heapwords_t  heapwords;

static void  heapstat_sample (void)
{
    int  b;

    if (heapwords.on)
    {
        b = (int) sbrk (0) - heapwords.start;

        if (b > heapwords.peak_break)
        {
            heapwords.peak_break = b;
        }
    }
}

static void  heapstat_begin (void)
{
    heapwords.on = 1;
    v32_heap_info (&heapwords.start, NULL, NULL, NULL, NULL);
    heapstat_sample ();
}

static void  heapstat_mark (void)
{
    int  e;

    if (heapwords.on)
    {
        v32_heap_info (NULL, &e, &heapwords.mark_used, &heapwords.mark_free, &heapwords.mark_largest);
        heapwords.mark_break = e + 1 - heapwords.start;
        heapwords.peak_used  = heapwords.mark_used;
        heapstat_sample ();
    }
}
#else
// (the host's counting wrappers, and a build with no counting at all, have no use for these)
#define heapstat_sample()   ((void) 0)
#define heapstat_begin()    ((void) 0)
#endif

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
