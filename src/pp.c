//
// pp.c: the v32as preprocessor. See pp.h for the overview.
//
// Design notes
//
//  - Line oriented, like the assembly language itself. A directive is a line whose first
//    non-blank character is '%'. Everything else is source text, expanded and passed on.
//  - Macro expansion is text based and lexically aware of what the assembler's scanner
//    (lexer.l) would see: ';' comments, "strings", 'c' character literals and numbers are
//    copied verbatim and never searched for macro names.
//  - Replacements are spliced into the line in place and rescanned from their start, so
//    an expansion can combine with the text that follows it (%define CALL f ... CALL(1)).
//    Each splice records a "hide region": a name is never re-expanded inside the text its
//    own expansion produced, which makes self-reference harmless.
//  - %define bodies are stored unexpanded and expanded where they are used (late binding,
//    as in the stock assembler and in C). %assign and %eval(...) compute a value right away.
//  - Everything beyond the stock assembler's preprocessor is switched off by --standard
//    (pp_set_standard): then this file behaves like the official tool, quirks included.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>

#include "v32as.h"
#include "ppexpr.h"

#define  MAX_INCLUDE_DEPTH   20             // same limit as the stock assembler
#define  MAX_EXPAND_DEPTH    64             // nested argument / %eval expansion
#define  MAX_LINE_BYTES      (1u << 22)     // one expanded line (4 MiB)
#define  MAX_SPLICES         200000         // replacements in one line
#define  HASH_SIZE           256

////////////////////////////////////////////////////////////////////////////////////////
//
// Small helpers
//
static void  *xrealloc (void *ptr, size_t size)
{
    void  *p = realloc (ptr, size ? size : 1);

    if (p == NULL)
    {
        fprintf (stderr, "v32as: out of memory\n");
        exit    (2);
    }

    return (p);
}

static char  *xstrndup (const char *s, size_t n)
{
    char  *p = xrealloc (NULL, n + 1);

    memcpy (p, s, n);
    p[n] = 0;

    return (p);
}

typedef struct
{
    char    *s;
    size_t   len;
    size_t   cap;
}
buf_t;

static void  buf_reserve (buf_t *b, size_t extra)
{
    if (b -> len + extra + 1 > b -> cap)
    {
        size_t  cap = b -> cap ? b -> cap : 256;

        while (b -> len + extra + 1 > cap)
        {
            cap *= 2;
        }

        b -> s   = xrealloc (b -> s, cap);
        b -> cap = cap;
    }
}

static void  buf_put (buf_t *b, const char *s, size_t n)
{
    buf_reserve (b, n);
    memcpy (b -> s + b -> len, s, n);
    b -> len += n;
    b -> s[b -> len] = 0;
}

static void  buf_puts (buf_t *b, const char *s)
{
    buf_put (b, s, strlen (s));
}

static void  buf_init (buf_t *b, const char *s, size_t n)
{
    b -> s = NULL;
    b -> len = b -> cap = 0;
    buf_reserve (b, n);
    b -> s[0] = 0;
    buf_put (b, s, n);
}

// replace [start, end) by r[0..rlen)
static void  buf_splice (buf_t *b, size_t start, size_t end, const char *r, size_t rlen)
{
    size_t  tail = b -> len - end;

    if (rlen > end - start)
    {
        buf_reserve (b, rlen - (end - start));
    }

    memmove (b -> s + start + rlen, b -> s + end, tail + 1);
    memcpy  (b -> s + start, r, rlen);
    b -> len = start + rlen + tail;
}

