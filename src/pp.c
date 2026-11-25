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
//  - %macro and %rep bodies are captured as raw lines and replayed through the same line
//    processor, so a body can hold directives, conditionals, other macros and %rep blocks.
//    A macro is invoked as a statement (optionally after labels): its arguments are split at
//    the top-level commas and substituted into each body line before that line is processed.
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
#include "portable.h"
#include "ppexpr.h"

#define  MAX_INCLUDE_DEPTH   20             // same limit as the stock assembler
#define  MAX_EXPAND_DEPTH    64             // nested argument / %eval expansion
#define  MAX_LINE_BYTES      (1u << 22)     // one expanded line (4 MiB)
#define  MAX_SPLICES         200000         // replacements in one line
#define  HASH_SIZE           256
#define  MAX_CTX_DEPTH       64             // nested %macro / %rep executions
#define  MAX_REP_COUNT       1000000        // iterations of one %rep
#define  MAX_OUT_LINES       (1u << 23)     // lines of output (guards runaway expansion)
#define  MAX_WORK_LINES      (1u << 22)     // body lines replayed in one run, output or not

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

    heapstat_sample ();             // --stats on the console (inc/heapstat.h); nothing elsewhere
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

        // doubling a big buffer needs the old and the new one at once and leaves the old one as a hole (v32nix: a character
        // is a word and a process has at most ~500 K of them), so past 32 K characters it grows by half
        while (b -> len + extra + 1 > cap)
        {
            cap += cap < 32768 ? cap : cap / 2;
        }

        b -> s   = xrealloc (b -> s, cap);
        b -> cap = cap;
    }
}

