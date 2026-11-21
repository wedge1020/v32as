//
// heapstat.c: counting wrappers for the allocator (see heapstat.h)
//
////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heapstat.h"

#if V32AS_HEAPSTAT

#if defined(__APPLE__)
#include <malloc/malloc.h>
#define BLOCK_SIZE(p)   malloc_size (p)
#else
#include <malloc.h>
#define BLOCK_SIZE(p)   malloc_usable_size (p)
#endif

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

static long           live;
static unsigned long  peak, mark_live, mark_peak, allocs, frees;

static void  add (void *p)
{
    if (p != NULL)
    {
        live += (long) BLOCK_SIZE (p);
        allocs++;

        if (live > 0 && (unsigned long) live > peak)
        {
            peak = (unsigned long) live;
        }
    }
}

void  *v32_malloc (size_t n)
{
    void  *p = malloc (n);

    add (p);
    return (p);
}

void  *v32_calloc (size_t n, size_t m)
{
    void  *p = calloc (n, m);

    add (p);
    return (p);
}

void  *v32_realloc (void *old, size_t n)
{
    long   was = old != NULL ? (long) BLOCK_SIZE (old) : 0;
    void  *p   = realloc (old, n);

    if (p != NULL)
    {
        // a block that grew in place is not a new allocation; the peak is taken after the swap (the old and the new
        // block are both alive for a moment when it moved, which the high-water mark of a copy has to include)
        long  now = (long) BLOCK_SIZE (p);

        if (p != old && old != NULL)
        {
            live += now;
            if (live > 0 && (unsigned long) live > peak)
            {
                peak = (unsigned long) live;
            }
            live -= was;
            allocs++;
        }
        else
        {
            live += now - was;
            if (old == NULL)
            {
                allocs++;
            }
            if (live > 0 && (unsigned long) live > peak)
            {
                peak = (unsigned long) live;
            }
        }
    }

    return (p);
}

void  v32_free (void *p)
{
    if (p != NULL)
    {
        live -= (long) BLOCK_SIZE (p);
        frees++;
    }

    free (p);
}

char  *v32_strdup (const char *s)
{
    size_t  n = strlen (s) + 1;
    char   *p = malloc (n);

    if (p != NULL)
    {
        memcpy (p, s, n);
        add (p);
    }

    return (p);
}

heapstat_t  heapstat_get (void)
{
    heapstat_t  h = { 1, live > 0 ? (unsigned long) live : 0, peak, mark_live, mark_peak, allocs, frees };

    return (h);
}

void  heapstat_mark (void)
{
    mark_live = live > 0 ? (unsigned long) live : 0;
    mark_peak = peak;
}

#else

heapstat_t  heapstat_get (void)
{
    heapstat_t  h = { 0, 0, 0, 0, 0, 0, 0 };

    return (h);
}

void  heapstat_mark (void)
{
}

#endif