static int  is_ws          (int c)  { return (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'); }
static int  is_ident_start (int c)  { return (isalpha (c) || c == '_'); }
static int  is_ident_char  (int c)  { return (isalnum (c) || c == '_'); }

// names like FFh are hexadecimal numbers to lexer.l, never identifiers
static int  is_hex_h_number (const char *s, size_t n)
{
    size_t  i;

    if (n < 2 || (s[n - 1] != 'h' && s[n - 1] != 'H'))
    {
        return (0);
    }

    for (i = 0; i + 1 < n; i++)
    {
        if (!isxdigit ((unsigned char) s[i]))
        {
            return (0);
        }
    }

    return (1);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Interned file names (the line map, fixups and debug records keep plain pointers)
//
const char  *pp_intern (const char *s)
{
    static char  **tab;
    static size_t  n;
    static size_t  cap;
    size_t         i;

    for (i = 0; i < n; i++)
    {
        if (strcmp (tab[i], s) == 0)
        {
            return (tab[i]);
        }
    }

    if (n == cap)
    {
        cap = cap ? cap * 2 : 16;
        tab = xrealloc (tab, cap * sizeof (char *));
    }

    tab[n] = xstrndup (s, strlen (s));

    return (tab[n++]);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// State
//
typedef struct macro
{
    char          *name;
    char          *body;
    int            is_func;
    int            nparams;             // named parameters (not counting the variadic tail)
    int            variadic;            // the last parameter is "..." (__VA_ARGS__)
    char         **params;              // nparams names (+ "__VA_ARGS__" when variadic)
    struct macro  *next;
}
macro_t;

typedef struct
{
    int  taken;                 // has some branch of this block been taken (or is the block dead)?
    int  else_seen;
    int  state;                 // are lines inside this branch (now) being kept?
    int  parent_active;
    int  line;                  // where it was opened
}
cond_t;

typedef struct
{
    const char  *name;          // interned
    char        *canon;         // canonical path (for %includeonce), or NULL
    int          line;          // line being processed (so, for the parent frames of an
                                // include, the line holding the %include)
    cond_t      *cond;
    int          ncond;
    int          capcond;
}
frame_t;

struct pp
{
    macro_t   *tab[HASH_SIZE];

    frame_t  **frames;
    int        nframes;
    int        capframes;

    buf_t      out;
    srcloc_t  *map;
    size_t     nmap;
    size_t     capmap;

    unsigned   errors;

    // configuration
    int        standard;
    int        warnings;
    char     **incdirs;
    int        nincdirs;

    // run-time state
    char     **once;                // canonical paths of %includeonce files
    int        nonce;
    unsigned   counter;             // __COUNTER__
    char      *last_global;         // last global label seen, for .local labels
};

////////////////////////////////////////////////////////////////////////////////////////
//
// Macro table
//
static unsigned  hash_name (const char *s)
{
    unsigned  h = 5381;

    while (*s)
    {
        h = h * 33 + (unsigned char) *s++;
    }

    return (h % HASH_SIZE);
}

static macro_t  *macro_find (const pp_t *p, const char *name, size_t n)
{
    macro_t   *m;
    char       tmp[128];
    char      *key = tmp;
    unsigned   h;

    if (n >= sizeof (tmp))
    {
        key = xstrndup (name, n);
    }
    else
    {
        memcpy (tmp, name, n);
        tmp[n] = 0;
    }

    h = hash_name (key);

    for (m = p -> tab[h]; m != NULL; m = m -> next)
    {
        if (strcmp (m -> name, key) == 0)
        {
            break;
        }
    }

    if (key != tmp)
    {
        free (key);
    }

    return (m);
}

static void  macro_free (macro_t *m)
{
    int  i;

    for (i = 0; i < m -> nparams + (m -> variadic ? 1 : 0); i++)
    {
        free (m -> params[i]);
    }

    free (m -> params);
    free (m -> name);
    free (m -> body);
    free (m);
}

static void  macro_remove (pp_t *p, const char *name)
{
    macro_t  **link = &p -> tab[hash_name (name)];

    while (*link != NULL)
    {
        if (strcmp ((*link) -> name, name) == 0)
        {
            macro_t  *dead = *link;

            *link = dead -> next;
            macro_free (dead);
            return;
        }

        link = &(*link) -> next;
    }
}

// Defines (or redefines) an object-like macro; for a function-like one, fill in the
// returned macro's parameters afterwards
static macro_t  *macro_set (pp_t *p, const char *name, const char *body)
{
    macro_t  *m;

    macro_remove (p, name);

    m          = xrealloc (NULL, sizeof (*m));
    memset (m, 0, sizeof (*m));
    m -> name  = xstrndup (name, strlen (name));
    m -> body  = xstrndup (body, strlen (body));
    m -> next  = p -> tab[hash_name (name)];
    p -> tab[hash_name (name)] = m;

    return (m);
}

void  pp_undefine (pp_t *p, const char *name)
{
    macro_remove (p, name);
}

int  pp_define_cmdline (pp_t *p, const char *spec)
{
    const char  *eq = strchr (spec, '=');
    size_t       n  = eq ? (size_t) (eq - spec) : strlen (spec);
    size_t       i;
    char        *name;

    if (n == 0 || !is_ident_start ((unsigned char) spec[0]))
    {
        return (-1);
    }

    for (i = 1; i < n; i++)
    {
        if (!is_ident_char ((unsigned char) spec[i]))
        {
            return (-1);
        }
    }

    name = xstrndup (spec, n);
    macro_set (p, name, eq ? eq + 1 : "1");
    free (name);

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Life cycle and configuration
//
pp_t  *pp_new (void)
{
    pp_t  *p = xrealloc (NULL, sizeof (*p));

    memset (p, 0, sizeof (*p));
    p -> warnings = 1;

    return (p);
}

void  pp_free (pp_t *p)
{
    int  i;

    if (p == NULL)
    {
        return;
    }

    for (i = 0; i < HASH_SIZE; i++)
    {
        while (p -> tab[i] != NULL)
        {
            macro_t  *m = p -> tab[i];

            p -> tab[i] = m -> next;
            macro_free (m);
        }
    }

    for (i = 0; i < p -> nframes; i++)
    {
        free (p -> frames[i] -> cond);
        free (p -> frames[i] -> canon);
        free (p -> frames[i]);
    }

    for (i = 0; i < p -> nincdirs; i++)
    {
        free (p -> incdirs[i]);
    }

    for (i = 0; i < p -> nonce; i++)
    {
        free (p -> once[i]);
    }

    free (p -> incdirs);
    free (p -> once);
    free (p -> last_global);
    free (p -> frames);
    free (p -> out.s);
    free (p -> map);
    free (p);
}

void  pp_set_standard (pp_t *p, int on)
{
    p -> standard = on;
}

void  pp_set_warnings (pp_t *p, int on)
{
    p -> warnings = on;
}

void  pp_add_include_dir (pp_t *p, const char *dir)
{
    p -> incdirs = xrealloc (p -> incdirs, (size_t) (p -> nincdirs + 1) * sizeof (char *));
    p -> incdirs[p -> nincdirs++] = xstrndup (dir, strlen (dir));
}

// Symbols describing the build: __V32AS__ (the version's date as a number), __BASE__
// (the first address), and one of __BIOS__ / __CART__ / __ORIGIN__ / __NONE__
void  pp_set_target (pp_t *p, const char *kind, unsigned base)
{
    char  name[32];
    char  val[32];

    if (p -> standard)
    {
        return;                                     // the official assembler defines nothing
    }

    snprintf (val, sizeof (val), "%lu", strtoul (VERSION, NULL, 10));
    macro_set (p, "__V32AS__", val);

    snprintf (val, sizeof (val), "0x%08X", base);
    macro_set (p, "__BASE__", val);

    snprintf (name, sizeof (name), "__%s__", kind);
    macro_set (p, name, "1");
}

unsigned  pp_errors (const pp_t *p)
{
    return (p -> errors);
}

const char  *pp_output (const pp_t *p, size_t *len)
{
    *len = p -> out.len;
    return (p -> out.s != NULL ? p -> out.s : "");
}

const srcloc_t  *pp_linemap (const pp_t *p, size_t *nlines)
{
    *nlines = p -> nmap;
    return (p -> map);
}

void  pp_output_clear (pp_t *p)
{
    p -> out.len = 0;

    if (p -> out.s != NULL)
    {
        p -> out.s[0] = 0;
    }

    p -> nmap = 0;
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Frames: one per file being read (the main file, then each %include on top of it)
//
static frame_t  *frame_top (pp_t *p)
{
    return (p -> frames[p -> nframes - 1]);
}

static frame_t  *frame_push (pp_t *p, const char *name)
{
    frame_t  *f = xrealloc (NULL, sizeof (*f));

    memset (f, 0, sizeof (*f));
    f -> name = pp_intern (name);

    if (p -> nframes == p -> capframes)
    {
        p -> capframes = p -> capframes ? p -> capframes * 2 : 8;
        p -> frames    = xrealloc (p -> frames, (size_t) p -> capframes * sizeof (frame_t *));
    }

    p -> frames[p -> nframes++] = f;

    return (f);
}

static void  frame_pop (pp_t *p)
{
    frame_t  *f = p -> frames[--p -> nframes];

    free (f -> cond);
    free (f -> canon);
    free (f);
}

static int  frame_active (const frame_t *f)
{
    return (f -> ncond == 0 || f -> cond[f -> ncond - 1].state);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Diagnostics: file:line:col: error: text (the same shape as the assembler's own),
// followed by the include chain
//
static void  vmsg (pp_t *p, const char *kind, int line, int col, const char *fmt, va_list ap)
{
    frame_t  *f = frame_top (p);
    int       k;

    fflush (stdout);

    if (col > 0)
    {
        fprintf (stderr, "%s:%d:%d: %s: ", f -> name, line, col, kind);
    }
    else
    {
        fprintf (stderr, "%s:%d: %s: ", f -> name, line, kind);
    }

    vfprintf (stderr, fmt, ap);
    fputc    ('\n', stderr);

    for (k = p -> nframes - 2; k >= 0; k--)
    {
        fprintf (stderr, "%s:%d: note: included from here\n", p -> frames[k] -> name,
                 p -> frames[k] -> line);
    }
}

static void  pp_error_at (pp_t *p, int line, int col, const char *fmt, ...)
    __attribute__ ((format (printf, 4, 5)));

static void  pp_error_at (pp_t *p, int line, int col, const char *fmt, ...)
{
    va_list  ap;

    va_start (ap, fmt);
    vmsg     (p, "error", line, col, fmt, ap);
    va_end   (ap);

    p -> errors++;
}

static void  pp_error (pp_t *p, int col, const char *fmt, ...)
    __attribute__ ((format (printf, 3, 4)));

static void  pp_error (pp_t *p, int col, const char *fmt, ...)
{
    va_list  ap;

    va_start (ap, fmt);
    vmsg     (p, "error", frame_top (p) -> line, col, fmt, ap);
    va_end   (ap);

    p -> errors++;
}

static void  pp_warning_at (pp_t *p, int line, int col, const char *fmt, ...)
    __attribute__ ((format (printf, 4, 5)));

static void  pp_warning_at (pp_t *p, int line, int col, const char *fmt, ...)
{
    va_list  ap;

    if (!p -> warnings)
    {
        return;
    }

    va_start (ap, fmt);
    vmsg     (p, "warning", line, col, fmt, ap);
    va_end   (ap);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Lexical scanning of one source line
//
// Index just past the string literal starting at s[i] (an unterminated one runs to the end)
static size_t  skip_string (const char *s, size_t n, size_t i)
{
    i++;

    while (i < n && s[i] != '"')
    {
        if (s[i] == '\\' && i + 1 < n)
        {
            i++;
        }

        i++;
    }

    return (i < n ? i + 1 : n);
}

// Length of the character literal starting at s[i] ('c', '\c' or '\xHH'), or 0 if it
// is not one (the same shapes lexer.l accepts)
static size_t  char_literal_len (const char *s, size_t n, size_t i)
{
    if (i + 2 < n && s[i + 1] == '\\')
    {
        if (i + 5 < n && s[i + 2] == 'x' && isxdigit ((unsigned char) s[i + 3])
                      && isxdigit ((unsigned char) s[i + 4]) && s[i + 5] == '\'')
        {
            return (6);
        }

        if (i + 3 < n && s[i + 3] == '\'')
        {
            return (4);
        }

        return (0);
    }

    if (i + 2 < n && s[i + 1] != '\'' && s[i + 2] == '\'')
    {
        return (3);
    }

    return (0);
}

// Index of the ';' that starts a comment in s[from..n), or n if there is none
static size_t  find_comment (const char *s, size_t n, size_t from)
{
    size_t  i = from;

    while (i < n)
    {
        if (s[i] == ';')
        {
            return (i);
        }
        else if (s[i] == '"')
        {
            i = skip_string (s, n, i);
        }
        else if (s[i] == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            i += (k ? k : 1);
        }
        else
        {
            i++;
        }
    }

    return (n);
}

static size_t  skip_ws (const char *s, size_t n, size_t i)
{
    while (i < n && is_ws ((unsigned char) s[i]))
    {
        i++;
    }

    return (i);
}

// nothing but blanks and an optional comment from here on?
static int  at_eol (const char *s, size_t n, size_t i)
{
    i = skip_ws (s, n, i);

    return (i >= n || s[i] == ';');
}

// index just past the number starting at s[i]: 0x1F 0b101 3.14 52 2Ah ...
static size_t  skip_number (const char *s, size_t n, size_t i)
{
    while (i < n && (is_ident_char ((unsigned char) s[i]) || s[i] == '.'))
    {
        i++;
    }

    return (i);
}

// Does s[i..] start with "%eval" (any case), blanks, then '('? Returns the index of the '('
// or 0.
static size_t  inline_eval_paren (const char *s, size_t n, size_t i)
{
    if (i + 5 <= n && s[i] == '%' && strncasecmp (s + i + 1, "eval", 4) == 0)
    {
        size_t  k = skip_ws (s, n, i + 5);

        if (k < n && s[k] == '(')
        {
            return (k);
        }
    }

    return (0);
}

// index of the ')' closing the '(' at s[open], or n if there is none
static size_t  matching_paren (const char *s, size_t n, size_t open)
{
    size_t  i     = open + 1;
    int     depth = 1;

    while (i < n)
    {
        if (s[i] == '"')
        {
            i = skip_string (s, n, i);
            continue;
        }
        if (s[i] == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            i += (k ? k : 1);
            continue;
        }
        if (s[i] == '(')
        {
            depth++;
        }
        else if (s[i] == ')' && --depth == 0)
        {
            return (i);
        }
        i++;
    }

    return (n);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Macro expansion
//
typedef struct
{
    const char  *name;
    size_t       nlen;
    size_t       end;           // the region [?, end) of the line produced by that expansion
}
hide_t;

typedef struct
{
    hide_t  *v;
    int      n;
    int      cap;
}
hides_t;

static void  hide_push (hides_t *h, const char *name, size_t nlen, size_t end)
{
    if (h -> n == h -> cap)
    {
        h -> cap = h -> cap ? h -> cap * 2 : 8;
        h -> v   = xrealloc (h -> v, (size_t) h -> cap * sizeof (hide_t));
    }

    h -> v[h -> n].name = name;
    h -> v[h -> n].nlen = nlen;
    h -> v[h -> n].end  = end;
    h -> n++;
}

static int  is_hidden (const hides_t *h, const char *name, size_t nlen, size_t pos)
{
    int  i;

    for (i = 0; i < h -> n; i++)
    {
        if (h -> v[i].end > pos && h -> v[i].nlen == nlen && memcmp (h -> v[i].name, name, nlen) == 0)
        {
            return (1);
        }
    }

    return (0);
}

// the names hidden at pos, as permanent hides (for expanding a macro argument on its own)
static void  hides_live_at (const hides_t *h, size_t pos, hides_t *out)
{
    int  i;

    for (i = 0; i < h -> n; i++)
    {
        if (h -> v[i].end > pos)
        {
            hide_push (out, h -> v[i].name, h -> v[i].nlen, SIZE_MAX);
        }
    }
}

// replace [start, end) by r, keeping every hide region consistent
static void  splice (buf_t *b, size_t start, size_t end, const char *r, size_t rlen, hides_t *h)
{
    int  i;

    for (i = 0; i < h -> n; i++)
    {
        hide_t  *x = &h -> v[i];

        if (x -> end == SIZE_MAX || x -> end <= start)
        {
            continue;
        }

        if (x -> end >= end)
        {
            x -> end = x -> end - (end - start) + rlen;
        }
        else
        {
            x -> end = start;                       // the invocation ended inside, the region did not
        }
    }

    buf_splice (b, start, end, r, rlen);
}

static int  expand_buffer (pp_t *p, buf_t *b, hides_t *hides, int depth, int *changed, const char **why);

static void  escape_into (buf_t *o, const char *s, size_t n)
{
    size_t  i;

    for (i = 0; i < n; i++)
    {
        if (s[i] == '"' || s[i] == '\\')
        {
            buf_put (o, "\\", 1);
        }

        buf_put (o, s + i, 1);
    }
}

// #param: the argument's text as a string literal (blank runs squeezed)
static void  stringize (buf_t *o, const char *raw)
{
    size_t  n = strlen (raw);
    size_t  i = 0;
    int     pending = 0;

    buf_put (o, "\"", 1);

    while (i < n)
    {
        if (is_ws ((unsigned char) raw[i]))
        {
            pending = 1;
            i++;
            continue;
        }

        if (pending && o -> len > 1)
        {
            buf_put (o, " ", 1);
        }
        pending = 0;

        if (raw[i] == '"')
        {
            size_t  j = skip_string (raw, n, i);

            escape_into (o, raw + i, j - i);
            i = j;
        }
        else if (raw[i] == '\'')
        {
            size_t  k = char_literal_len (raw, n, i);

            k = (k ? k : 1);
            escape_into (o, raw + i, k);
            i += k;
        }
        else
        {
            escape_into (o, raw + i, 1);
            i++;
        }
    }

    buf_put (o, "\"", 1);
}

// Splits the argument list whose '(' is at s[open]. On success returns 0 with *args (malloc'd
// array of malloc'd, trimmed strings), *nargs and *close (index of the ')'). -1 if unterminated.
static int  collect_args (const char *s, size_t n, size_t open, int nparams, int variadic,
                          char ***args, int *nargs, size_t *close)
{
    size_t  i     = open + 1;
    size_t  start = i;
    int     depth = 0;
    int     count = 0;
    int     cap   = 4;
    char  **v     = xrealloc (NULL, (size_t) cap * sizeof (char *));

    for (;;)
    {
        int  end_arg = 0;
        int  done    = 0;

        if (i >= n)
        {
            for (count--; count >= 0; count--)
            {
                free (v[count]);
            }
            free (v);
            return (-1);
        }

        if (s[i] == '"')
        {
            i = skip_string (s, n, i);
            continue;
        }

        if (s[i] == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            i += (k ? k : 1);
            continue;
        }

        if (s[i] == '(' || s[i] == '[')
        {
            depth++;
        }
        else if ((s[i] == ')' || s[i] == ']') && depth > 0)
        {
            depth--;
        }
        else if (s[i] == ')')
        {
            end_arg = 1;
            done    = 1;
        }
        else if (s[i] == ',' && depth == 0 && (!variadic || count < nparams))
        {
            end_arg = 1;
        }

        if (end_arg)
        {
            size_t  a = start;
            size_t  e = i;

            while (a < e && is_ws ((unsigned char) s[a]))  a++;
            while (e > a && is_ws ((unsigned char) s[e - 1]))  e--;

            if (count == cap)
            {
                cap *= 2;
                v    = xrealloc (v, (size_t) cap * sizeof (char *));
            }

            v[count++] = xstrndup (s + a, e - a);
            start      = i + 1;
        }

        if (done)
        {
            break;
        }

        i++;
    }

    *args  = v;
    *nargs = count;
    *close = i;

    return (0);
}

static void  free_args (char **v, int n)
{
    int  i;

    for (i = 0; i < n; i++)
    {
        free (v[i]);
    }

    free (v);
}

// Builds the replacement text of a function-like macro for the given arguments
static int  substitute (pp_t *p, const macro_t *m, char **args, int nargs, const hides_t *live,
                        buf_t *o, int depth, const char **why)
{
    const char  *body   = m -> body;
    size_t       n      = strlen (body);
    size_t       i      = 0;
    int          nall   = m -> nparams + (m -> variadic ? 1 : 0);
    char       **expd   = xrealloc (NULL, (size_t) (nall + 1) * sizeof (char *));
    int          prev_paste = 0;
    int          k;
    int          rc = 0;

    for (k = 0; k < nall; k++)
    {
        expd[k] = NULL;
    }

    // __VA_ARGS__ empty when the call gave only the named arguments
    #define  ARG(idx)  ((idx) < nargs ? args[idx] : "")

    while (i < n && rc == 0)
    {
        unsigned char  c = (unsigned char) body[i];

        if (c == '"')
        {
            size_t  j = skip_string (body, n, i);

            buf_put (o, body + i, j - i);
            i = j;
            prev_paste = 0;
        }
        else if (c == '\'')
        {
            size_t  j = char_literal_len (body, n, i);

            j = (j ? j : 1);
            buf_put (o, body + i, j);
            i += j;
            prev_paste = 0;
        }
        else if (isdigit (c))
        {
            size_t  j = skip_number (body, n, i);

            buf_put (o, body + i, j - i);
            i = j;
            prev_paste = 0;
        }
        else if (c == '#' && i + 1 < n && body[i + 1] == '#')       // a ## b
        {
            size_t  j;

            while (o -> len > 0 && is_ws ((unsigned char) o -> s[o -> len - 1]))
            {
                o -> len--;
                o -> s[o -> len] = 0;
            }

            j = skip_ws (body, n, i + 2);

            // GNU: ", ## __VA_ARGS__" drops the comma when there is nothing to pass
            if (m -> variadic && o -> len > 0 && o -> s[o -> len - 1] == ','
                && n - j >= 11 && memcmp (body + j, "__VA_ARGS__", 11) == 0
                && (j + 11 >= n || !is_ident_char ((unsigned char) body[j + 11]))
                && ARG (m -> nparams)[0] == 0)
            {
                o -> len--;
                o -> s[o -> len] = 0;
                j += 11;
            }

            i = j;
            prev_paste = 1;
        }
        else if (c == '#' && !prev_paste)                           // #param: stringize
        {
            size_t  j = skip_ws (body, n, i + 1);
            size_t  e = j;
            int     idx = -1;

            while (e < n && is_ident_char ((unsigned char) body[e]))
            {
                e++;
            }

            if (e > j && is_ident_start ((unsigned char) body[j]))
            {
                for (k = 0; k < nall; k++)
                {
                    if (strlen (m -> params[k]) == e - j && memcmp (m -> params[k], body + j, e - j) == 0)
                    {
                        idx = k;
                        break;
                    }
                }
            }

            if (idx >= 0)
            {
                stringize (o, ARG (idx));
                i = e;
            }
            else
            {
                buf_put (o, "#", 1);        // a plain '#' (as in #42)
                i++;
            }
        }
        else if (is_ident_start (c))
        {
            size_t  j = i;
            int     idx = -1;

            while (j < n && is_ident_char ((unsigned char) body[j]))
            {
                j++;
            }

            for (k = 0; k < nall; k++)
            {
                if (strlen (m -> params[k]) == j - i && memcmp (m -> params[k], body + i, j - i) == 0)
                {
                    idx = k;
                    break;
                }
            }

            if (idx < 0)
            {
                buf_put (o, body + i, j - i);
            }
            else
            {
                // next to ## the argument is used as written; elsewhere it is expanded first
                size_t  after = skip_ws (body, n, j);
                int     raw   = prev_paste || (after + 1 < n && body[after] == '#' && body[after + 1] == '#');

                if (raw)
                {
                    buf_puts (o, ARG (idx));
                }
                else
                {
                    if (expd[idx] == NULL)
                    {
                        buf_t     t;
                        hides_t   h = { 0 };
                        int       ch = 0;

                        buf_init (&t, ARG (idx), strlen (ARG (idx)));

                        {
                            int  q;

                            for (q = 0; q < live -> n; q++)
                            {
                                hide_push (&h, live -> v[q].name, live -> v[q].nlen, SIZE_MAX);
                            }
                        }

                        if (expand_buffer (p, &t, &h, depth + 1, &ch, why) != 0)
                        {
                            rc = -1;
                        }

                        expd[idx] = t.s;
                        free (h.v);
                    }

                    if (rc == 0)
                    {
                        buf_puts (o, expd[idx]);
                    }
                }
            }

            i = j;
            prev_paste = 0;
        }
        else
        {
            if (!is_ws (c))
            {
                prev_paste = 0;
            }

            buf_put (o, body + i, 1);
            i++;
        }
    }

    #undef  ARG

    for (k = 0; k < nall; k++)
    {
        free (expd[k]);
    }

    free (expd);

    return (rc);
}

// `defined NAME` / `defined(NAME)` -> 1 or 0, before macros are expanded (so NAME survives)
static void  resolve_defined (const pp_t *p, buf_t *b)
{
    size_t  i = 0;

    while (i < b -> len)
    {
        const char *s = b -> s;
        size_t      n = b -> len;

        if (s[i] == '"')
        {
            i = skip_string (s, n, i);
        }
        else if (s[i] == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            i += (k ? k : 1);
        }
        else if (isdigit ((unsigned char) s[i]))
        {
            i = skip_number (s, n, i);
        }
        else if (is_ident_start ((unsigned char) s[i]))
        {
            size_t  j = i;

            while (j < n && is_ident_char ((unsigned char) s[j]))
            {
                j++;
            }

            if (j - i == 7 && memcmp (s + i, "defined", 7) == 0)
            {
                size_t  k      = skip_ws (s, n, j);
                int     paren  = (k < n && s[k] == '(');
                size_t  ns;
                size_t  ne;

                if (paren)
                {
                    k = skip_ws (s, n, k + 1);
                }

                ns = k;
                ne = k;

                while (ne < n && is_ident_char ((unsigned char) s[ne]))
                {
                    ne++;
                }

                if (ne > ns)
                {
                    int  value = (macro_find (p, s + ns, ne - ns) != NULL);

                    k = ne;

                    if (paren)
                    {
                        k = skip_ws (s, n, k);

                        if (k < n && s[k] == ')')
                        {
                            k++;
                        }
                    }

                    buf_splice (b, i, k, value ? "1" : "0", 1);
                    i++;
                    continue;
                }
            }

            i = j;
        }
        else
        {
            i++;
        }
    }
}

// Evaluates an expression written in source text: defined() first, then macros, then arithmetic
static int  eval_text (pp_t *p, const char *text, size_t n, ppval_t *v, int depth, const char **why,
                       char *err, size_t errsize)
{
    buf_t    b;
    hides_t  h       = { 0 };
    int      changed = 0;
    int      rc;

    buf_init (&b, text, n);
    resolve_defined (p, &b);

    if (expand_buffer (p, &b, &h, depth + 1, &changed, why) != 0)
    {
        free (b.s);
        free (h.v);
        return (-1);
    }

    rc = ppexpr_eval (b.s, b.len, v, err, errsize);

    free (b.s);
    free (h.v);

    return (rc);
}

static int  expand_buffer (pp_t *p, buf_t *b, hides_t *hides, int depth, int *changed, const char **why)
{
    size_t  pos     = 0;
    long    splices = 0;

    if (depth > MAX_EXPAND_DEPTH)
    {
        *why = "macro expansion is nested too deeply";
        return (-1);
    }

    while (pos < b -> len)
    {
        const char     *s = b -> s;
        size_t          n = b -> len;
        unsigned char   c = (unsigned char) s[pos];

        if (n > MAX_LINE_BYTES)
        {
            *why = "macro expansion is too large";
            return (-1);
        }

        if (c == ';')                                   // comment: the rest is verbatim
        {
            return (0);
        }

        if (c == '"')
        {
            pos = skip_string (s, n, pos);
            continue;
        }

        if (c == '\'')
        {
            size_t  k = char_literal_len (s, n, pos);

            pos += (k ? k : 1);
            continue;
        }

        if (isdigit (c))                                // numbers: 0x1F 0b101 3.14 ...
        {
            pos = skip_number (s, n, pos);
            continue;
        }

        // %eval(expression): replaced by the value
        if (c == '%' && !p -> standard)
        {
            size_t  open = inline_eval_paren (s, n, pos);

            if (open)
            {
                size_t   close = matching_paren (s, n, open);
                ppval_t  v;
                char     err[160];
                char     text[200];

                if (close >= n)
                {
                    *why = "%eval( is missing its closing ')'";
                    return (-1);
                }

                if (eval_text (p, s + open + 1, close - open - 1, &v, depth, why, err, sizeof (err)) != 0)
                {
                    if (*why == NULL)
                    {
                        snprintf (text, sizeof (text), "%%eval: %s", err);
                        *why = pp_intern (text);
                    }
                    return (-1);
                }

                ppexpr_format (&v, text, sizeof (text));
                splice (b, pos, close + 1, text, strlen (text), hides);
                pos += strlen (text);
                *changed = 1;
                continue;
            }

            pos++;
            continue;
        }

        if (is_ident_start (c))
        {
            size_t    start = pos;
            size_t    end   = pos;
            size_t    nlen;
            macro_t  *m;

            while (end < n && is_ident_char ((unsigned char) s[end]))
            {
                end++;
            }

            nlen = end - start;

            if (!p -> standard && is_hex_h_number (s + start, nlen))
            {
                pos = end;
                continue;
            }

            m = macro_find (p, s + start, nlen);

            if (m == NULL)
            {
                // builtins (unless the program defined a macro of that name)
                if (!p -> standard && nlen > 4 && s[start] == '_' && s[start + 1] == '_')
                {
                    char  val[600];
                    int   have = 1;

                    if (nlen == 8 && memcmp (s + start, "__LINE__", 8) == 0)
                    {
                        snprintf (val, sizeof (val), "%d", frame_top (p) -> line);
                    }
                    else if (nlen == 8 && memcmp (s + start, "__FILE__", 8) == 0)
                    {
                        buf_t  q = { 0 };

                        buf_put (&q, "\"", 1);
                        escape_into (&q, frame_top (p) -> name, strlen (frame_top (p) -> name));
                        buf_put (&q, "\"", 1);
                        snprintf (val, sizeof (val), "%.590s", q.s);
                        free (q.s);
                    }
                    else if (nlen == 11 && memcmp (s + start, "__COUNTER__", 11) == 0)
                    {
                        snprintf (val, sizeof (val), "%u", p -> counter++);
                    }
                    else if (nlen == 17 && memcmp (s + start, "__INCLUDE_LEVEL__", 17) == 0)
                    {
                        snprintf (val, sizeof (val), "%d", p -> nframes - 1);
                    }
                    else
                    {
                        have = 0;
                    }

                    if (have)
                    {
                        splice (b, start, end, val, strlen (val), hides);
                        pos = start + strlen (val);
                        *changed = 1;
                        continue;
                    }
                }

                pos = end;
                continue;
            }

            if (is_hidden (hides, m -> name, nlen, start))
            {
                pos = end;
                continue;
            }

            if (++splices > MAX_SPLICES)
            {
                *why = "runaway macro expansion";
                return (-1);
            }

            if (!m -> is_func)
            {
                size_t  blen = strlen (m -> body);

                splice (b, start, end, m -> body, blen, hides);
                hide_push (hides, m -> name, nlen, start + blen);
                *changed = 1;
                pos = start;
                continue;
            }

            // function-like: an invocation only if '(' comes next
            {
                size_t    k = skip_ws (s, n, end);
                char    **args = NULL;
                int       nargs = 0;
                size_t    close = 0;
                int       given;
                int       ok;
                buf_t     repl = { 0 };
                hides_t   live = { 0 };
                int       rc;

                if (k >= n || s[k] != '(')
                {
                    pos = end;
                    continue;
                }

                if (collect_args (s, n, k, m -> nparams, m -> variadic, &args, &nargs, &close) != 0)
                {
                    static char  msg[160];

                    snprintf (msg, sizeof (msg), "unterminated argument list invoking macro '%s'", m -> name);
                    *why = msg;
                    return (-1);
                }

                given = nargs;

                if (given == 1 && args[0][0] == 0 && m -> nparams == 0)
                {
                    given = 0;                              // F() calls with no arguments
                }

                ok = m -> variadic ? (given >= m -> nparams) : (given == m -> nparams);

                if (!ok)
                {
                    static char  msg[200];

                    snprintf (msg, sizeof (msg), "macro '%s' expects %s%d argument%s, but %d given", m -> name,
                              m -> variadic ? "at least " : "", m -> nparams, m -> nparams == 1 ? "" : "s", given);
                    free_args (args, nargs);
                    *why = msg;
                    return (-1);
                }

                hides_live_at (hides, start, &live);
                rc = substitute (p, m, args, given, &live, &repl, depth, why);
                free (live.v);
                free_args (args, nargs);

                if (rc != 0)
                {
                    free (repl.s);
                    return (-1);
                }

                {
                    size_t  rlen = repl.len;

                    splice (b, start, close + 1, repl.s ? repl.s : "", rlen, hides);
                    hide_push (hides, m -> name, nlen, start + rlen);
                    free (repl.s);
                    *changed = 1;
                    pos = start;
                }
            }

            continue;
        }

        pos++;
    }

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// File-local labels: ".loop" means "the loop of the last global label"
//
//         _draw:                          _draw:
//         .again:                  ==>    _draw__again:
//             jt R1, .again                  jt R1, _draw__again
//
// A global label is any "_name:". The expansion is an ordinary label, so everything
// downstream (symbol table, debug file) sees plain names.
//
static int  localize (pp_t *p, const buf_t *in, buf_t *out)
{
    const char  *s = in -> s;
    size_t       n = in -> len;
    size_t       i = 0;

    while (i < n)
    {
        unsigned char  c = (unsigned char) s[i];

        if (c == ';')
        {
            buf_put (out, s + i, n - i);
            return (0);
        }
        else if (c == '"')
        {
            size_t  j = skip_string (s, n, i);

            buf_put (out, s + i, j - i);
            i = j;
        }
        else if (c == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            k = (k ? k : 1);
            buf_put (out, s + i, k);
            i += k;
        }
        else if (isdigit (c))
        {
            size_t  j = skip_number (s, n, i);

            buf_put (out, s + i, j - i);
            i = j;
        }
        else if (is_ident_start (c))
        {
            size_t  j = i;

            while (j < n && is_ident_char ((unsigned char) s[j]))
            {
                j++;
            }

            if (c == '_' && j - i >= 2 && j < n && s[j] == ':')         // a global label definition
            {
                free (p -> last_global);
                p -> last_global = xstrndup (s + i, j - i);
            }

            buf_put (out, s + i, j - i);
            i = j;
        }
        else if (c == '.' && i + 1 < n && is_ident_start ((unsigned char) s[i + 1])
                 && (i == 0 || !(is_ident_char ((unsigned char) s[i - 1]) || s[i - 1] == '.')))
        {
            size_t  j = i + 1;

            while (j < n && is_ident_char ((unsigned char) s[j]))
            {
                j++;
            }

            if (p -> last_global == NULL)
            {
                pp_error (p, (int) i + 1, "local label '%.*s' used before any global label", (int) (j - i), s + i);
                return (-1);
            }

            buf_puts (out, p -> last_global);
            buf_put  (out, "__", 2);
            buf_put  (out, s + i + 1, j - i - 1);
            i = j;
        }
        else
        {
            buf_put (out, s + i, 1);
            i++;
        }
    }

    return (0);
}

static void  map_push (pp_t *p, const char *file, int line, int expanded)
{
    if (p -> nmap == p -> capmap)
    {
        p -> capmap = p -> capmap ? p -> capmap * 2 : 256;
        p -> map    = xrealloc (p -> map, p -> capmap * sizeof (srcloc_t));
    }

    p -> map[p -> nmap].file     = file;
    p -> map[p -> nmap].line     = line;
    p -> map[p -> nmap].expanded = expanded;
    p -> nmap++;
}

static void  emit_line (pp_t *p, frame_t *f, const char *s, size_t n)
{
    buf_t        b;
    hides_t      h       = { 0 };
    int          changed = 0;
    const char  *why     = NULL;
    int          rc;

    buf_init (&b, s, n);
    rc = expand_buffer (p, &b, &h, 0, &changed, &why);
    free (h.v);

    if (rc != 0)
    {
        pp_error (p, 0, "%s", why);
        free (b.s);
        return;
    }

    if (!p -> standard)
    {
        buf_t  loc = { 0 };

        if (localize (p, &b, &loc) != 0)
        {
            free (loc.s);
            free (b.s);
            return;
        }

        if (loc.len != b.len || memcmp (loc.s ? loc.s : "", b.s, b.len) != 0)
        {
            changed = 1;
        }

        free (b.s);
        b = loc;
        if (b.s == NULL)
        {
            buf_init (&b, "", 0);
        }
    }

    buf_put (&p -> out, b.s, b.len);
    buf_put (&p -> out, "\n", 1);
    map_push (p, f -> name, f -> line, changed);

    free (b.s);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Directives
//
static int  expect_eol (pp_t *p, const char *s, size_t n, size_t i)
{
    i = skip_ws (s, n, i);

    if (i < n && s[i] != ';')
    {
        pp_error (p, (int) i + 1, "expected end of line");
        return (0);
    }

    return (1);
}

// Reads an identifier at s[*i] after blanks; NULL (with an error recorded) if there is none
static char  *expect_ident (pp_t *p, const char *s, size_t n, size_t *i, const char *missing)
{
    size_t  j;
    size_t  start = skip_ws (s, n, *i);

    if (start >= n || s[start] == ';')
    {
        pp_error (p, (int) start + 1, "%s", missing);
        return (NULL);
    }

    if (!is_ident_start ((unsigned char) s[start]))
    {
        pp_error (p, (int) start + 1, "expected identifier");
        return (NULL);
    }

    j = start;

    while (j < n && is_ident_char ((unsigned char) s[j]))
    {
        j++;
    }

    *i = j;

    return (xstrndup (s + start, j - start));
}

// Reads a string literal at s[*i] (after blanks) and decodes the same escapes as the
// scanner does. NULL (with an error recorded) when there is none.
static char  *expect_string (pp_t *p, const char *s, size_t n, size_t *i, const char *missing)
{
    size_t  k = skip_ws (s, n, *i);
    size_t  end;
    buf_t   b = { 0 };

    if (k >= n || s[k] == ';')
    {
        pp_error (p, (int) k + 1, "%s", missing);
        return (NULL);
    }

    if (s[k] != '"')
    {
        pp_error (p, (int) k + 1, "expected a string");
        return (NULL);
    }

    end = skip_string (s, n, k);

    if (end - k < 2 || s[end - 1] != '"')
    {
        pp_error (p, (int) k + 1, "string not terminated");
        return (NULL);
    }

    for (k++; k < end - 1; k++)
    {
        char  c = s[k];

        if (c == '\\' && k + 1 < end - 1)
        {
            c = s[++k];

            switch (c)
            {
                case 'n':  c = '\n';  break;
                case 'r':  c = '\r';  break;
                case 't':  c = '\t';  break;

                case 'x':
                    if (k + 2 < end - 1 && isxdigit ((unsigned char) s[k + 1])
                                        && isxdigit ((unsigned char) s[k + 2]))
                    {
                        char  hex[3] = { s[k + 1], s[k + 2], 0 };

                        c  = (char) strtol (hex, NULL, 16);
                        k += 2;
                    }
                    break;

                default:  break;                // \\ \" \' and anything else: the character itself
            }
        }

        buf_put (&b, &c, 1);
    }

    *i = end;

    if (b.s == NULL)
    {
        return (xstrndup ("", 0));
    }

    return (b.s);
}

static void  cond_open (frame_t *f, int value, int evaluated)
{
    cond_t  *c;

    if (f -> ncond == f -> capcond)
    {
        f -> capcond = f -> capcond ? f -> capcond * 2 : 8;
        f -> cond    = xrealloc (f -> cond, (size_t) f -> capcond * sizeof (cond_t));
    }

    c = &f -> cond[f -> ncond];
    c -> parent_active = frame_active (f);
    c -> taken         = evaluated ? value : 1;         // inside a dead block no branch may be taken
    c -> else_seen     = 0;
    c -> state         = c -> parent_active && value;
    c -> line          = f -> line;
    f -> ncond++;
}

static int  dir_is (const pp_t *p, const char *name, const char *want)
{
    return (p -> standard ? strcmp (name, want) == 0 : strcasecmp (name, want) == 0);
}

// The expression that follows a directive: value of s[i..] up to the comment
static int  eval_rest (pp_t *p, const char *s, size_t n, size_t i, ppval_t *v)
{
    size_t       b   = skip_ws (s, n, i);
    size_t       e   = find_comment (s, n, b);
    const char  *why = NULL;
    char         err[160];

    while (e > b && is_ws ((unsigned char) s[e - 1]))
    {
        e--;
    }

    if (e == b)
    {
        pp_error (p, (int) b + 1, "expected an expression");
        return (-1);
    }

    if (eval_text (p, s + b, e - b, v, 0, &why, err, sizeof (err)) != 0)
    {
        pp_error (p, (int) b + 1, "%s", why ? why : err);
        return (-1);
    }

    return (0);
}

static unsigned  process_text (pp_t *p, const char *name, const char *text, size_t len);

static int  read_whole_file (const char *path, buf_t *data)
{
    FILE   *fp = fopen (path, "rb");
    char    chunk[4096];
    size_t  got;

    if (fp == NULL)
    {
        return (-1);
    }

    while ((got = fread (chunk, 1, sizeof (chunk), fp)) > 0)
    {
        buf_put (data, chunk, got);
    }

    fclose (fp);

    return (0);
}

static char  *join_path (const char *dir, size_t dirlen, const char *name)
{
    char  *full = xrealloc (NULL, dirlen + strlen (name) + 2);

    memcpy (full, dir, dirlen);

    if (dirlen > 0 && dir[dirlen - 1] != '/')
    {
        full[dirlen++] = '/';
    }

    strcpy (full + dirlen, name);

    return (full);
}

static int  once_contains (const pp_t *p, const char *canon)
{
    int  i;

    for (i = 0; i < p -> nonce; i++)
    {
        if (strcmp (p -> once[i], canon) == 0)
        {
            return (1);
        }
    }

    return (0);
}

static void  do_include (pp_t *p, frame_t *f, const char *s, size_t n, size_t i, size_t dstart)
{
    char        *path  = NULL;
    char        *found = NULL;
    const char  *slash;
    size_t       k;
    int          angle = 0;
    int          d;
    buf_t        data  = { 0 };

    // same nesting limit as the stock assembler (it counts the main file too)
    if (p -> nframes > MAX_INCLUDE_DEPTH)
    {
        pp_error (p, (int) dstart + 1,
                  "includes are too deeply nested (possible circular references)");
        return;
    }

    k = skip_ws (s, n, i);

    if (!p -> standard && k < n && s[k] == '<')                 // %include <name>: search path only
    {
        size_t  e = k + 1;

        while (e < n && s[e] != '>')
        {
            e++;
        }

        if (e >= n)
        {
            pp_error (p, (int) k + 1, "missing '>' in include path");
            return;
        }

        path  = xstrndup (s + k + 1, e - k - 1);
        angle = 1;
        i     = e + 1;
    }
    else
    {
        path = expect_string (p, s, n, &i, "include file path is missing");

        if (path == NULL)
        {
            return;
        }
    }

    if (!expect_eol (p, s, n, i))
    {
        free (path);
        return;
    }

    // relative paths: the including file's directory first (not for <...>), then the -I list
    slash = strrchr (f -> name, '/');

    if (path[0] == '/')
    {
        found = xstrndup (path, strlen (path));
    }
    else
    {
        if (!angle)
        {
            char  *cand = join_path (f -> name, slash ? (size_t) (slash - f -> name) : 0, path);
            FILE  *fp   = fopen (cand, "rb");

            if (fp != NULL)
            {
                fclose (fp);
                found = cand;
            }
            else
            {
                free (cand);
            }
        }

        for (d = 0; found == NULL && d < p -> nincdirs && !p -> standard; d++)
        {
            char  *cand = join_path (p -> incdirs[d], strlen (p -> incdirs[d]), path);
            FILE  *fp   = fopen (cand, "rb");

            if (fp != NULL)
            {
                fclose (fp);
                found = cand;
            }
            else
            {
                free (cand);
            }
        }
    }

    if (found == NULL || read_whole_file (found, &data) != 0)
    {
        pp_error (p, (int) k + 1, "cannot open include file \"%s\"", path);
        free (path);
        free (found);
        free (data.s);
        return;
    }

    // %includeonce: a file that asked for it is read only the first time
    {
        char  *canon = realpath (found, NULL);

        if (canon != NULL && once_contains (p, canon))
        {
            free (canon);
        }
        else
        {
            free (canon);
            process_text (p, found, data.s ? data.s : "", data.len);
        }
    }

    free (data.s);
    free (path);
    free (found);
}

// "name" or "name(params)" after %define; fills the macro's parameter list
static int  parse_params (pp_t *p, const char *s, size_t n, size_t *i, macro_t *m)
{
    size_t  k = skip_ws (s, n, *i + 1);              // after the '('
    int     cap = 0;

    m -> is_func = 1;

    if (k < n && s[k] == ')')
    {
        *i = k + 1;
        return (0);
    }

    for (;;)
    {
        size_t  e;
        int     j;

        k = skip_ws (s, n, k);

        if (k + 2 < n && s[k] == '.' && s[k + 1] == '.' && s[k + 2] == '.')
        {
            m -> variadic = 1;
            k += 3;
            m -> params = xrealloc (m -> params, (size_t) (m -> nparams + 1) * sizeof (char *));
            m -> params[m -> nparams] = xstrndup ("__VA_ARGS__", 11);
            k = skip_ws (s, n, k);

            if (k >= n || s[k] != ')')
            {
                pp_error (p, (int) k + 1, "'...' must be the last parameter");
                return (-1);
            }

            *i = k + 1;
            return (0);
        }

        if (k >= n || !is_ident_start ((unsigned char) s[k]))
        {
            pp_error (p, (int) k + 1, "expected a parameter name");
            return (-1);
        }

        e = k;

        while (e < n && is_ident_char ((unsigned char) s[e]))
        {
            e++;
        }

        for (j = 0; j < m -> nparams; j++)
        {
            if (strlen (m -> params[j]) == e - k && memcmp (m -> params[j], s + k, e - k) == 0)
            {
                pp_error (p, (int) k + 1, "duplicate parameter name '%.*s'", (int) (e - k), s + k);
                return (-1);
            }
        }

        (void) cap;
        m -> params = xrealloc (m -> params, (size_t) (m -> nparams + 2) * sizeof (char *));
        m -> params[m -> nparams++] = xstrndup (s + k, e - k);

        k = skip_ws (s, n, e);

        if (k < n && s[k] == ',')
        {
            k++;
            continue;
        }

        if (k < n && s[k] == ')')
        {
            *i = k + 1;
            return (0);
        }

        pp_error (p, (int) k + 1, "expected ',' or ')' in the parameter list");
        return (-1);
    }
}

// stock-assembler restrictions on a definition's value (only enforced by --standard)
static int  check_stock_body (pp_t *p, const char *name, const char *body, size_t col)
{
    size_t  n = strlen (body);
    size_t  i = 0;

    while (i < n)
    {
        if (body[i] == '"')
        {
            i = skip_string (body, n, i);
        }
        else if (body[i] == '\'')
        {
            size_t  k = char_literal_len (body, n, i);

            i += (k ? k : 1);
        }
        else if (body[i] == '%')
        {
            pp_error (p, (int) col, "definitions cannot contain the percent symbol (%%)");
            return (-1);
        }
        else if (is_ident_start ((unsigned char) body[i]))
        {
            size_t  j = i;

            while (j < n && is_ident_char ((unsigned char) body[j]))
            {
                j++;
            }

            if (strlen (name) == j - i && memcmp (name, body + i, j - i) == 0)
            {
                pp_error (p, (int) col, "a definition cannot contain itself (circular reference)");
                return (-1);
            }

            i = j;
        }
        else
        {
            i++;
        }
    }

    return (0);
}

static void  do_define (pp_t *p, const char *s, size_t n, size_t j)
{
    char     *name = expect_ident (p, s, n, &j, "definition name is missing");
    macro_t  *m;
    size_t    b;
    size_t    e;
    char     *body;
    macro_t   proto;

    if (name == NULL)
    {
        return;
    }

    memset (&proto, 0, sizeof (proto));

    // NAME( with no blank in between starts a parameter list (stock reads that as the
    // object-like macro NAME whose value begins with a parenthesis)
    if (!p -> standard && j < n && s[j] == '(')
    {
        if (parse_params (p, s, n, &j, &proto) != 0)
        {
            int  q;

            for (q = 0; q < proto.nparams + (proto.variadic ? 1 : 0); q++)
            {
                free (proto.params[q]);
            }

            free (proto.params);
            free (name);
            return;
        }
    }

    b = skip_ws (s, n, j);
    e = find_comment (s, n, b);

    while (e > b && is_ws ((unsigned char) s[e - 1]))
    {
        e--;
    }

    body = xstrndup (s + b, e - b);

    if (p -> standard && check_stock_body (p, name, body, b + 1) != 0)
    {
        free (body);
        free (name);
        return;
    }

    m = macro_set (p, name, body);

    if (proto.is_func)
    {
        m -> is_func  = 1;
        m -> nparams  = proto.nparams;
        m -> variadic = proto.variadic;
        m -> params   = proto.params;
    }

    free (body);
    free (name);
}

static void  directive (pp_t *p, frame_t *f, const char *s, size_t n, size_t dstart)
{
    size_t  i = skip_ws (s, n, dstart + 1);
    size_t  j;
    char    name[32];
    int     active = frame_active (f);
    int     is_cond;

    // an empty directive ("%" alone) is legal and does nothing
    if (at_eol (s, n, i))
    {
        return;
    }

    if (!is_ident_start ((unsigned char) s[i]))
    {
        pp_error (p, (int) i + 1, "expected identifier");
        return;
    }

    for (j = i; j < n && is_ident_char ((unsigned char) s[j]); j++)
    {
    }

    if (j - i >= sizeof (name))
    {
        snprintf (name, sizeof (name), "?");
    }
    else
    {
        memcpy (name, s + i, j - i);
        name[j - i] = 0;
    }

    is_cond = dir_is (p, name, "ifdef") || dir_is (p, name, "ifndef")
           || dir_is (p, name, "else")  || dir_is (p, name, "endif")
           || (!p -> standard && (dir_is (p, name, "if") || dir_is (p, name, "elif")
                               || dir_is (p, name, "elifdef") || dir_is (p, name, "elifndef")));

    // inside a skipped block only the conditionals matter (so nesting stays balanced);
    // anything else, even an unknown directive, is dropped
    if (!active && !is_cond)
    {
        return;
    }

    if (dir_is (p, name, "include"))
    {
        do_include (p, f, s, n, j, dstart);
    }
    else if (dir_is (p, name, "define"))
    {
        do_define (p, s, n, j);
    }
    else if (dir_is (p, name, "undef"))
    {
        char  *macro = expect_ident (p, s, n, &j, "definition name is missing");

        if (macro != NULL)
        {
            if (expect_eol (p, s, n, j))
            {
                macro_remove (p, macro);
            }

            free (macro);
        }
    }
    else if (dir_is (p, name, "ifdef") || dir_is (p, name, "ifndef")
             || (!p -> standard && (dir_is (p, name, "elifdef") || dir_is (p, name, "elifndef"))))
    {
        int    negate = dir_is (p, name, "ifndef") || dir_is (p, name, "elifndef");
        int    is_elif = (name[0] == 'e' || name[0] == 'E') && (name[1] == 'l' || name[1] == 'L')
                         && (name[2] == 'i' || name[2] == 'I');
        char  *macro  = expect_ident (p, s, n, &j, "expected an identifier");
        int    value  = 0;

        if (macro != NULL)
        {
            if (expect_eol (p, s, n, j))
            {
                value = ((macro_find (p, macro, strlen (macro)) != NULL) != negate);
            }

            free (macro);
        }

        if (!is_elif)
        {
            cond_open (f, value, 1);                // always opened, even after an error, so %else/%endif stay balanced
        }
        else if (f -> ncond == 0)
        {
            pp_error (p, (int) dstart + 1, "%%%s with no previous %%if", name);
        }
        else if (f -> cond[f -> ncond - 1].else_seen)
        {
            pp_error (p, (int) dstart + 1, "%%%s after %%else", name);
        }
        else
        {
            cond_t  *c = &f -> cond[f -> ncond - 1];

            if (!c -> parent_active || c -> taken)
            {
                c -> state = 0;
            }
            else
            {
                c -> state = value;
                c -> taken = value;
            }
        }
    }
    else if (!p -> standard && dir_is (p, name, "if"))
    {
        ppval_t  v = { 0, 0, 0.0f };
        int      value = 0;
        int      parent = frame_active (f);

        // inside a skipped block the expression is not even looked at
        if (parent && eval_rest (p, s, n, j, &v) == 0)
        {
            value = ppexpr_truth (&v);
        }

        cond_open (f, value, parent);
    }
    else if (!p -> standard && dir_is (p, name, "elif"))
    {
        if (f -> ncond == 0)
        {
            pp_error (p, (int) dstart + 1, "%%elif with no previous %%if");
        }
        else if (f -> cond[f -> ncond - 1].else_seen)
        {
            pp_error (p, (int) dstart + 1, "%%elif after %%else");
        }
        else
        {
            cond_t  *c = &f -> cond[f -> ncond - 1];

            if (!c -> parent_active || c -> taken)
            {
                c -> state = 0;
            }
            else
            {
                ppval_t  v     = { 0, 0, 0.0f };
                int      value = 0;

                if (eval_rest (p, s, n, j, &v) == 0)
                {
                    value = ppexpr_truth (&v);
                }

                c -> state = value;
                c -> taken = value;
            }
        }
    }
    else if (dir_is (p, name, "else"))
    {
        if (expect_eol (p, s, n, j))
        {
            if (f -> ncond == 0)
            {
                pp_error (p, (int) dstart + 1, "%%else with no previous %%if");
            }
            else if (f -> cond[f -> ncond - 1].else_seen)
            {
                pp_error (p, (int) dstart + 1, "%%else can only be used once per %%if");
            }
            else
            {
                cond_t  *c = &f -> cond[f -> ncond - 1];

                c -> else_seen = 1;
                c -> state     = c -> parent_active && !c -> taken;
                c -> taken     = 1;
            }
        }
    }
    else if (dir_is (p, name, "endif"))
    {
        if (expect_eol (p, s, n, j))
        {
            if (f -> ncond == 0)
            {
                pp_error (p, (int) dstart + 1, "%%endif with no previous %%if");
            }
            else
            {
                f -> ncond--;
            }
        }
    }
    else if (!p -> standard && (dir_is (p, name, "assign") || dir_is (p, name, "eval")))
    {
        char  *macro = expect_ident (p, s, n, &j, "name is missing");

        if (macro != NULL)
        {
            ppval_t  v;

            if (eval_rest (p, s, n, j, &v) == 0)
            {
                char  text[200];

                ppexpr_format (&v, text, sizeof (text));
                macro_set (p, macro, text);
            }

            free (macro);
        }
    }
    else if (!p -> standard && dir_is (p, name, "includeonce"))
    {
        if (expect_eol (p, s, n, j) && f -> canon != NULL && !once_contains (p, f -> canon))
        {
            p -> once = xrealloc (p -> once, (size_t) (p -> nonce + 1) * sizeof (char *));
            p -> once[p -> nonce++] = xstrndup (f -> canon, strlen (f -> canon));
        }
    }
    else if (dir_is (p, name, "error") || dir_is (p, name, "warning"))
    {
        char  *msg = expect_string (p, s, n, &j, "expected a string");

        if (msg != NULL)
        {
            if (expect_eol (p, s, n, j))
            {
                if (dir_is (p, name, "error"))
                {
                    pp_error (p, (int) dstart + 1, "%s", msg);
                }
                else
                {
                    pp_warning_at (p, f -> line, (int) dstart + 1, "%s", msg);
                }
            }

            free (msg);
        }
    }
    else
    {
        pp_error (p, (int) dstart + 1, "unsupported preprocessor directive \"%s\"", name);
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Driving the lines
//
static void  process_line (pp_t *p, const char *s, size_t n)
{
    frame_t  *f = frame_top (p);
    size_t    i = skip_ws (s, n, 0);

    // "%eval(...)" at the start of a line is source text that happens to begin with an
    // expression, not a directive
    if (i < n && s[i] == '%' && !(!p -> standard && inline_eval_paren (s, n, i)))
    {
        directive (p, f, s, n, i);
        return;
    }

    if (frame_active (f))
    {
        emit_line (p, f, s, n);
    }
}

static unsigned  process_text (pp_t *p, const char *name, const char *text, size_t len)
{
    frame_t   *f      = frame_push (p, name);
    unsigned   before = p -> errors;
    size_t     pos    = 0;
    int        k;

    f -> canon = realpath (name, NULL);

    // skip a UTF-8 byte order mark (so a directive on the first line is still seen)
    if (len >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB
                 && (unsigned char) text[2] == 0xBF)
    {
        pos = 3;
    }

    while (pos < len)
    {
        size_t  end = pos;
        size_t  n;

        while (end < len && text[end] != '\n')
        {
            end++;
        }

        n = end - pos;

        if (n > 0 && text[pos + n - 1] == '\r')
        {
            n--;
        }

        f -> line++;
        process_line (p, text + pos, n);

        pos = (end < len) ? end + 1 : end;
    }

    // frame_top() is still f here: includes pop their own frame before returning.
    // (The stock assembler lets an unterminated block pass without a word.)
    for (k = f -> ncond - 1; k >= 0; k--)
    {
        if (p -> standard)
        {
            pp_warning_at (p, f -> cond[k].line, 0,
                           "unterminated %%if block (no matching %%endif before end of file)");
        }
        else
        {
            pp_error_at (p, f -> cond[k].line, 0,
                         "unterminated %%if block (no matching %%endif before end of file)");
        }
    }

    frame_pop (p);

    return (p -> errors - before);
}

unsigned  pp_process (pp_t *p, const char *name, const char *text, size_t len)
{
    return (process_text (p, name, text, len));
}

void  pp_stream_begin (pp_t *p, const char *name)
{
    frame_push (p, name);
}

unsigned  pp_stream_line (pp_t *p, const char *line, size_t len, int lineno)
{
    unsigned  before = p -> errors;

    frame_top (p) -> line = lineno;
    process_line (p, line, len);

    return (p -> errors - before);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// -E output
//
void  pp_write_expanded (const pp_t *p, FILE *fp, int markers)
{
    const char  *s    = p -> out.s;
    const char  *file = NULL;
    int          next = 0;
    size_t       k;

    for (k = 0; k < p -> nmap && s != NULL; k++)
    {
        const char  *nl = strchr (s, '\n');
        size_t       n  = nl ? (size_t) (nl - s) : strlen (s);

        if (markers && (p -> map[k].file != file || p -> map[k].line != next))
        {
            fprintf (fp, "# %d \"%s\"\n", p -> map[k].line, p -> map[k].file);
        }

        file = p -> map[k].file;
        next = p -> map[k].line + 1;

        fwrite (s, 1, n, fp);
        fputc  ('\n', fp);

        s = nl ? nl + 1 : NULL;
    }
}