// room for exactly CHARS characters (and the NUL): one allocation of the final size, no doubling
static void  buf_reserve_exact (buf_t *b, size_t chars)
{
    if (chars + 1 > b -> cap)
    {
        b -> s   = xrealloc (b -> s, chars + 1);
        b -> cap = chars + 1;

        if (b -> len == 0)
        {
            b -> s[0] = 0;
        }
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
    int            icase;               // %idefine: found whatever the case (name stored lower-cased)
    int            cmdline;             // defined by -D
    int            force;               // defined by -D!NAME: the command line wins over the file
    struct macro  *next;
}
macro_t;

// A multi-line macro (%macro ... %endmacro). Several may share a name if their argument
// counts differ.
typedef struct smacro
{
    char           *name;               // as written for %macro, lower-cased for %imacro
    int             icase;
    int             min;                // fewest arguments accepted
    int             max;                // most accepted; -1: any number
    int             nparams;            // named parameters (incl. the variadic tail)
    char          **pnames;             // NULL entries for anonymous (%1 only) parameters
    char          **defaults;           // NULL when there is no default
    int             variadic;           // the last named parameter takes the remaining arguments
    char          **body;               // raw lines
    int            *bline;              // their source lines (for -E style notes only)
    int             nbody;
    const char     *dfile;              // where it was defined
    int             dline;
    struct smacro  *next;
}
smacro_t;

// What is being captured between %macro/%rep and its terminator
typedef struct
{
    int          kind;                  // 0: nothing, CTX_MACRO or CTX_REP
    int          depth;                 // nested openers of the same kind seen so far
    smacro_t    *m;                     // the macro being defined (NULL: parse error, discard)
    long         count;                 // %rep count
    char       **line;
    int         *lineno;
    int          n;
    int          cap;
    int          nframes;               // the file the capture started in
    int          nctx;                  // the macro/rep execution it started in
    int          start;                 // line of the opener
    const char  *file;
}
capture_t;

enum { CTX_NONE = 0, CTX_MACRO = 1, CTX_REP = 2 };

// One running %macro expansion or %rep
typedef struct
{
    int         kind;
    smacro_t   *m;
    char      **args;                   // the actual arguments, after defaults
    int         nargs;
    int         rot;                    // %rotate
    unsigned    serial;                 // for %%labels
    int         iter;                   // %rep: iteration number (1-based)
    int         start_ncond;
    void       *frame;
    int         line;                   // where the invocation / %rep was
}
ctx_t;

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

// one %texture / %sound file: as written, and where it was found (or NULL)
typedef struct
{
    char  *hint;
    char  *found;
}
romres_t;

struct pp
{
    macro_t   *tab[HASH_SIZE];

    frame_t  **frames;
    int        nframes;
    int        capframes;

    buf_t      out;
    pp_sink_t  sink;                // output lines go here, not into out and map
    void      *sink_ud;
    size_t     nsunk;               // lines handed to the sink
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

    // file-local labels seen: the label the assembler gets, how a person writes it (made when first asked for) and the length
    // of the global label it was written under.  Only noted when something reads them afterwards (keep_locals: -g, --symbols).
    struct local { char *mangled; char *display; size_t glen; }  *locals;
    int        keep_locals;
    size_t     nlocals;
    size_t     caplocals;
    int        locals_sorted;
    size_t    *lhash;               // open addressing over locals[] by mangled name, + 1 (0 = empty): each label is kept once
    size_t     lhcap;

    // files read by %include (for -MD)
    char     **deps;
    size_t     ndeps;
    size_t     capdeps;

    int        warn_redefine;       // warn when a macro gets a different value

    // the ROM definition: %title, %version, %texture, %sound
    char      *rom_title;
    char      *rom_version;
    romres_t  *rom_res[2];          // [0] textures, [1] sounds
    size_t     nrom_res[2];
    size_t     caprom_res[2];

    // multi-line macros, %rep
    smacro_t  *smtab[HASH_SIZE];
    smacro_t  *dead_macros;         // %unmacro'd while still running: freed at the end of the run
    int        nicase;              // number of case-insensitive macros (of either kind)
    capture_t  cap;
    ctx_t     *ctx;
    int        nctx;
    int        capctx;
    int        unwind;              // pending %exitmacro / %exitrep (a CTX_ kind)
    unsigned   serial;              // %%label serial numbers
    int        halt;                // output cap reached: stop quietly
    unsigned   work;                // body lines replayed so far
    int        shadow_all;          // %pragma shadow
    char     **shadow;
    int        nshadow;

    // %push / %pop context stack
    char     **cname;
    unsigned  *cid;
    int        ncstack;
    int        capcstack;
    unsigned   cserial;
};

static void  smac_free (smacro_t *m);
static void  cap_reset (pp_t *p);
static void  free_args (char **v, int n);

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

    if (m == NULL && p -> nicase > 0)
    {
        char  *q;

        for (q = key; *q; q++)
        {
            *q = (char) tolower ((unsigned char) *q);
        }

        for (m = p -> tab[hash_name (key)]; m != NULL; m = m -> next)
        {
            if (m -> icase && strcmp (m -> name, key) == 0)
            {
                break;
            }
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
    macro_t  *m = macro_find (p, name, strlen (name));
    macro_t  **link;

    if (m == NULL)
    {
        return;
    }

    link = &p -> tab[hash_name (m -> name)];

    while (*link != m)
    {
        link = &(*link) -> next;
    }

    *link = m -> next;

    if (m -> icase)
    {
        p -> nicase--;
    }

    macro_free (m);
}

// Defines (or redefines) an object-like macro; for a function-like one, fill in the
// returned macro's parameters afterwards
static macro_t  *macro_set_i (pp_t *p, const char *name, const char *body, int icase)
{
    macro_t  *m;
    char     *key = xstrndup (name, strlen (name));
    char     *q;

    if (icase)
    {
        for (q = key; *q; q++)
        {
            *q = (char) tolower ((unsigned char) *q);
        }
    }

    macro_remove (p, key);

    m          = xrealloc (NULL, sizeof (*m));
    memset (m, 0, sizeof (*m));
    m -> name  = key;
    m -> body  = xstrndup (body, strlen (body));
    m -> icase = icase;
    m -> next  = p -> tab[hash_name (key)];
    p -> tab[hash_name (key)] = m;

    if (icase)
    {
        p -> nicase++;
    }

    return (m);
}

static macro_t  *macro_set (pp_t *p, const char *name, const char *body)
{
    return (macro_set_i (p, name, body, 0));
}

void  pp_undefine (pp_t *p, const char *name)
{
    macro_remove (p, name);
}

int  pp_define_cmdline (pp_t *p, const char *spec)
{
    int          force = (spec[0] == '!');
    const char  *eq;
    macro_t     *m;

    spec += force;
    eq    = strchr (spec, '=');
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
    m    = macro_set (p, name, eq ? eq + 1 : "1");
    m -> cmdline = 1;
    m -> force   = force;
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
    p -> warn_redefine = 1;

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

    for (i = 0; i < HASH_SIZE; i++)
    {
        while (p -> smtab[i] != NULL)
        {
            smacro_t  *m = p -> smtab[i];

            p -> smtab[i] = m -> next;
            smac_free (m);
        }
    }

    while (p -> dead_macros != NULL)
    {
        smacro_t  *m = p -> dead_macros;

        p -> dead_macros = m -> next;
        smac_free (m);
    }

    for (i = 0; i < p -> nctx; i++)
    {
        free_args (p -> ctx[i].args, p -> ctx[i].nargs);
    }

    for (i = 0; i < p -> nshadow; i++)
    {
        free (p -> shadow[i]);
    }

    cap_reset (p);

    for (i = 0; i < p -> ncstack; i++)
    {
        free (p -> cname[i]);
    }

    free (p -> cname);
    free (p -> cid);
    free (p -> ctx);
    free (p -> shadow);

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

    for (i = 0; i < (int) p -> nlocals; i++)
    {
        free (p -> locals[i].mangled);
        free (p -> locals[i].display);
    }

    for (i = 0; i < (int) p -> ndeps; i++)
    {
        free (p -> deps[i]);
    }

    for (i = 0; i < 2; i++)
    {
        size_t  r;

        for (r = 0; r < p -> nrom_res[i]; r++)
        {
            free (p -> rom_res[i][r].hint);
            free (p -> rom_res[i][r].found);
        }

        free (p -> rom_res[i]);
    }

    free (p -> rom_title);
    free (p -> rom_version);
    free (p -> locals);
    free (p -> lhash);
    free (p -> deps);
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

void  pp_set_redefine_warning (pp_t *p, int on)
{
    p -> warn_redefine = on;
}

const char  *const  *pp_deps (const pp_t *p, size_t *n)
{
    *n = p -> ndeps;

    return ((const char *const *) p -> deps);
}

const char  *pp_rom_title (const pp_t *p)
{
    return (p -> rom_title);
}

const char  *pp_rom_version (const pp_t *p)
{
    return (p -> rom_version);
}

size_t  pp_rom_resources (const pp_t *p, int sound)
{
    return (p -> nrom_res[sound != 0]);
}

const char  *pp_rom_resource (const pp_t *p, int sound, size_t i, const char **found)
{
    sound = (sound != 0);

    if (i >= p -> nrom_res[sound])
    {
        return (NULL);
    }

    if (found != NULL)
    {
        *found = p -> rom_res[sound][i].found;
    }

    return (p -> rom_res[sound][i].hint);
}

static void  dep_add (pp_t *p, const char *path)
{
    if (p -> ndeps == p -> capdeps)
    {
        p -> capdeps = p -> capdeps ? p -> capdeps * 2 : 16;
        p -> deps    = xrealloc (p -> deps, p -> capdeps * sizeof (char *));
    }

    p -> deps[p -> ndeps++] = xstrndup (path, strlen (path));
}

const char  *pp_define_text (const pp_t *p, const char *name)
{
    const macro_t  *m = macro_find (p, name, strlen (name));

    return (m != NULL && !m -> is_func ? m -> body : NULL);
}

void  pp_each_define (pp_t *p, void (*cb) (void *ud, const char *name, const char *body), void *ud)
{
    int       i;
    macro_t  *m;

    for (i = 0; i < HASH_SIZE; i++)
    {
        for (m = p -> tab[i]; m != NULL; m = m -> next)
        {
            if (!m -> is_func)
            {
                cb (ud, m -> name, m -> body);
            }
        }
    }
}

static int  local_cmp (const void *x, const void *y)
{
    const struct local  *p = x, *q = y;

    return (strcmp (p -> mangled, q -> mangled));
}

// the entry for a mangled name, or NULL
static struct local  *local_find (pp_t *p, const char *mangled)
{
    size_t  lo = 0, hi = p -> nlocals;

    if (!p -> locals_sorted)
    {
        if (p -> nlocals > 1)
        {
            qsort (p -> locals, p -> nlocals, sizeof (p -> locals[0]), local_cmp);
        }
        p -> locals_sorted = 1;
    }

    while (lo < hi)
    {
        size_t  mid = (lo + hi) / 2;
        int     c   = strcmp (p -> locals[mid].mangled, mangled);

        if (c == 0)
        {
            return (&p -> locals[mid]);
        }

        if (c < 0)  { lo = mid + 1; }
        else        { hi = mid; }
    }

    return (NULL);
}

// Whether the preprocessor noted file-local labels at all: only when the output wants them afterwards (-g, --symbols), the
// names are not kept otherwise (the assembler has the mangled ones as its symbols)
void  pp_set_keep_locals (pp_t *p, int on)
{
    p -> keep_locals = on;
}

// "_draw__again" -> "_draw.again" when that is what a ".again" under "_draw:" became (the spelling is made on the first ask)
const char  *pp_local_name (pp_t *p, const char *mangled)
{
    struct local  *l = local_find (p, mangled);

    if (l != NULL && l -> display == NULL)
    {
        size_t  n = strlen (l -> mangled);

        l -> display = xrealloc (NULL, n);                      // one character shorter than the mangled name ("__" -> ".")
        memcpy (l -> display, l -> mangled, l -> glen);
        l -> display[l -> glen] = '.';
        memcpy (l -> display + l -> glen + 1, l -> mangled + l -> glen + 2, n - l -> glen - 2 + 1);
    }

    return (l != NULL ? l -> display : NULL);
}

// the same question without making the spelling: the mangled name itself if it is a file-local label, else NULL
const char  *pp_is_local (pp_t *p, const char *mangled)
{
    struct local  *l = local_find (p, mangled);

    return (l != NULL ? l -> mangled : NULL);
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

void  pp_set_sink (pp_t *p, pp_sink_t sink, void *ud)
{
    p -> sink    = sink;
    p -> sink_ud = ud;
    p -> nsunk   = 0;
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

// An estimate of the output length (the input's length plus half again: macros and %define make it longer), so that the
// output buffer is allocated once at about its final size
void  pp_reserve_output (pp_t *p, size_t chars)
{
    buf_reserve_exact (&p -> out, chars);
}

// The output text, handed over: the caller frees it.  It is trimmed to its length plus 4 (the scanner's final newline and NULs),
// so the doubling slack does not stay allocated while the text is assembled (on v32nix a character is a word).
char  *pp_output_take (pp_t *p, size_t *len)
{
    char  *s = p -> out.s;

    *len = p -> out.len;
    if (s == NULL)
    {
        s = xrealloc (NULL, 4);
        s[0] = 0;
    }
    else
    {
        s = xrealloc (s, p -> out.len + 4);
    }
    p -> out.s   = NULL;
    p -> out.len = p -> out.cap = 0;
    return (s);
}

// Room for the line map of about this many lines, so that it is allocated once and not doubled
void  pp_reserve_lines (pp_t *p, size_t lines)
{
    if (lines > p -> capmap)
    {
        p -> capmap = lines;
        p -> map    = xrealloc (p -> map, p -> capmap * sizeof (srcloc_t));
    }
}

// The line map cut to the lines it has (after the preprocessing of a whole file: the estimate or the doubling left slack)
void  pp_trim_linemap (pp_t *p)
{
    if (p -> nmap < p -> capmap && p -> map != NULL)
    {
        p -> capmap = p -> nmap > 0 ? p -> nmap : 1;
        p -> map    = xrealloc (p -> map, p -> capmap * sizeof (srcloc_t));
    }
}

// pp_output_clear, and the line map is given back as well: for when the assembler is done with it (not for the REPL, which
// preprocesses a line at a time and keeps its map for the next one)
void  pp_output_release (pp_t *p)
{
    pp_output_clear (p);
    free (p -> map);
    p -> map    = NULL;
    p -> capmap = 0;
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

    if (col > 0 && p -> nctx == 0)
    {
        fprintf (stderr, "%s:%d:%d: %s: ", f -> name, line, col, kind);
    }
    else
    {
        fprintf (stderr, "%s:%d: %s: ", f -> name, line, kind);
    }

    vfprintf (stderr, fmt, ap);
    fputc    ('\n', stderr);

    asm_cmap_note (f -> name, line);            // --map: the C line this assembly line came from

    for (k = p -> nctx - 1; k >= 0; k--)
    {
        const ctx_t  *c = &p -> ctx[k];

        if (k < p -> nctx - 6 && k > 5)
        {
            if (k == p -> nctx - 7)
            {
                fprintf (stderr, "%s:%d: note: ... %d more expansion levels ...\n", f -> name, c -> line,
                         p -> nctx - 12);
            }

            continue;
        }

        if (c -> kind == CTX_MACRO)
        {
            fprintf (stderr, "%s:%d: note: in expansion of macro '%s' (defined at %s:%d)\n", f -> name,
                     c -> line, c -> m -> name, c -> m -> dfile, c -> m -> dline);
        }
        else
        {
            fprintf (stderr, "%s:%d: note: in %%rep, iteration %d\n", f -> name, c -> line, c -> iter);
        }
    }

    for (k = p -> nframes - 2; k >= 0; k--)
    {
        fprintf (stderr, "%s:%d: note: included from here\n", p -> frames[k] -> name,
                 p -> frames[k] -> line);
    }
}

static void  pp_error_at (pp_t *p, int line, int col, const char *fmt, ...)
    __attribute__ ((format (V32_PRINTF, 4, 5)));

static void  pp_error_at (pp_t *p, int line, int col, const char *fmt, ...)
{
    va_list  ap;

    va_start (ap, fmt);
    vmsg     (p, "error", line, col, fmt, ap);
    va_end   (ap);

    p -> errors++;
}

static void  pp_error (pp_t *p, int col, const char *fmt, ...)
    __attribute__ ((format (V32_PRINTF, 3, 4)));

static void  pp_error (pp_t *p, int col, const char *fmt, ...)
{
    va_list  ap;

    va_start (ap, fmt);
    vmsg     (p, "error", frame_top (p) -> line, col, fmt, ap);
    va_end   (ap);

    p -> errors++;
}

static void  pp_warning_at (pp_t *p, int line, int col, const char *fmt, ...)
    __attribute__ ((format (V32_PRINTF, 4, 5)));

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
    int  keep = 0;

    // scanning only moves forward, so a region that ends at or before START can never hide anything again: drop it
    // (otherwise the list grows with every replacement and a big expansion costs quadratic time)
    for (i = 0; i < h -> n; i++)
    {
        if (h -> v[i].end != SIZE_MAX && h -> v[i].end <= start)
        {
            continue;
        }

        h -> v[keep++] = h -> v[i];
    }

    h -> n = keep;

    for (i = 0; i < h -> n; i++)
    {
        hide_t  *x = &h -> v[i];

        if (x -> end == SIZE_MAX)
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
static unsigned  hash_str (const char *s, size_t n)
{
    unsigned  h = 5381;

    while (n--)
    {
        h = h * 33 + (unsigned char) *s++;
    }

    return (h);
}

// is mangled (n characters) noted already?  Otherwise, where in lhash it goes (the table has room)
static size_t  local_slot (pp_t *p, const char *mangled, size_t n, int *found)
{
    size_t  i = hash_str (mangled, n) % p -> lhcap;

    *found = 0;
    while (p -> lhash[i] != 0)
    {
        const char  *m = p -> locals[p -> lhash[i] - 1].mangled;

        if (strncmp (m, mangled, n) == 0 && m[n] == 0)
        {
            *found = 1;
            break;
        }
        i = (i + 1) % p -> lhcap;
    }
    return (i);
}

static void  local_note (pp_t *p, const char *name, size_t n)
{
    size_t  g = strlen (p -> last_global);
    char   *mangled;
    size_t  slot;
    int     found;

    if (!p -> keep_locals)
    {
        return;
    }

    // the same label is written many times (its definition, every jump to it): keep one copy of each (v32cc's assembly has
    // ~2.4 references per local label; on v32nix every copy was ~70 words of a process that has ~300 K)
    if (p -> nlocals + 1 > p -> lhcap / 2 || p -> locals_sorted)
    {
        // (re)build the table: it is full, or pp_local_name sorted locals[] and the indexes moved
        size_t  k, cap = p -> lhcap ? (p -> nlocals + 1 > p -> lhcap / 2 ? p -> lhcap * 2 : p -> lhcap) : 256;

        p -> locals_sorted = 0;
        free (p -> lhash);
        p -> lhash = xrealloc (NULL, cap * sizeof (size_t));
        memset (p -> lhash, 0, cap * sizeof (size_t));
        p -> lhcap = cap;
        for (k = 0; k < p -> nlocals; k++)
        {
            p -> lhash[local_slot (p, p -> locals[k].mangled, strlen (p -> locals[k].mangled), &found)] = k + 1;
        }
    }

    mangled = xrealloc (NULL, g + 2 + n + 1);
    memcpy (mangled, p -> last_global, g);
    memcpy (mangled + g, "__", 2);
    memcpy (mangled + g + 2, name, n);
    mangled[g + 2 + n] = 0;

    slot = local_slot (p, mangled, g + 2 + n, &found);
    if (found)
    {
        free (mangled);
        return;
    }

    if (p -> nlocals == p -> caplocals)
    {
        p -> caplocals = p -> caplocals ? p -> caplocals * 2 : 64;
        p -> locals    = xrealloc (p -> locals, p -> caplocals * sizeof (p -> locals[0]));
    }

    p -> locals[p -> nlocals].mangled = mangled;
    p -> locals[p -> nlocals].display = NULL;
    p -> locals[p -> nlocals].glen    = g;
    p -> nlocals++;
    p -> lhash[slot] = p -> nlocals;
}

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
            local_note (p, s + i + 1, j - i - 1);
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

    if (p -> halt)
    {
        return;
    }

    if ((p -> sink != NULL ? p -> nsunk : p -> nmap) >= MAX_OUT_LINES)
    {
        pp_error (p, 0, "the output is too large (more than %u lines): runaway %%rep or macro?", MAX_OUT_LINES);
        p -> halt = 1;
        return;
    }

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

    if (p -> sink != NULL)
    {
        srcloc_t  where;

        where.file     = f -> name;
        where.line     = f -> line;
        where.expanded = changed || p -> nctx > 0;
        p -> nsunk++;
        p -> sink (p -> sink_ud, b.s != NULL ? b.s : "", b.len, &where);
    }
    else
    {
        buf_put (&p -> out, b.s, b.len);
        buf_put (&p -> out, "\n", 1);
        map_push (p, f -> name, f -> line, changed || p -> nctx > 0);
    }

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

    if (ferror (fp))                    // a directory opens fine with fopen() and then fails to read (EISDIR)
    {
        fclose (fp);
        return (-1);
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
        char  *canon = v32_realpath (found, NULL);

        if (canon != NULL && once_contains (p, canon))
        {
            free (canon);
        }
        else
        {
            free (canon);
            dep_add (p, found);
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

static void  do_define (pp_t *p, const char *s, size_t n, size_t j, int icase, int expand)
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

    // %xdefine: the value is expanded now rather than where it is used
    if (expand && !proto.is_func)
    {
        buf_t        x;
        hides_t      h       = { 0 };
        int          changed = 0;
        const char  *why     = NULL;

        buf_init (&x, body, strlen (body));

        if (expand_buffer (p, &x, &h, 0, &changed, &why) != 0)
        {
            pp_error (p, (int) b + 1, "%s", why);
            free (x.s);
            free (h.v);
            free (body);
            free (name);
            return;
        }

        free (h.v);
        free (body);
        body = x.s;
    }

    // a definition meeting an earlier one: the command line may have asked to win, or a
    // difference may be unintended
    {
        macro_t  *old = macro_find (p, name, strlen (name));

        if (old != NULL && old -> force)
        {
            int  q;

            for (q = 0; q < proto.nparams + (proto.variadic ? 1 : 0); q++)
            {
                free (proto.params[q]);
            }

            free (proto.params);
            free (body);
            free (name);
            return;
        }

        if (old != NULL && !p -> standard)
        {
            if (old -> cmdline)
            {
                pp_warning_at (p, frame_top (p) -> line, 0,
                               "'%s' redefines the -D definition (-D!%s=VALUE makes the command line win)",
                               name, name);
            }
            else if (p -> warn_redefine
                     && (old -> is_func != proto.is_func || strcmp (old -> body, body) != 0))
            {
                pp_warning_at (p, frame_top (p) -> line, 0,
                               "macro '%s' redefined with a different value (was '%s') [-Wredefine]",
                               name, old -> body);
            }
        }
    }

    m = macro_set_i (p, name, body, icase);

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

////////////////////////////////////////////////////////////////////////////////////////
//
// Multi-line macros (%macro ... %endmacro) and %rep ... %endrep
//
//   %macro CLAMP reg, lo=0, hi=255          named parameters, defaults
//       imax  reg, lo                        (%1 %2 %3, %0 and %* work too)
//       imin  reg, hi
//   %endmacro
//
//   CLAMP R3, 10                             invoked like an instruction
//
static void  process_line (pp_t *p, const char *s, size_t n);

static unsigned  hash_lower (const char *s, size_t n)
{
    unsigned  h = 5381;
    size_t    i;

    for (i = 0; i < n; i++)
    {
        h = h * 33 + (unsigned char) tolower ((unsigned char) s[i]);
    }

    return (h % HASH_SIZE);
}

static int  smac_name_is (const smacro_t *m, const char *name, size_t n)
{
    if (strlen (m -> name) != n)
    {
        return (0);
    }

    return (m -> icase ? strncasecmp (m -> name, name, n) == 0 : memcmp (m -> name, name, n) == 0);
}

static void  smac_free (smacro_t *m)
{
    int  i;

    if (m == NULL)
    {
        return;
    }

    for (i = 0; i < m -> nparams; i++)
    {
        free (m -> pnames[i]);
        free (m -> defaults[i]);
    }

    for (i = 0; i < m -> nbody; i++)
    {
        free (m -> body[i]);
    }

    free (m -> pnames);
    free (m -> defaults);
    free (m -> body);
    free (m -> bline);
    free (m -> name);
    free (m);
}

static int  smac_accepts (const smacro_t *m, int nargs)
{
    return (nargs >= m -> min && (m -> max < 0 || nargs <= m -> max));
}

// the macro of that name taking nargs arguments (nargs < 0: any); *any tells whether the
// name is known at all
static smacro_t  *smac_pick (const pp_t *p, const char *name, size_t n, int nargs, int *any)
{
    smacro_t  *m;

    if (any != NULL)
    {
        *any = 0;
    }

    for (m = p -> smtab[hash_lower (name, n)]; m != NULL; m = m -> next)
    {
        if (smac_name_is (m, name, n))
        {
            if (any != NULL)
            {
                *any = 1;
            }

            if (nargs < 0 || smac_accepts (m, nargs))
            {
                return (m);
            }
        }
    }

    return (NULL);
}

static int  smac_running (const pp_t *p, const smacro_t *m)
{
    int  i;

    for (i = 0; i < p -> nctx; i++)
    {
        if (p -> ctx[i].kind == CTX_MACRO && p -> ctx[i].m == m)
        {
            return (1);
        }
    }

    return (0);
}

static void  smac_unlink_all (pp_t *p, const char *name, size_t n, int *count)
{
    smacro_t  **link = &p -> smtab[hash_lower (name, n)];

    while (*link != NULL)
    {
        if (smac_name_is (*link, name, n))
        {
            smacro_t  *dead = *link;

            *link = dead -> next;

            if (dead -> icase)
            {
                p -> nicase--;
            }

            if (smac_running (p, dead))
            {
                dead -> next = p -> dead_macros;    // its body is being replayed: keep it alive
                p -> dead_macros = dead;
            }
            else
            {
                smac_free (dead);
            }

            (*count)++;
        }
        else
        {
            link = &(*link) -> next;
        }
    }
}

static int  shadow_allowed (const pp_t *p, const char *name)
{
    int  i;

    if (p -> shadow_all)
    {
        return (1);
    }

    for (i = 0; i < p -> nshadow; i++)
    {
        if (strcasecmp (p -> shadow[i], name) == 0)
        {
            return (1);
        }
    }

    return (0);
}

// Registers a finished macro. Two definitions of a name may only differ in argument count.
static int  smac_add (pp_t *p, smacro_t *m, int line)
{
    smacro_t  *o;
    size_t     nlen = strlen (m -> name);

    for (o = p -> smtab[hash_lower (m -> name, nlen)]; o != NULL; o = o -> next)
    {
        if (smac_name_is (o, m -> name, nlen) || smac_name_is (m, o -> name, strlen (o -> name)))
        {
            int  lo = m -> min > o -> min ? m -> min : o -> min;
            int  overlap;

            if (m -> max < 0 && o -> max < 0)
            {
                overlap = 1;
            }
            else if (m -> max < 0)
            {
                overlap = (o -> max >= lo);
            }
            else if (o -> max < 0)
            {
                overlap = (m -> max >= lo);
            }
            else
            {
                overlap = (m -> max < o -> max ? m -> max : o -> max) >= lo;
            }

            if (overlap)
            {
                pp_error_at (p, line, 0, "macro '%s' is already defined for this number of arguments "
                                         "(at %s:%d); use %%unmacro first",
                             m -> name, o -> dfile, o -> dline);
                return (-1);
            }
        }
    }

    if (kw_lookup (m -> name, nlen) != NULL && !shadow_allowed (p, m -> name))
    {
        pp_warning_at (p, line, 0, "macro '%s' hides the built-in name of the same name "
                                   "(%%pragma shadow silences this)", m -> name);
    }

    m -> next = p -> smtab[hash_lower (m -> name, nlen)];
    p -> smtab[hash_lower (m -> name, nlen)] = m;

    if (m -> icase)
    {
        p -> nicase++;
    }

    return (0);
}

// Index of the next comma outside any bracket, string or character literal (or n)
static size_t  scan_arg_end (const char *s, size_t n, size_t i)
{
    int  depth = 0;

    while (i < n)
    {
        char  c = s[i];

        if (c == '"')
        {
            i = skip_string (s, n, i);
        }
        else if (c == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            i += (k ? k : 1);
        }
        else if (c == '(' || c == '[' || c == '{')
        {
            depth++;
            i++;
        }
        else if (c == ')' || c == ']' || c == '}')
        {
            if (depth > 0)
            {
                depth--;
            }

            i++;
        }
        else if (c == ',' && depth == 0)
        {
            return (i);
        }
        else
        {
            i++;
        }
    }

    return (n);
}

// Index of the '}' that closes the '{' at s[i] (or n)
static size_t  brace_close (const char *s, size_t n, size_t i)
{
    int  depth = 0;

    while (i < n)
    {
        if (s[i] == '"')
        {
            i = skip_string (s, n, i);
            continue;
        }

        if (s[i] == '{')
        {
            depth++;
        }
        else if (s[i] == '}' && --depth == 0)
        {
            return (i);
        }

        i++;
    }

    return (n);
}

static void  trim_range (const char *s, size_t *b, size_t *e)
{
    while (*b < *e && is_ws ((unsigned char) s[*b]))
    {
        (*b)++;
    }

    while (*e > *b && is_ws ((unsigned char) s[*e - 1]))
    {
        (*e)--;
    }
}

// Splits "a, b, {c, d}" into trimmed argument texts. A lone blank text is no arguments.
static void  split_args (const char *s, size_t n, char ***out, int *count)
{
    char   **v   = NULL;
    int      nv  = 0;
    size_t   i   = 0;
    size_t   b0  = 0;
    size_t   e0  = n;

    trim_range (s, &b0, &e0);

    if (b0 == e0)
    {
        *out   = NULL;
        *count = 0;
        return;
    }

    i = b0;
    n = e0;

    for (;;)
    {
        size_t  e = scan_arg_end (s, n, i);
        size_t  b = i;
        size_t  t = e;

        trim_range (s, &b, &t);

        // {braced} groups an argument that contains commas: the braces are dropped
        if (t - b >= 2 && s[b] == '{' && s[t - 1] == '}' && brace_close (s, t, b) == t - 1)
        {
            b++;
            t--;
            trim_range (s, &b, &t);
        }

        v = xrealloc (v, (size_t) (nv + 1) * sizeof (char *));
        v[nv++] = xstrndup (s + b, t - b);

        if (e >= n)
        {
            break;
        }

        i = e + 1;
    }

    *out   = v;
    *count = nv;
}

// Parses what follows "%macro NAME": "reg, lo=0, rest...", or a count ("2", "1-3", "1-*")
static smacro_t  *smac_parse_header (pp_t *p, const char *s, size_t n, size_t j, const char *name, int icase)
{
    smacro_t  *m   = xrealloc (NULL, sizeof (*m));
    size_t     b   = skip_ws (s, n, j);
    size_t     e   = find_comment (s, n, b);
    int        seen_default = 0;
    int        k;

    memset (m, 0, sizeof (*m));

    m -> name  = xstrndup (name, strlen (name));
    m -> icase = icase;

    if (icase)
    {
        char  *q;

        for (q = m -> name; *q; q++)
        {
            *q = (char) tolower ((unsigned char) *q);
        }
    }

    {
        frame_t  *f = frame_top (p);

        m -> dfile = f -> name;
        m -> dline = f -> line;
    }

    while (e > b && is_ws ((unsigned char) s[e - 1]))
    {
        e--;
    }

    if (b == e)
    {
        return (m);                                 // no parameters
    }

    if (isdigit ((unsigned char) s[b]))
    {
        char  *end;
        long   lo = strtol (s + b, &end, 10);
        long   hi = lo;
        size_t q  = (size_t) (end - s);

        if (q < e && s[q] == '-')
        {
            q++;

            if (q < e && s[q] == '*')
            {
                hi = -1;
                q++;
            }
            else
            {
                hi = strtol (s + q, &end, 10);
                q  = (size_t) (end - s);
            }
        }

        q = skip_ws (s, e, q);

        if (q != e || lo < 0 || lo > 255 || (hi >= 0 && hi < lo) || hi > 255)
        {
            pp_error (p, (int) b + 1, "bad argument count for the macro (use N, N-M or N-*)");
            smac_free (m);
            return (NULL);
        }

        m -> min = (int) lo;
        m -> max = (int) hi;
        return (m);
    }

    for (;;)
    {
        size_t  ne;
        size_t  de;
        char   *pname;
        char   *dflt = NULL;
        int     variadic = 0;

        b = skip_ws (s, e, b);

        if (b + 2 < e + 0 && s[b] == '.' && s[b + 1] == '.' && s[b + 2] == '.')
        {
            pname    = xstrndup ("__VA_ARGS__", 11);
            variadic = 1;
            ne       = b + 3;
        }
        else
        {
            if (b >= e || !is_ident_start ((unsigned char) s[b]))
            {
                pp_error (p, (int) b + 1, "expected a parameter name");
                smac_free (m);
                return (NULL);
            }

            ne = b;

            while (ne < e && is_ident_char ((unsigned char) s[ne]))
            {
                ne++;
            }

            pname = xstrndup (s + b, ne - b);

            if (ne + 2 < e + 0 && s[ne] == '.' && s[ne + 1] == '.' && s[ne + 2] == '.')
            {
                variadic = 1;
                ne += 3;
            }
        }

        for (k = 0; k < m -> nparams; k++)
        {
            if (strcmp (m -> pnames[k], pname) == 0)
            {
                pp_error (p, (int) b + 1, "duplicate parameter name '%s'", pname);
                free (pname);
                smac_free (m);
                return (NULL);
            }
        }

        b = skip_ws (s, e, ne);

        if (!variadic && b < e && s[b] == '=')
        {
            size_t  db;
            size_t  dt;

            de = scan_arg_end (s, e, b + 1);
            db = b + 1;
            dt = de;
            trim_range (s, &db, &dt);
            dflt = xstrndup (s + db, dt - db);
            b    = de;
            seen_default = 1;
        }
        else if (!variadic && seen_default)
        {
            pp_error (p, (int) b + 1, "parameter '%s' needs a default (an earlier one has one)", pname);
            free (pname);
            smac_free (m);
            return (NULL);
        }

        m -> pnames   = xrealloc (m -> pnames,   (size_t) (m -> nparams + 1) * sizeof (char *));
        m -> defaults = xrealloc (m -> defaults, (size_t) (m -> nparams + 1) * sizeof (char *));
        m -> pnames[m -> nparams]   = pname;
        m -> defaults[m -> nparams] = dflt;
        m -> nparams++;

        if (variadic)
        {
            m -> variadic = 1;

            if (skip_ws (s, e, b) != e)
            {
                pp_error (p, (int) b + 1, "the variadic parameter must be the last one");
                smac_free (m);
                return (NULL);
            }

            break;
        }

        if (b >= e)
        {
            break;
        }

        if (s[b] != ',')
        {
            pp_error (p, (int) b + 1, "expected ',' in the parameter list");
            smac_free (m);
            return (NULL);
        }

        b++;
    }

    {
        int  fixed = m -> nparams - (m -> variadic ? 1 : 0);

        for (k = 0; k < fixed; k++)
        {
            if (m -> defaults[k] == NULL)
            {
                m -> min = k + 1;
            }
        }

        m -> max = m -> variadic ? -1 : fixed;
    }

    return (m);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Expansion contexts
//
static ctx_t  *ctx_push (pp_t *p, int kind, frame_t *f)
{
    ctx_t  *c;

    if (p -> nctx == p -> capctx)
    {
        p -> capctx = p -> capctx ? p -> capctx * 2 : 16;
        p -> ctx    = xrealloc (p -> ctx, (size_t) p -> capctx * sizeof (ctx_t));
    }

    c = &p -> ctx[p -> nctx++];
    memset (c, 0, sizeof (*c));
    c -> kind        = kind;
    c -> start_ncond = f -> ncond;
    c -> frame       = f;
    c -> line        = f -> line;

    return (c);
}

static int  ctx_find (const pp_t *p, int kind)
{
    int  i;

    for (i = p -> nctx - 1; i >= 0; i--)
    {
        if (p -> ctx[i].kind == kind)
        {
            return (i);
        }
    }

    return (-1);
}

// the lowest %if depth a %else/%elif/%endif in this frame may touch
static int  cond_base (const pp_t *p, const frame_t *f)
{
    if (p -> nctx > 0 && p -> ctx[p -> nctx - 1].frame == (const void *) f)
    {
        return (p -> ctx[p -> nctx - 1].start_ncond);
    }

    return (0);
}

static void  cap_reset (pp_t *p)
{
    int  i;

    for (i = 0; i < p -> cap.n; i++)
    {
        free (p -> cap.line[i]);
    }

    free (p -> cap.line);
    free (p -> cap.lineno);
    smac_free (p -> cap.m);
    memset (&p -> cap, 0, sizeof (p -> cap));
}

// A macro / %rep body that did not end where it began, or conditionals left open
static void  ctx_balance (pp_t *p, frame_t *f)
{
    ctx_t  *c = &p -> ctx[p -> nctx - 1];

    if (p -> cap.kind != CTX_NONE && p -> cap.nctx == p -> nctx && p -> cap.nframes == p -> nframes)
    {
        pp_error_at (p, p -> cap.start, 0, "unterminated %s inside the expansion",
                     p -> cap.kind == CTX_MACRO ? "%macro" : "%rep");
        cap_reset (p);
    }

    if (f -> ncond > c -> start_ncond)
    {
        if (!p -> unwind)
        {
            pp_error (p, 0, "unterminated %%if inside the %s (no matching %%endif)",
                      c -> kind == CTX_MACRO ? "macro" : "%rep block");
        }

        f -> ncond = c -> start_ncond;
    }
}

static void  ctx_leave (pp_t *p, frame_t *f)
{
    ctx_t  *c;

    ctx_balance (p, f);

    c = &p -> ctx[p -> nctx - 1];

    if (p -> unwind == c -> kind)
    {
        p -> unwind = 0;
    }

    free_args (c -> args, c -> nargs);
    p -> nctx--;
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Substituting arguments into a body line
//
static int  line_directive (const char *s, size_t n, char *name, size_t size)
{
    size_t  i = skip_ws (s, n, 0);
    size_t  j;

    if (i >= n || s[i] != '%')
    {
        return (0);
    }

    i = skip_ws (s, n, i + 1);

    if (i >= n || !is_ident_start ((unsigned char) s[i]))
    {
        return (0);
    }

    for (j = i; j < n && is_ident_char ((unsigned char) s[j]); j++)
    {
    }

    if (j - i >= size)
    {
        return (0);
    }

    memcpy (name, s + i, j - i);
    name[j - i] = 0;

    return (1);
}

static void  put_joined (buf_t *o, char **v, int from, int n)
{
    int  i;

    for (i = from; i < n; i++)
    {
        if (i > from)
        {
            buf_put (o, ", ", 2);
        }

        buf_puts (o, v[i]);
    }
}

// Writes the text of "%N" for the macro running as mc
static int  positional (const ctx_t *mc, long idx, int lenient, buf_t *o, char *err, size_t errsize)
{
    if (idx == 0)
    {
        char  num[16];

        snprintf (num, sizeof (num), "%d", mc -> nargs);
        buf_puts (o, num);
        return (0);
    }

    if (idx > mc -> nargs)
    {
        if (lenient || (mc -> m -> max >= 0 && idx <= mc -> m -> max))
        {
            return (0);                             // a skipped line, or an optional argument left out
        }

        snprintf (err, errsize, "macro '%s' was given only %d argument%s, so %%%ld has no value",
                  mc -> m -> name, mc -> nargs, mc -> nargs == 1 ? "" : "s", idx);
        return (-1);
    }

    buf_puts (o, mc -> args[(idx - 1 + mc -> rot) % mc -> nargs]);

    return (0);
}

// Copies s with the macro's parameters, %N, %0, %* and %%labels replaced. mc may be NULL
// (a top-level %rep has only the %%labels).
static int  subst_line (const ctx_t *mc, unsigned serial, int iter, int in_message, int lenient, const char *s, size_t n,
                        buf_t *o, char *err, size_t errsize)
{
    size_t  i = 0;

    while (i < n)
    {
        char  c = s[i];

        if (c == ';')
        {
            buf_put (o, s + i, n - i);
            break;
        }

        if (c == '"')
        {
            size_t  e = skip_string (s, n, i);

            if (in_message)
            {
                size_t  k;

                for (k = i; k < e; k++)
                {
                    if (mc != NULL && s[k] == '%' && k + 1 < e && (isdigit ((unsigned char) s[k + 1]) || s[k + 1] == '*'))
                    {
                        if (s[k + 1] == '*')
                        {
                            put_joined (o, mc -> args, 0, mc -> nargs);
                            k++;
                        }
                        else
                        {
                            char  *end;
                            long   idx = strtol (s + k + 1, &end, 10);

                            if (positional (mc, idx, lenient, o, err, errsize) != 0)
                            {
                                return (-1);
                            }

                            k = (size_t) (end - s) - 1;
                        }
                    }
                    else
                    {
                        buf_put (o, s + k, 1);
                    }
                }
            }
            else
            {
                buf_put (o, s + i, e - i);
            }

            i = e;
            continue;
        }

        if (c == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            if (k == 0)
            {
                k = 1;
            }

            buf_put (o, s + i, k);
            i += k;
            continue;
        }

        if (c == '%' && i + 1 < n)
        {
            if (s[i + 1] == '%' && i + 2 < n && is_ident_char ((unsigned char) s[i + 2]))
            {
                size_t  e = i + 2;
                char    pre[32];

                while (e < n && is_ident_char ((unsigned char) s[e]))
                {
                    e++;
                }

                snprintf (pre, sizeof (pre), "__M%u_", serial);
                buf_puts (o, pre);
                buf_put  (o, s + i + 2, e - i - 2);
                i = e;
                continue;
            }

            if (s[i + 1] == '@')
            {
                if (iter > 0)
                {
                    char  num[16];

                    snprintf (num, sizeof (num), "%d", iter);
                    buf_puts (o, num);
                }
                else if (!lenient)
                {
                    snprintf (err, errsize, "%%@ is only meaningful inside a %%rep block");
                    return (-1);
                }

                i += 2;
                continue;
            }

            if (mc != NULL && s[i + 1] == '*')
            {
                put_joined (o, mc -> args, 0, mc -> nargs);
                i += 2;
                continue;
            }

            if (mc != NULL && isdigit ((unsigned char) s[i + 1]))
            {
                char  *end;
                long   idx = strtol (s + i + 1, &end, 10);

                if (positional (mc, idx, lenient, o, err, errsize) != 0)
                {
                    return (-1);
                }

                i = (size_t) (end - s);
                continue;
            }
        }

        if (isdigit ((unsigned char) c))
        {
            size_t  e = skip_number (s, n, i);

            if (e == i)
            {
                e = i + 1;
            }

            buf_put (o, s + i, e - i);
            i = e;
            continue;
        }

        if (is_ident_start ((unsigned char) c))
        {
            size_t  e = i;
            int     k;
            int     hit = -1;

            while (e < n && is_ident_char ((unsigned char) s[e]))
            {
                e++;
            }

            if (mc != NULL && !(i > 0 && s[i - 1] == '.'))
            {
                for (k = 0; k < mc -> m -> nparams; k++)
                {
                    if (mc -> m -> pnames[k] != NULL && strlen (mc -> m -> pnames[k]) == e - i
                        && memcmp (mc -> m -> pnames[k], s + i, e - i) == 0)
                    {
                        hit = k;
                        break;
                    }
                }
            }

            if (hit < 0)
            {
                buf_put (o, s + i, e - i);
            }
            else if (mc -> m -> variadic && hit == mc -> m -> nparams - 1)
            {
                put_joined (o, mc -> args, hit, mc -> nargs);
            }
            else if (hit < mc -> nargs)
            {
                buf_puts (o, mc -> args[hit]);
            }

            i = e;
            continue;
        }

        buf_put (o, &c, 1);
        i++;
    }

    if (o -> s == NULL)
    {
        buf_init (o, "", 0);
    }

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Running a body
//
// Lines inside a nested %rep stay raw until that %rep runs, so that %rotate and the
// per-iteration %%labels see the right values.
//
static void  feed_lines (pp_t *p, frame_t *f, char **line, const int *lineno, int n, unsigned iter_serial)
{
    int  k;
    int  depth = 0;
    int  mi    = ctx_find (p, CTX_MACRO);
    int  ri    = ctx_find (p, CTX_REP);

    for (k = 0; k < n; k++)
    {
        const char  *text = line[k];
        size_t       len  = strlen (text);
        buf_t        b    = { 0 };
        char         nm[16];
        int          has  = line_directive (text, len, nm, sizeof (nm));

        if (depth == 0)
        {
            char  err[200];
            int   msg = has && (strcasecmp (nm, "error") == 0 || strcasecmp (nm, "warning") == 0);
            int   lenient = !frame_active (f);

            // an %elif is only looked at when no earlier branch was taken
            if (has && strncasecmp (nm, "elif", 4) == 0 && f -> ncond > 0)
            {
                lenient = (!f -> cond[f -> ncond - 1].parent_active || f -> cond[f -> ncond - 1].taken);
            }

            if (subst_line (mi >= 0 ? &p -> ctx[mi] : NULL, mi >= 0 ? p -> ctx[mi].serial : iter_serial,
                            ri >= 0 ? p -> ctx[ri].iter : 0, msg,
                            lenient, text, len, &b, err, sizeof (err)) != 0)
            {
                pp_error (p, 0, "%s", err);
                free (b.s);
                continue;
            }

            text = b.s;
            len  = b.len;
        }

        if (has && strcasecmp (nm, "rep") == 0)
        {
            depth++;
        }
        else if (has && strcasecmp (nm, "endrep") == 0 && depth > 0)
        {
            depth--;
        }

        if (++p -> work > MAX_WORK_LINES)
        {
            pp_error (p, 0, "too much macro / %%rep expansion (more than %u lines): runaway loop?", MAX_WORK_LINES);
            p -> halt = 1;
            free (b.s);
            return;
        }

        // a %rep outside any macro: its lines are the lines of the file (a macro's output is the invocation's)
        if (lineno != NULL && mi < 0)
        {
            int  saved = f -> line;

            f -> line = lineno[k];
            process_line (p, text, len);
            f -> line = saved;
        }
        else
        {
            process_line (p, text, len);
        }

        free (b.s);

        if (p -> unwind || p -> halt)
        {
            return;
        }
    }
}

// takes over args
static void  exec_macro (pp_t *p, frame_t *f, smacro_t *m, char **args, int nargs)
{
    ctx_t  *c;

    if (p -> nctx >= MAX_CTX_DEPTH)
    {
        pp_error (p, 0, "macro expansion is nested too deeply (is '%s' recursive?)", m -> name);
        p -> halt = 1;                          // not just this call: a macro that recurses twice would report it 2^64 times
        free_args (args, nargs);
        return;
    }

    c = ctx_push (p, CTX_MACRO, f);
    c -> m      = m;
    c -> args   = args;
    c -> nargs  = nargs;
    c -> serial = ++p -> serial;

    feed_lines (p, f, m -> body, NULL, m -> nbody, 0);
    ctx_leave  (p, f);
}

static void  exec_rep (pp_t *p, frame_t *f, long count, char **line, const int *lineno, int n)
{
    long  it;

    if (p -> nctx >= MAX_CTX_DEPTH)
    {
        pp_error (p, 0, "%%rep is nested too deeply");
        p -> halt = 1;
        return;
    }

    ctx_push (p, CTX_REP, f);

    for (it = 1; it <= count; it++)
    {
        unsigned  errors = p -> errors;

        p -> ctx[p -> nctx - 1].iter = (int) it;
        feed_lines (p, f, line, lineno, n, ++p -> serial);
        ctx_balance (p, f);

        if (p -> errors != errors)
        {
            break;                              // one report is enough, not one per iteration
        }

        if (p -> unwind == CTX_REP)
        {
            break;
        }

        if (p -> unwind || p -> halt)
        {
            break;
        }
    }

    ctx_leave (p, f);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Capturing a body
//
static void  cap_begin (pp_t *p, frame_t *f, int kind, smacro_t *m, long count)
{
    cap_reset (p);

    p -> cap.kind    = kind;
    p -> cap.m       = m;
    p -> cap.count   = count;
    p -> cap.nframes = p -> nframes;
    p -> cap.nctx    = p -> nctx;
    p -> cap.start   = f -> line;
    p -> cap.file    = f -> name;
}

static void  cap_add (pp_t *p, const char *s, size_t n, int lineno)
{
    capture_t  *c = &p -> cap;

    if (c -> n == c -> cap)
    {
        c -> cap    = c -> cap ? c -> cap * 2 : 16;
        c -> line   = xrealloc (c -> line,   (size_t) c -> cap * sizeof (char *));
        c -> lineno = xrealloc (c -> lineno, (size_t) c -> cap * sizeof (int));
    }

    c -> line[c -> n]   = xstrndup (s, n);
    c -> lineno[c -> n] = lineno;
    c -> n++;
}

// Takes one raw line while a body is being captured
static void  cap_feed (pp_t *p, frame_t *f, const char *s, size_t n)
{
    capture_t  *c = &p -> cap;
    char        nm[16];
    int         has = line_directive (s, n, nm, sizeof (nm));
    int         opener = 0;
    int         closer = 0;

    if (has)
    {
        if (c -> kind == CTX_MACRO)
        {
            opener = (strcasecmp (nm, "macro") == 0 || strcasecmp (nm, "imacro") == 0);
            closer = (strcasecmp (nm, "endmacro") == 0);
        }
        else
        {
            opener = (strcasecmp (nm, "rep") == 0);
            closer = (strcasecmp (nm, "endrep") == 0);
        }
    }

    if (closer && c -> depth > 0)
    {
        c -> depth--;
    }
    else if (closer)
    {
        capture_t  done = *c;

        memset (c, 0, sizeof (*c));                 // the body now belongs to `done`

        if (done.kind == CTX_MACRO)
        {
            if (done.m != NULL)
            {
                done.m -> body  = done.line;
                done.m -> bline = done.lineno;
                done.m -> nbody = done.n;
                done.line   = NULL;
                done.lineno = NULL;
                done.n      = 0;

                if (smac_add (p, done.m, done.start) == 0)
                {
                    done.m = NULL;                  // now owned by the table
                }
            }
        }
        else if (done.count > 0)
        {
            exec_rep (p, f, done.count, done.line, done.lineno, done.n);
        }

        p -> cap = done;
        p -> cap.kind = CTX_NONE;
        cap_reset (p);
        return;
    }

    if (opener)
    {
        c -> depth++;
    }

    cap_add (p, s, n, f -> line);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Invoking a macro: [labels] NAME arg, arg ...
//
static int  try_invoke (pp_t *p, frame_t *f, const char *s, size_t n)
{
    size_t      i = skip_ws (s, n, 0);
    size_t      prefix_end = 0;
    size_t      b;
    size_t      e;
    size_t      a;
    smacro_t   *m;
    smacro_t   *first;
    int         any = 0;
    char      **av = NULL;
    int         nav = 0;
    char      **args;
    int         nargs;
    int         k;

    if (p -> standard)
    {
        return (0);
    }

    // leading labels
    for (;;)
    {
        size_t  j = i;

        if (j < n && (s[j] == '.' || is_ident_start ((unsigned char) s[j])))
        {
            j++;

            while (j < n && is_ident_char ((unsigned char) s[j]))
            {
                j++;
            }

            if (j < n && s[j] == ':')
            {
                i = skip_ws (s, n, j + 1);
                prefix_end = j + 1;
                continue;
            }
        }

        break;
    }

    b = i;

    if (b >= n || !is_ident_start ((unsigned char) s[b]))
    {
        return (0);
    }

    e = b;

    while (e < n && is_ident_char ((unsigned char) s[e]))
    {
        e++;
    }

    if (e < n && !is_ws ((unsigned char) s[e]) && s[e] != ';')
    {
        return (0);
    }

    first = smac_pick (p, s + b, e - b, -1, &any);

    if (!any)
    {
        return (0);
    }

    a = find_comment (s, n, e);
    split_args (s + e, a - e, &av, &nav);

    m = smac_pick (p, s + b, e - b, nav, NULL);

    if (prefix_end > 0)
    {
        emit_line (p, f, s, prefix_end);
    }

    if (m == NULL)
    {
        char  want[48];

        if (first -> max < 0)
        {
            snprintf (want, sizeof (want), "at least %d", first -> min);
        }
        else if (first -> max == first -> min)
        {
            snprintf (want, sizeof (want), "%d", first -> min);
        }
        else
        {
            snprintf (want, sizeof (want), "%d to %d", first -> min, first -> max);
        }

        pp_error (p, 0, "macro '%s' takes %s argument%s, but %d given", first -> name, want,
                  first -> min == 1 && first -> max == 1 ? "" : "s", nav);
        free_args (av, nav);
        return (1);
    }

    // the actual arguments: the named ones (defaults filling the gaps), then any extras
    {
        int  fixed = m -> nparams - (m -> variadic ? 1 : 0);

        args  = NULL;
        nargs = 0;

        for (k = 0; k < fixed; k++)
        {
            const char  *v = NULL;

            if (k < nav && av[k][0] != 0)
            {
                v = av[k];
            }
            else if (m -> defaults[k] != NULL)
            {
                v = m -> defaults[k];
            }
            else if (k < nav)
            {
                v = av[k];
            }
            else
            {
                v = "";
            }

            args = xrealloc (args, (size_t) (nargs + 1) * sizeof (char *));
            args[nargs++] = xstrndup (v, strlen (v));
        }

        for (k = fixed; k < nav; k++)
        {
            args = xrealloc (args, (size_t) (nargs + 1) * sizeof (char *));
            args[nargs++] = xstrndup (av[k], strlen (av[k]));
        }
    }

    free_args (av, nav);
    exec_macro (p, f, m, args, nargs);

    return (1);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Operand tests: %ifreg %ifnum %ifstr %iflabel %ifport %ifidn %ifidni %ifempty %ifmacro
// (and their negations %ifnreg ..., %elifreg ...)
//
enum { P_NONE = 0, P_REG, P_SYSREG, P_NUM, P_STR, P_LABEL, P_PORT, P_IDN, P_IDNI, P_EMPTY, P_MACRO, P_CTX };

static int  pred_kind (const char *w)
{
    static const struct { const char *name; int kind; } t[] =
    {
        { "reg", P_REG }, { "sysreg", P_SYSREG }, { "num", P_NUM }, { "str", P_STR },
        { "label", P_LABEL }, { "port", P_PORT }, { "idn", P_IDN }, { "idni", P_IDNI },
        { "empty", P_EMPTY }, { "macro", P_MACRO }, { "ctx", P_CTX }
    };
    size_t  i;

    for (i = 0; i < sizeof (t) / sizeof (t[0]); i++)
    {
        if (strcasecmp (w, t[i].name) == 0)
        {
            return (t[i].kind);
        }
    }

    return (P_NONE);
}

// "ifreg", "elifnnum", ... ?
static int  pred_parse (const char *name, int *is_elif, int *neg, int *kind)
{
    const char  *w;

    if (strncasecmp (name, "elif", 4) == 0)
    {
        *is_elif = 1;
        w = name + 4;
    }
    else if (strncasecmp (name, "if", 2) == 0)
    {
        *is_elif = 0;
        w = name + 2;
    }
    else
    {
        return (0);
    }

    *neg  = 0;
    *kind = pred_kind (w);

    if (*kind == P_NONE && (w[0] == 'n' || w[0] == 'N'))
    {
        *neg  = 1;
        *kind = pred_kind (w + 1);
    }

    return (*kind != P_NONE);
}

static int  single_word (const char *t, size_t n)
{
    size_t  i;

    if (n == 0)
    {
        return (0);
    }

    for (i = 0; i < n; i++)
    {
        if (!is_ident_char ((unsigned char) t[i]))
        {
            return (0);
        }
    }

    return (1);
}

// Evaluates the test on the rest of the directive line. -1: an error was reported.
static int  run_pred (pp_t *p, int kind, int neg, const char *s, size_t n, size_t j, int *value)
{
    size_t       b = skip_ws (s, n, j);
    size_t       e = find_comment (s, n, b);
    buf_t        x;
    hides_t      h = { 0 };
    int          changed = 0;
    const char  *why = NULL;
    int          v = 0;
    size_t       tb;
    size_t       te;
    const char  *t;
    const kw_t  *kw;

    trim_range (s, &b, &e);

    if (kind == P_CTX)
    {
        // %ifctx NAME: is the innermost context called that?
        char  *w = xstrndup (s + b, e - b);

        v = (p -> ncstack > 0 && strcasecmp (p -> cname[p -> ncstack - 1], w) == 0);
        free (w);
        *value = v != neg;
        return (0);
    }

    if (kind == P_MACRO)
    {
        // %ifmacro NAME [argcount]
        size_t  ne = b;
        int     any = 0;
        int     cnt = -1;

        while (ne < e && is_ident_char ((unsigned char) s[ne]))
        {
            ne++;
        }

        if (ne == b)
        {
            pp_error (p, (int) b + 1, "expected a macro name");
            return (-1);
        }

        if (skip_ws (s, e, ne) < e)
        {
            cnt = atoi (s + skip_ws (s, e, ne));
        }

        v = (smac_pick (p, s + b, ne - b, cnt, &any) != NULL) || (cnt < 0 && any);
        *value = v != neg;
        return (0);
    }

    buf_init (&x, s + b, e - b);

    if (expand_buffer (p, &x, &h, 1, &changed, &why) != 0)
    {
        pp_error (p, (int) b + 1, "%s", why);
        free (x.s);
        free (h.v);
        return (-1);
    }

    free (h.v);

    tb = 0;
    te = x.len;
    trim_range (x.s, &tb, &te);
    t  = x.s + tb;
    te -= tb;
    kw = single_word (t, te) ? kw_lookup (t, te) : NULL;

    switch (kind)
    {
        case P_EMPTY:
            v = (te == 0);
            break;

        case P_REG:
            v = (kw != NULL && kw -> kind == KW_REGISTER);
            break;

        case P_SYSREG:
            v = (kw != NULL && kw -> kind == KW_SYSREG);
            break;

        case P_PORT:
            v = (kw != NULL && kw -> kind == KW_PORT);
            break;

        case P_STR:
            v = (te >= 2 && t[0] == '"' && skip_string (t, te, 0) == te && t[te - 1] == '"');
            break;

        case P_LABEL:
            v = (te >= 1 && (t[0] == '_' || (t[0] == '.' && te > 1)) && single_word (t + (t[0] == '.'), te - (t[0] == '.'))
                 && kw == NULL);
            break;

        case P_NUM:
            if (te > 0 && !(kw != NULL && (kw -> kind == KW_PORT || kw -> kind == KW_REGISTER || kw -> kind == KW_SYSREG)))
            {
                ppval_t  pv;
                char     err[160];

                v = (ppexpr_eval (t, te, &pv, err, sizeof (err)) == 0);
            }
            break;

        case P_IDN:
        case P_IDNI:
        {
            size_t  c = scan_arg_end (t, te, 0);

            if (c >= te || scan_arg_end (t, te, c + 1) < te)
            {
                pp_error (p, (int) b + 1, "expected two operands separated by a comma");
                free (x.s);
                return (-1);
            }

            {
                size_t  b1 = 0, e1 = c, b2 = c + 1, e2 = te;

                trim_range (t, &b1, &e1);
                trim_range (t, &b2, &e2);

                v = (e1 - b1 == e2 - b2)
                    && (kind == P_IDN ? memcmp (t + b1, t + b2, e1 - b1) == 0
                                      : strncasecmp (t + b1, t + b2, e1 - b1) == 0);
            }
            break;
        }

        default:
            break;
    }

    free (x.s);
    *value = (v != 0) != neg;

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Helpers for the string directives and %pragma
//
// A string literal for text with the given bytes (escapes the scanner understands)
static void  quote_into (buf_t *o, const char *s, size_t n)
{
    size_t  i;

    buf_put (o, "\"", 1);

    for (i = 0; i < n; i++)
    {
        unsigned char  c = (unsigned char) s[i];

        if (c == '"' || c == '\\')
        {
            buf_put (o, "\\", 1);
            buf_put (o, s + i, 1);
        }
        else if (c == '\n')
        {
            buf_put (o, "\\n", 2);
        }
        else if (c == '\r')
        {
            buf_put (o, "\\r", 2);
        }
        else if (c == '\t')
        {
            buf_put (o, "\\t", 2);
        }
        else if (c < 32 || c == 127)
        {
            char  hex[8];

            snprintf (hex, sizeof (hex), "\\x%02X", c);
            buf_puts (o, hex);
        }
        else
        {
            buf_put (o, s + i, 1);
        }
    }

    buf_put (o, "\"", 1);
}

// The rest of a directive line (before any comment), macro-expanded, in out
static int  expand_rest (pp_t *p, const char *s, size_t n, size_t j, buf_t *out)
{
    size_t       b = skip_ws (s, n, j);
    size_t       e = find_comment (s, n, b);
    hides_t      h = { 0 };
    int          changed = 0;
    const char  *why = NULL;

    trim_range (s, &b, &e);
    buf_init (out, s + b, e - b);

    if (expand_buffer (p, out, &h, 0, &changed, &why) != 0)
    {
        pp_error (p, (int) b + 1, "%s", why);
        free (out -> s);
        out -> s = NULL;
        free (h.v);
        return (-1);
    }

    free (h.v);

    return (0);
}

static void  define_string (pp_t *p, const char *name, const char *bytes, size_t n)
{
    buf_t  q = { 0 };

    quote_into (&q, bytes, n);
    macro_set (p, name, q.s);
    free (q.s);
}

// Evaluates an integer expression; reports an error and returns -1 otherwise
static int  eval_int (pp_t *p, const char *t, size_t n, long *out)
{
    ppval_t  v;
    char     err[160];
    size_t   b = 0;
    size_t   e = n;

    trim_range (t, &b, &e);

    if (b == e)
    {
        pp_error (p, 0, "expected a number");
        return (-1);
    }

    if (ppexpr_eval (t + b, e - b, &v, err, sizeof (err)) != 0)
    {
        pp_error (p, 0, "%s", err);
        return (-1);
    }

    if (v.is_float)
    {
        pp_error (p, 0, "expected an integer, not a float");
        return (-1);
    }

    *out = (long) v.i;

    return (0);
}

static void  do_string_directive (pp_t *p, const char *dname, const char *s, size_t n, size_t j)
{
    char   *name = expect_ident (p, s, n, &j, "name is missing");
    buf_t   x    = { 0 };
    size_t  i    = 0;
    char   *str;

    if (name == NULL)
    {
        return;
    }

    if (expand_rest (p, s, n, j, &x) != 0)
    {
        free (name);
        return;
    }

    if (dir_is (p, dname, "defstr"))
    {
        define_string (p, name, x.s, x.len);
    }
    else if (dir_is (p, dname, "strcat"))
    {
        buf_t  all = { 0 };
        int    ok  = 1;

        for (;;)
        {
            str = expect_string (p, x.s, x.len, &i, "expected a string");

            if (str == NULL)
            {
                ok = 0;
                break;
            }

            buf_puts (&all, str);
            free (str);
            i = skip_ws (x.s, x.len, i);

            if (i < x.len && x.s[i] == ',')
            {
                i++;
                continue;
            }

            if (i < x.len)
            {
                pp_error (p, 0, "expected ',' or the end of the line");
                ok = 0;
            }

            break;
        }

        if (ok)
        {
            define_string (p, name, all.s ? all.s : "", all.len);
        }

        free (all.s);
    }
    else
    {
        // %strlen NAME "text"        %substr NAME "text", start [, length]  (start counts from 1)
        str = expect_string (p, x.s, x.len, &i, "expected a string");

        if (str != NULL)
        {
            size_t  slen = strlen (str);

            i = skip_ws (x.s, x.len, i);

            if (dir_is (p, dname, "strlen"))
            {
                if (i < x.len)
                {
                    pp_error (p, 0, "expected end of line");
                }
                else
                {
                    char  num[32];

                    snprintf (num, sizeof (num), "%zu", slen);
                    macro_set (p, name, num);
                }
            }
            else if (i >= x.len || x.s[i] != ',')
            {
                pp_error (p, 0, "expected ',' and a start position");
            }
            else
            {
                size_t  e1 = scan_arg_end (x.s, x.len, i + 1);
                long    start = 0;
                long    len   = -1;
                int     ok    = (eval_int (p, x.s + i + 1, e1 - (i + 1), &start) == 0);

                if (ok && e1 < x.len)
                {
                    ok = (eval_int (p, x.s + e1 + 1, x.len - e1 - 1, &len) == 0);
                }

                if (ok && (start < 1 || (size_t) start > slen + 1))
                {
                    pp_error (p, 0, "the start position %ld is outside the string (1..%zu)", start, slen + 1);
                    ok = 0;
                }

                if (ok && (len < -1 || (len >= 0 && (size_t) (start - 1) + (size_t) len > slen)))
                {
                    pp_error (p, 0, "the length %ld runs past the end of the string", len);
                    ok = 0;
                }

                if (ok)
                {
                    size_t  take = (len < 0) ? slen - (size_t) (start - 1) : (size_t) len;

                    define_string (p, name, str + start - 1, take);
                }
            }

            free (str);
        }
    }

    free (x.s);
    free (name);
}

static void  do_include_once (pp_t *p, frame_t *f)
{
    if (f -> canon != NULL && !once_contains (p, f -> canon))
    {
        p -> once = xrealloc (p -> once, (size_t) (p -> nonce + 1) * sizeof (char *));
        p -> once[p -> nonce++] = xstrndup (f -> canon, strlen (f -> canon));
    }
}

// %pragma once   |   %pragma shadow [NAME, ...]
static void  do_pragma (pp_t *p, frame_t *f, const char *s, size_t n, size_t j, size_t dstart)
{
    char  *what = expect_ident (p, s, n, &j, "expected a pragma name");

    if (what == NULL)
    {
        return;
    }

    if (strcasecmp (what, "once") == 0)
    {
        if (expect_eol (p, s, n, j))
        {
            do_include_once (p, f);
        }
    }
    else if (strcasecmp (what, "shadow") == 0)
    {
        size_t  e = find_comment (s, n, j);

        if (skip_ws (s, e, j) >= e)
        {
            p -> shadow_all = 1;
        }
        else
        {
            for (;;)
            {
                char  *nm = expect_ident (p, s, n, &j, "expected a name");

                if (nm == NULL)
                {
                    break;
                }

                p -> shadow = xrealloc (p -> shadow, (size_t) (p -> nshadow + 1) * sizeof (char *));
                p -> shadow[p -> nshadow++] = nm;
                j = skip_ws (s, n, j);

                if (j < n && s[j] == ',')
                {
                    j++;
                    continue;
                }

                expect_eol (p, s, n, j);
                break;
            }
        }
    }
    else
    {
        pp_warning_at (p, f -> line, (int) dstart + 1, "unknown pragma '%s' ignored", what);
    }

    free (what);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Contexts: %push NAME ... %pop. "%$name" is a name private to the innermost context
// (%$$name: the one below it, ...). That is what makes IF/ELSE/ENDIF macro sets possible.
//
static void  ctx_name_push (pp_t *p, const char *name)
{
    if (p -> ncstack == p -> capcstack)
    {
        p -> capcstack = p -> capcstack ? p -> capcstack * 2 : 8;
        p -> cname     = xrealloc (p -> cname, (size_t) p -> capcstack * sizeof (char *));
        p -> cid       = xrealloc (p -> cid,   (size_t) p -> capcstack * sizeof (unsigned));
    }

    p -> cname[p -> ncstack] = xstrndup (name, strlen (name));
    p -> cid[p -> ncstack]   = ++p -> cserial;
    p -> ncstack++;
}

// Rewrites every %$name in a line as __C<id>_name. Returns 0, or -1 (error reported) if a
// name refers to a context that does not exist.
static int  ctx_rewrite (pp_t *p, const char *s, size_t n, buf_t *o, int report)
{
    size_t  i = 0;

    while (i < n)
    {
        char  c = s[i];

        if (c == ';')
        {
            buf_put (o, s + i, n - i);
            break;
        }

        if (c == '"')
        {
            size_t  e = skip_string (s, n, i);

            buf_put (o, s + i, e - i);
            i = e;
            continue;
        }

        if (c == '\'')
        {
            size_t  k = char_literal_len (s, n, i);

            if (k == 0)
            {
                k = 1;
            }

            buf_put (o, s + i, k);
            i += k;
            continue;
        }

        if (c == '%' && i + 1 < n && s[i + 1] == '$')
        {
            size_t  e = i + 1;
            int     up = 0;
            size_t  ns;

            while (e < n && s[e] == '$')
            {
                up++;
                e++;
            }

            ns = e;

            while (e < n && is_ident_char ((unsigned char) s[e]))
            {
                e++;
            }

            if (e == ns)
            {
                buf_put (o, s + i, e - i);              // "%$" with no name: left alone
                i = e;
                continue;
            }

            if (up > p -> ncstack)
            {
                if (report)
                {
                    pp_error (p, (int) i + 1, p -> ncstack == 0 ? "%%$%.*s used outside any %%push context"
                                                                 : "%%$%.*s reaches past the outermost context",
                              (int) (e - ns), s + ns);
                    return (-1);
                }

                buf_put (o, s + i, e - i);
            }
            else
            {
                char  pre[32];

                snprintf (pre, sizeof (pre), "__C%u_", p -> cid[p -> ncstack - up]);
                buf_puts (o, pre);
                buf_put  (o, s + ns, e - ns);
            }

            i = e;
            continue;
        }

        buf_put (o, &c, 1);
        i++;
    }

    if (o -> s == NULL)
    {
        buf_init (o, "", 0);
    }

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// The ROM definition: %title "text", %version MAJOR.MINOR, %texture [NAME] "file", %sound [NAME] "file"
//
// They only collect what `v32as --rom-xml` / `--build-scripts` write (see buildscript.c); the assembled
// program is the same with or without them.  NAME becomes a macro holding the resource's number in its own
// list (textures and sounds are numbered separately, from 0, in order of first appearance; the same file named
// again keeps its number), the value the program passes to select_texture / select_sound.
//
#define ROM_TITLE_MAX  63               // the cartridge header has room for 63 characters and a terminator

// where a resource file is: next to the file holding the directive, then the -I directories (what datafile does)
static char  *rom_find (pp_t *p, frame_t *f, const char *path)
{
    const char  *slash = strrchr (f -> name, '/');
    char        *cand;
    FILE        *fp;
    int          d;

    if (path[0] == '/')
    {
        fp = fopen (path, "rb");

        if (fp != NULL)
        {
            fclose (fp);
            return (xstrndup (path, strlen (path)));
        }

        return (NULL);
    }

    cand = join_path (f -> name, slash ? (size_t) (slash - f -> name) : 0, path);
    fp   = fopen (cand, "rb");

    if (fp != NULL)
    {
        fclose (fp);
        return (cand);
    }

    free (cand);

    for (d = 0; d < p -> nincdirs; d++)
    {
        cand = join_path (p -> incdirs[d], strlen (p -> incdirs[d]), path);
        fp   = fopen (cand, "rb");

        if (fp != NULL)
        {
            fclose (fp);
            return (cand);
        }

        free (cand);
    }

    return (NULL);
}

static int  rom_version_ok (const char *v)
{
    size_t  i = 0;
    size_t  digits = 0;

    for (; isdigit ((unsigned char) v[i]); i++)  { digits++; }

    if (digits == 0 || v[i] != '.')
    {
        return (0);
    }

    for (digits = 0, i++; isdigit ((unsigned char) v[i]); i++)  { digits++; }

    return (digits > 0 && v[i] == 0);
}

static void  rom_set_text (pp_t *p, frame_t *f, size_t dstart, char **slot, char *value, const char *what)
{
    if (*slot != NULL && strcmp (*slot, value) != 0 && p -> warn_redefine)
    {
        pp_warning_at (p, f -> line, (int) dstart + 1, "%%%s changes the %s from \"%s\" (the last one wins)", what, what, *slot);
    }

    free (*slot);
    *slot = value;
}

static void  do_rom_directive (pp_t *p, frame_t *f, const char *name, const char *s, size_t n, size_t j, size_t dstart)
{
    if (dir_is (p, name, "title"))
    {
        char  *t = expect_string (p, s, n, &j, "the title is missing");

        if (t == NULL)
        {
            return;
        }

        if (!expect_eol (p, s, n, j))
        {
            free (t);
            return;
        }

        if (t[0] == 0)
        {
            pp_error (p, (int) dstart + 1, "the title is empty");
            free (t);
            return;
        }

        if (strlen (t) > ROM_TITLE_MAX)
        {
            size_t  cut = ROM_TITLE_MAX;

            while (cut > 0 && ((unsigned char) t[cut] & 0xC0) == 0x80)      // not in the middle of a UTF-8 character
            {
                cut--;
            }

            pp_warning_at (p, f -> line, (int) dstart + 1, "the title is longer than %d characters; it is cut to \"%.*s\"",
                           ROM_TITLE_MAX, (int) cut, t);
            t[cut] = 0;
        }

        rom_set_text (p, f, dstart, &p -> rom_title, t, "title");
    }
    else if (dir_is (p, name, "version"))
    {
        size_t  b = skip_ws (s, n, j);
        char   *v;

        if (b < n && s[b] == '"')
        {
            v = expect_string (p, s, n, &j, "the version is missing");

            if (v == NULL || !expect_eol (p, s, n, j))
            {
                free (v);
                return;
            }
        }
        else
        {
            size_t  e = find_comment (s, n, b);

            while (e > b && is_ws ((unsigned char) s[e - 1]))
            {
                e--;
            }

            if (e == b)
            {
                pp_error (p, (int) b + 1, "the version is missing");
                return;
            }

            v = xstrndup (s + b, e - b);
        }

        if (!rom_version_ok (v))
        {
            pp_error (p, (int) dstart + 1, "the version \"%s\" is not of the form MAJOR.MINOR (digits, a dot, digits) that packrom accepts", v);
            free (v);
            return;
        }

        rom_set_text (p, f, dstart, &p -> rom_version, v, "version");
    }
    else
    {
        int     sound = dir_is (p, name, "sound");
        char   *id    = NULL;
        char   *hint;
        size_t  b = skip_ws (s, n, j);
        size_t  r;

        if (b < n && is_ident_start ((unsigned char) s[b]))
        {
            id = expect_ident (p, s, n, &j, "expected a name");
        }

        hint = expect_string (p, s, n, &j, "the file name is missing (expected [NAME] \"file\")");

        if (hint == NULL || !expect_eol (p, s, n, j))
        {
            free (id);
            free (hint);
            return;
        }

        if (hint[0] == 0)
        {
            pp_error (p, (int) dstart + 1, "the file name of the %s is empty", name);
            free (id);
            free (hint);
            return;
        }

        for (r = 0; r < p -> nrom_res[sound] && strcmp (p -> rom_res[sound][r].hint, hint) != 0; r++)
        {
        }

        if (r == p -> nrom_res[sound])                       // the first time this file is named
        {
            if (r == p -> caprom_res[sound])
            {
                p -> caprom_res[sound] = r ? r * 2 : 8;
                p -> rom_res[sound]    = xrealloc (p -> rom_res[sound], p -> caprom_res[sound] * sizeof (romres_t));
            }

            p -> rom_res[sound][r].hint  = hint;
            p -> rom_res[sound][r].found = rom_find (p, f, hint);
            p -> nrom_res[sound]++;

            if (p -> rom_res[sound][r].found == NULL)
            {
                pp_warning_at (p, f -> line, (int) dstart + 1, "cannot find the %s file \"%s\" (next to %s or in the -I directories)",
                               sound ? "sound" : "texture", hint, f -> name);
            }
        }
        else
        {
            free (hint);
        }

        if (id != NULL)
        {
            char  num[24];

            snprintf (num, sizeof (num), "%u", (unsigned) r);
            macro_set (p, id, num);
        }

        free (id);
    }
}

static void  directive (pp_t *p, frame_t *f, const char *s, size_t n, size_t dstart)
{
    size_t  i = skip_ws (s, n, dstart + 1);
    size_t  j;
    char    name[32];
    int     active = frame_active (f);
    int     is_cond;
    int     pe, pn, pk;

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
                               || dir_is (p, name, "elifdef") || dir_is (p, name, "elifndef")
                               || pred_parse (name, &pe, &pn, &pk)));

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
        do_define (p, s, n, j, 0, 0);
    }
    else if (!p -> standard && dir_is (p, name, "idefine"))
    {
        do_define (p, s, n, j, 1, 0);
    }
    else if (!p -> standard && dir_is (p, name, "xdefine"))
    {
        do_define (p, s, n, j, 0, 1);
    }
    else if (!p -> standard && dir_is (p, name, "ixdefine"))
    {
        do_define (p, s, n, j, 1, 1);
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
        else if (f -> ncond <= cond_base (p, f))
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
        if (f -> ncond <= cond_base (p, f))
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
            if (f -> ncond <= cond_base (p, f))
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
            if (f -> ncond <= cond_base (p, f))
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
        if (expect_eol (p, s, n, j))
        {
            do_include_once (p, f);
        }
    }
    else if (!p -> standard && pred_parse (name, &pe, &pn, &pk))
    {
        int  value = 0;

        if (!pe)
        {
            int  parent = frame_active (f);

            if (parent && run_pred (p, pk, pn, s, n, j, &value) != 0)
            {
                value = 0;
            }

            cond_open (f, value, parent);
        }
        else if (f -> ncond <= cond_base (p, f))
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
                if (run_pred (p, pk, pn, s, n, j, &value) != 0)
                {
                    value = 0;
                }

                c -> state = value;
                c -> taken = value;
            }
        }
    }
    else if (!p -> standard && (dir_is (p, name, "macro") || dir_is (p, name, "imacro")))
    {
        int        icase = dir_is (p, name, "imacro");
        char      *mname = expect_ident (p, s, n, &j, "macro name is missing");
        smacro_t  *m     = NULL;

        if (mname != NULL)
        {
            m = smac_parse_header (p, s, n, j, mname, icase);
            free (mname);
        }

        cap_begin (p, f, CTX_MACRO, m, 0);                // (a NULL macro: swallow the body quietly)
    }
    else if (!p -> standard && dir_is (p, name, "rep"))
    {
        ppval_t  v;
        long     count = -1;

        if (eval_rest (p, s, n, j, &v) == 0)
        {
            if (v.is_float)
            {
                pp_error (p, (int) dstart + 1, "the %%rep count must be an integer");
            }
            else if (v.i < 0)
            {
                pp_error (p, (int) dstart + 1, "the %%rep count is negative (%d)", (int) v.i);
            }
            else if (v.i > MAX_REP_COUNT)
            {
                pp_error (p, (int) dstart + 1, "the %%rep count %d is over the limit of %d", (int) v.i, MAX_REP_COUNT);
            }
            else
            {
                count = (long) v.i;
            }
        }

        cap_begin (p, f, CTX_REP, NULL, count);
    }
    else if (!p -> standard && (dir_is (p, name, "endmacro") || dir_is (p, name, "endrep")))
    {
        pp_error (p, (int) dstart + 1, "%%%s with no matching %%%s", name,
                  dir_is (p, name, "endmacro") ? "macro" : "rep");
    }
    else if (!p -> standard && (dir_is (p, name, "exitmacro") || dir_is (p, name, "exitrep")))
    {
        int  kind = dir_is (p, name, "exitmacro") ? CTX_MACRO : CTX_REP;

        if (expect_eol (p, s, n, j))
        {
            if (ctx_find (p, kind) < 0)
            {
                pp_error (p, (int) dstart + 1, "%%%s outside a %s", name, kind == CTX_MACRO ? "macro" : "%rep block");
            }
            else
            {
                p -> unwind = kind;
            }
        }
    }
    else if (!p -> standard && dir_is (p, name, "rotate"))
    {
        int  mi = ctx_find (p, CTX_MACRO);

        if (mi < 0)
        {
            pp_error (p, (int) dstart + 1, "%%rotate outside a macro");
        }
        else
        {
            ppval_t  v;

            if (eval_rest (p, s, n, j, &v) == 0)
            {
                ctx_t  *c = &p -> ctx[mi];

                if (v.is_float)
                {
                    pp_error (p, (int) dstart + 1, "%%rotate takes an integer");
                }
                else if (c -> nargs > 0)
                {
                    long  r = ((long) c -> rot + (long) v.i) % c -> nargs;

                    c -> rot = (int) (r < 0 ? r + c -> nargs : r);
                }
            }
        }
    }
    else if (!p -> standard && dir_is (p, name, "unmacro"))
    {
        char  *mname = expect_ident (p, s, n, &j, "macro name is missing");

        if (mname != NULL)
        {
            if (expect_eol (p, s, n, j))
            {
                int  count = 0;

                smac_unlink_all (p, mname, strlen (mname), &count);

                if (count == 0)
                {
                    pp_warning_at (p, f -> line, (int) dstart + 1, "there is no macro named '%s' to remove", mname);
                }
            }

            free (mname);
        }
    }
    else if (!p -> standard && (dir_is (p, name, "strlen") || dir_is (p, name, "substr")
                                || dir_is (p, name, "strcat") || dir_is (p, name, "defstr")))
    {
        do_string_directive (p, name, s, n, j);
    }
    else if (!p -> standard && dir_is (p, name, "pragma"))
    {
        do_pragma (p, f, s, n, j, dstart);
    }
    else if (!p -> standard && (dir_is (p, name, "title") || dir_is (p, name, "version")
                                || dir_is (p, name, "texture") || dir_is (p, name, "sound")))
    {
        do_rom_directive (p, f, name, s, n, j, dstart);
    }
    else if (!p -> standard && dir_is (p, name, "push"))
    {
        char  *cn = NULL;
        size_t k  = skip_ws (s, n, j);

        if (!at_eol (s, n, k))
        {
            cn = expect_ident (p, s, n, &j, "expected a context name");

            if (cn == NULL)
            {
                return;
            }
        }

        if (expect_eol (p, s, n, j))
        {
            ctx_name_push (p, cn ? cn : "");
        }

        free (cn);
    }
    else if (!p -> standard && (dir_is (p, name, "pop") || dir_is (p, name, "repl")))
    {
        char  *cn = NULL;
        size_t k  = skip_ws (s, n, j);
        int    is_pop = dir_is (p, name, "pop");

        if (!at_eol (s, n, k))
        {
            cn = expect_ident (p, s, n, &j, "expected a context name");

            if (cn == NULL)
            {
                return;
            }
        }

        if (!expect_eol (p, s, n, j))
        {
        }
        else if (p -> ncstack == 0)
        {
            pp_error (p, (int) dstart + 1, "%%%s with no context (no matching %%push)", name);
        }
        else if (!is_pop && cn == NULL)
        {
            pp_error (p, (int) dstart + 1, "%%repl needs the new context name");
        }
        else if (is_pop)
        {
            if (cn != NULL && strcasecmp (cn, p -> cname[p -> ncstack - 1]) != 0)
            {
                pp_error (p, (int) dstart + 1, "%%pop %s, but the innermost context is '%s'", cn,
                          p -> cname[p -> ncstack - 1]);
            }
            else
            {
                free (p -> cname[--p -> ncstack]);
            }
        }
        else
        {
            free (p -> cname[p -> ncstack - 1]);
            p -> cname[p -> ncstack - 1] = xstrndup (cn, strlen (cn));
        }

        free (cn);
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

    if (p -> halt)
    {
        return;
    }

    // while a %macro / %rep body is being captured, lines are only stored
    if (p -> cap.kind != CTX_NONE)
    {
        cap_feed (p, f, s, n);
        return;
    }

    // %$names of the context stack
    if (!p -> standard && memchr (s, '$', n) != NULL)
    {
        buf_t  r = { 0 };
        int    rc;

        if (strstr (s, "%$") != NULL)
        {
            rc = ctx_rewrite (p, s, n, &r, frame_active (f));

            if (rc == 0 && (r.len != n || memcmp (r.s, s, n) != 0))
            {
                process_line (p, r.s, r.len);
                free (r.s);
                return;
            }

            free (r.s);

            if (rc != 0)
            {
                return;
            }
        }
    }

    // "%eval(...)" at the start of a line is source text that happens to begin with an
    // expression, not a directive
    if (i < n && s[i] == '%' && !(!p -> standard && inline_eval_paren (s, n, i)))
    {
        directive (p, f, s, n, i);
        return;
    }

    if (frame_active (f))
    {
        if (!try_invoke (p, f, s, n))
        {
            emit_line (p, f, s, n);
        }
    }
}

static unsigned  process_end (pp_t *p, frame_t *f, unsigned before);

static unsigned  process_text (pp_t *p, const char *name, const char *text, size_t len)
{
    frame_t   *f      = frame_push (p, name);
    unsigned   before = p -> errors;
    size_t     pos    = 0;

    f -> canon = v32_realpath (name, NULL);

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

    return (process_end (p, f, before));
}

// the end of a file (text or stream): what was left open is an error
static unsigned  process_end (pp_t *p, frame_t *f, unsigned before)
{
    int  k;

    // a body that was still being captured when this file ended
    if (p -> cap.kind != CTX_NONE && p -> cap.nframes == p -> nframes)
    {
        pp_error_at (p, p -> cap.start, 0, "unterminated %s (no matching %s before end of file)",
                     p -> cap.kind == CTX_MACRO ? "%macro" : "%rep",
                     p -> cap.kind == CTX_MACRO ? "%endmacro" : "%endrep");
        cap_reset (p);
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

    if (p -> nframes == 1 && p -> ncstack > 0)
    {
        pp_error_at (p, f -> line, 0, "unterminated %%push context '%s' (no matching %%pop before end of file)",
                     p -> cname[p -> ncstack - 1]);

        while (p -> ncstack > 0)
        {
            free (p -> cname[--p -> ncstack]);
        }
    }

    frame_pop (p);

    return (p -> errors - before);
}

// A file read a line at a time (pp_process_file): the text is never all in memory at once next to the output (v32nix: a
// character is a word, a linked program's assembly is ~75 K of them, and the output is as long again)
static unsigned  process_stream (pp_t *p, const char *name, FILE *fp)
{
    frame_t   *f      = frame_push (p, name);
    unsigned   before = p -> errors;
    buf_t      line   = { 0 };
    int        c, first = 1;

    f -> canon = v32_realpath (name, NULL);

    for (;;)
    {
        line.len = 0;
        while ((c = getc (fp)) != EOF && c != '\n')
        {
            char  ch = (char) c;

            buf_put (&line, &ch, 1);
        }
        if (c == EOF && line.len == 0)
        {
            break;
        }
        if (line.len > 0 && line.s[line.len - 1] == '\r')
        {
            line.len--;
        }
        {
            const char  *s = line.s ? line.s : "";
            size_t       n = line.len;

            if (first && n >= 3 && (unsigned char) s[0] == 0xEF && (unsigned char) s[1] == 0xBB && (unsigned char) s[2] == 0xBF)
            {
                s += 3;
                n -= 3;
            }
            first = 0;
            f -> line++;
            process_line (p, s, n);
        }
        if (c == EOF)
        {
            break;
        }
    }
    free (line.s);

    return (process_end (p, f, before));
}

unsigned  pp_process (pp_t *p, const char *name, const char *text, size_t len)
{
    return (process_text (p, name, text, len));
}

unsigned  pp_process_file (pp_t *p, const char *name, FILE *fp)
{
    return (process_stream (p, name, fp));
}

void  pp_stream_begin (pp_t *p, const char *name)
{
    frame_push (p, name);
}

int  pp_pending (const pp_t *p)
{
    return (p -> cap.kind != CTX_NONE || (p -> nframes > 0 && p -> frames[p -> nframes - 1] -> ncond > 0));
}

unsigned  pp_stream_line (pp_t *p, const char *line, size_t len, int lineno)
{
    unsigned  before = p -> errors;

    p -> halt = 0;                  // each line typed is a new run: a runaway macro must not silence the session
    p -> work = 0;

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

        if (markers && (p -> map[k].file != file
                        || (p -> map[k].line != next && !(p -> map[k].expanded && p -> map[k].line == next - 1))))
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
