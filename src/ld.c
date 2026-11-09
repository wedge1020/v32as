//
// ld.c: v32asld, the linker for the relocatable objects (.v32ob) written by "v32as -C"
//
//     v32asld [-b|-c|-O ADDR] [-o FILE] [-g[MODE]] [-r FILE] [-w] OBJECT...
//
// Objects are placed one after the other, in command line order (the first word of the
// first object is the program's entry point). Every word the objects mark as an address
// is rewritten with the final address of its symbol; symbols are the labels of the
// objects, and 'global' ones are shared between objects. The output is a VBIN, exactly
// what "v32as" writes. The format of the objects is described in docs/OBJECT.md.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define  VERSION      "20261109-dev"
#define  BIOS_BASE    0x10000000u
#define  CART_BASE    0x20000000u

#define  SYM_DEFINED  1u
#define  SYM_GLOBAL   2u
#define  SYM_EXTERN   4u
#define  SYM_CODE     8u        // the label is followed by code (set by v32as -C)
#define  SYM_DATASYM  16u       // a datasym: the base of the data, value 0 wherever the object goes (version 2)
#define  NO_SYMBOL    0xFFFFFFFFu
#define  NO_STRING    0xFFFFFFFFu
#define  NO_FUNC      0xFFFFFFFEu     // a ;@src entry with no function: the debug file gets no label either

typedef struct
{
    const char  *name;
    uint32_t     value;             // word offset in its object
    uint32_t     flags;
}
osym_t;

typedef struct
{
    uint32_t  word;
    uint32_t  sym;
    uint32_t  addend;
}
orel_t;

typedef struct
{
    uint32_t  word;
    uint32_t  file;                 // offset in the string table
    uint32_t  line;
}
odbg_t;

typedef struct
{
    const char     *path;
    unsigned char  *raw;
    uint32_t        version, dbgsz;     // dbgsz: bytes per debug record (12; 16 in version 2: + function name)
    uint32_t        nwords, nsyms, nrels, ndbg, strsize;
    uint32_t        nvars;          // version 3: ;@var / ;@scope records (9 words each, after the string table)
    uint32_t        langoff, langflags;     // version 4: the ;@lang (a string offset or NO_STRING); flags 1: FILE.debug, 2: FILE.vars
    const unsigned char  *words, *syms, *rels, *dbg, *vars;
    const char     *str;
    uint32_t        start;          // word offset of this object in the output
}
object_t;

typedef struct
{
    const char  *name;
    uint32_t     addr;
    const char  *where;
}
gsym_t;

typedef struct
{
    uint32_t     addr;              // absolute address
    const char  *name;
    char         kind;
}
label_t;

typedef enum { DBG_PROGRAM, DBG_VBIN, DBG_V32 } dbgmode_t;

static uint32_t  get32 (const unsigned char *p)
{
    return ((uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24);
}

static void  *xrealloc (void *p, size_t n)
{
    p = realloc (p, n ? n : 1);

    if (p == NULL)
    {
        fprintf (stderr, "v32asld: out of memory\n");
        exit    (2);
    }

    return (p);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Reading and checking an object
//
// check an object held in memory and fill in o; buf stays owned by the caller unless
// o -> raw takes it (own != 0)
static int  parse_object (object_t *o, const char *path, unsigned char *buf, size_t size, int own)
{
    size_t  need;

    o -> path = path;

    if (size < 32)
    {
        fprintf (stderr, "v32asld: %s: not a v32as object (too short)\n", path);
        return (-1);
    }

    if (memcmp (buf, "V32-VOBJ", 8) != 0)
    {
        fprintf (stderr, "v32asld: %s: not a v32as object (bad magic)\n", path);
        return (-1);
    }

    if (get32 (buf + 8) < 1 || get32 (buf + 8) > 4)
    {
        fprintf (stderr, "v32asld: %s: unsupported object format version %u\n", path, get32 (buf + 8));
        return (-1);
    }

    o -> version = get32 (buf + 8);
    o -> dbgsz   = o -> version >= 2 ? 16 : 12;

    o -> raw     = own ? buf : NULL;
    o -> nwords  = get32 (buf + 12);
    o -> nsyms   = get32 (buf + 16);
    o -> nrels   = get32 (buf + 20);
    o -> ndbg    = get32 (buf + 24);
    o -> strsize = get32 (buf + 28);

    need = 32 + 4 * (size_t) o -> nwords + 12 * ((size_t) o -> nsyms + o -> nrels)
           + (size_t) o -> dbgsz * o -> ndbg + o -> strsize;

    o -> nvars = 0;
    o -> vars  = NULL;
    o -> langoff = NO_STRING;
    o -> langflags = 0;

    if (o -> version == 4)
    {
        // after the string table: the ;@lang (string offset), its flags, then as in version 3
        if (size < need + 12)
        {
            fprintf (stderr, "v32asld: %s: corrupt object (size does not match its header)\n", path);
            return (-1);
        }

        o -> langoff   = get32 (buf + need);
        o -> langflags = get32 (buf + need + 4);
        o -> nvars     = get32 (buf + need + 8);
        o -> vars      = buf + need + 12;
        need          += 12 + 36 * (size_t) o -> nvars;
    }
    else if (o -> version == 3)
    {
        // after the string table: the number of ;@var / ;@scope records, then 9 words each
        if (size < need + 4)
        {
            fprintf (stderr, "v32asld: %s: corrupt object (size does not match its header)\n", path);
            return (-1);
        }

        o -> nvars = get32 (buf + need);
        o -> vars  = buf + need + 4;
        need      += 4 + 36 * (size_t) o -> nvars;
    }

    if (need != size)
    {
        fprintf (stderr, "v32asld: %s: corrupt object (size does not match its header)\n", path);
        return (-1);
    }

    o -> words = buf + 32;
    o -> syms  = o -> words + 4 * (size_t) o -> nwords;
    o -> rels  = o -> syms + 12 * (size_t) o -> nsyms;
    o -> dbg   = o -> rels + 12 * (size_t) o -> nrels;
    o -> str   = (const char *) (o -> dbg + (size_t) o -> dbgsz * o -> ndbg);

    if (o -> strsize == 0 || o -> str[o -> strsize - 1] != 0)
    {
        fprintf (stderr, "v32asld: %s: corrupt object (string table)\n", path);
        return (-1);
    }

    if (o -> langoff != NO_STRING && o -> langoff >= o -> strsize)
    {
        fprintf (stderr, "v32asld: %s: corrupt object (language name)\n", path);
        return (-1);
    }

    return (0);
}

static unsigned char  *read_file (const char *path, size_t *size)
{
    FILE           *fp = fopen (path, "rb");
    unsigned char  *buf;
    long            n;

    if (fp == NULL)
    {
        fprintf (stderr, "v32asld: cannot read '%s': %s\n", path, strerror (errno));
        return (NULL);
    }

    fseek (fp, 0, SEEK_END);
    n = ftell (fp);
    fseek (fp, 0, SEEK_SET);

    buf = xrealloc (NULL, (size_t) n + 1);

    if (n < 0 || fread (buf, 1, (size_t) n, fp) != (size_t) n)
    {
        fprintf (stderr, "v32asld: cannot read '%s'\n", path);
        fclose (fp);
        free   (buf);
        return (NULL);
    }

    fclose (fp);
    *size = (size_t) n;

    return (buf);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Archives: a bundle of objects, of which the linker takes only the members that define
// a symbol still missing.  Layout (32-bit little endian numbers):
//
//     "V32-VARC"  version (1)  member count
//     per member: name length, name (zero padded to 4), data size, data (zero padded to 4)
//
static int  is_archive (const unsigned char *buf, size_t size)
{
    return (size >= 16 && memcmp (buf, "V32-VARC", 8) == 0);
}

static void  put32 (FILE *fp, uint32_t v)
{
    fputc ((int) (v & 0xFF), fp);
    fputc ((int) ((v >> 8) & 0xFF), fp);
    fputc ((int) ((v >> 16) & 0xFF), fp);
    fputc ((int) ((v >> 24) & 0xFF), fp);
}

static void  put_padded (FILE *fp, const void *data, size_t n)
{
    fwrite (data, 1, n, fp);

    for (; n % 4 != 0; n++)
    {
        fputc (0, fp);
    }
}

static int  make_archive (const char *out, char **files, int nfiles)
{
    FILE  *fp = fopen (out, "wb");
    int    i;

    if (fp == NULL)
    {
        fprintf (stderr, "v32asld: cannot write '%s': %s\n", out, strerror (errno));
        return (2);
    }

    fwrite ("V32-VARC", 1, 8, fp);
    put32  (fp, 1);
    put32  (fp, (uint32_t) nfiles);

    for (i = 0; i < nfiles; i++)
    {
        size_t          size;
        unsigned char  *buf = read_file (files[i], &size);
        object_t        o;
        const char     *name = strrchr (files[i], '/');

        name = name ? name + 1 : files[i];

        if (buf == NULL || parse_object (&o, files[i], buf, size, 0) != 0)
        {
            fclose (fp);
            remove (out);
            return (2);
        }

        put32      (fp, (uint32_t) strlen (name));
        put_padded (fp, name, strlen (name));
        put32      (fp, (uint32_t) size);
        put_padded (fp, buf, size);
        free       (buf);
    }

    return (fclose (fp) != 0 ? 2 : 0);
}

static osym_t  osym (const object_t *o, uint32_t i)
{
    const unsigned char  *p = o -> syms + 12 * (size_t) i;
    osym_t                s;
    uint32_t              off = get32 (p);

    s.name  = off < o -> strsize ? o -> str + off : "?";
    s.value = get32 (p + 4);
    s.flags = get32 (p + 8);

    return (s);
}

static const char  *ostr (const object_t *o, uint32_t off)
{
    return (off < o -> strsize ? o -> str + off : "?");
}

// ;@lang of the objects: the first one's, and whether FILE.debug (1) / FILE.vars (2) get "# lang X"
static const char  *objects_lang (const object_t *obj, int nobj, uint32_t *flags)
{
    const char  *lang = NULL;
    int          i;

    *flags = 0;

    for (i = 0; i < nobj; i++)
    {
        if (obj[i].langoff != NO_STRING && obj[i].langflags != 0)
        {
            lang    = lang != NULL ? lang : ostr (&obj[i], obj[i].langoff);
            *flags |= obj[i].langflags;
        }
    }

    return (lang);
}

static int  loc_number (const char *p, long long *v)
{
    int        hex = 0;
    long long  x = 0;
    size_t     n = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))  { hex = 1; p += 2; }

    for (; *p; p++, n++)
    {
        int  d;

        if (*p >= '0' && *p <= '9')                { d = *p - '0'; }
        else if (hex && *p >= 'a' && *p <= 'f')    { d = *p - 'a' + 10; }
        else if (hex && *p >= 'A' && *p <= 'F')    { d = *p - 'A' + 10; }
        else                                       { return (0); }

        x = x * (hex ? 16 : 10) + d;

        if (x > 0xFFFFFFFFll)  { return (0); }
    }

    *v = x;

    return (n > 0);
}

static int  is_register (const char *id)
{
    static const char *const  names[] = { "CR", "SR", "DR", "BP", "SP" };
    size_t                    i;

    if ((id[0] == 'R' || id[0] == 'r') && id[1] >= '0' && id[1] <= '9'
        && (id[2] == 0 || (id[2] >= '0' && id[2] <= '9' && id[3] == 0)))
    {
        return (atoi (id + 1) <= 15);
    }

    for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    {
        if (strcasecmp (id, names[i]) == 0)  { return (1); }
    }

    return (0);
}

// The loc= of a ;@var record of object o, as FILE.vars writes it: "label" or "label+N" (not a register) is the label's address as
// @0xADDRESS (a label of o first, then a global one; in the style of the START column of the -g mode); anything else, or
// an unknown label, as the object has it. 'buf' holds the result when it is not 'loc'.
static const char  *resolve_loc (const object_t *obj, int nobj, const object_t *o, const char *loc, uint32_t base, dbgmode_t dmode,
                                 char *buf, size_t n)
{
    char       id[128];
    size_t     len = 0;
    long long  add = 0;
    const char *p = loc;
    int        pass, i;

    if (!(isalpha ((unsigned char) *p) || *p == '_'))
    {
        return (loc);
    }

    while ((isalnum ((unsigned char) *p) || *p == '_') && len + 1 < sizeof (id))
    {
        id[len++] = *p++;
    }

    id[len] = 0;

    if (*p == '+' || *p == '-')
    {
        if (!loc_number (p + 1, &add))  { return (loc); }
        if (*p == '-')  { add = -add; }
    }
    else if (*p != 0)
    {
        return (loc);
    }

    if (is_register (id))
    {
        return (loc);
    }

    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < nobj; i++)
        {
            uint32_t  k;

            if ((pass == 0) != (&obj[i] == o))  { continue; }

            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t  s = osym (&obj[i], k);

                if (!(s.flags & SYM_DEFINED) || (s.flags & SYM_DATASYM) || strcmp (s.name, id) != 0)  { continue; }
                if (pass == 1 && !(s.flags & SYM_GLOBAL))  { continue; }

                {
                    uint32_t  w = obj[i].start + s.value + (uint32_t) add;

                    snprintf (buf, n, "@0x%08X", dmode == DBG_PROGRAM ? base + w
                                                                      : (dmode == DBG_V32 ? 0x8Cu : 0x0Cu) + w * 4);
                    return (buf);
                }
            }
        }
    }

    return (loc);
}

static int  gsym_cmp (const void *x, const void *y)
{
    return (strcmp (((const gsym_t *) x) -> name, ((const gsym_t *) y) -> name));
}

static int  label_cmp (const void *x, const void *y)
{
    const label_t  *p = x, *q = y;

    if (p -> addr != q -> addr)
    {
        return (p -> addr < q -> addr ? -1 : 1);
    }

    return (strcmp (p -> name, q -> name));
}

// FILE.sym (-g): kind,name,address,words,cfile,cline as 'v32as -g' writes it (F functions, R data; the objects carry no
// %define lines, so there are no V lines: v32cc's FILE.asm.sym has the variables)
typedef struct
{
    const char  *name;
    uint32_t     addr;
    char         kind;                  // 'F' function, 'R' data
}
vsym_t;

static int  vsym_cmp (const void *x, const void *y)
{
    const vsym_t  *p = x, *q = y;

    if (p -> addr != q -> addr)
    {
        return (p -> addr < q -> addr ? -1 : 1);
    }

    return (strcmp (p -> name, q -> name));
}

static int  str_cmp (const void *x, const void *y)
{
    return (strcmp (*(const char *const *) x, *(const char *const *) y));
}

static void  put_path (FILE *fp, const char *path)
{
    // like v32as -g: backslashes become '/', "//" collapses, a leading "./" goes
    const char  *p = path;
    int          prev_slash = 0;

    if (p[0] == '.' && p[1] == '/')
    {
        p += 2;
    }

    for (; *p; p++)
    {
        char  c = (*p == '\\') ? '/' : *p;

        if (c == '/' && prev_slash)
        {
            continue;
        }

        prev_slash = (c == '/');
        fputc (c, fp);
    }
}

#define  V32X_WINDOW_BASE  0x100000u
#define  V32X_MAGIC        0x58323356u

static int  debug_mode_not_program (int debug, dbgmode_t m)
{
    return (debug && m != DBG_PROGRAM);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// -R: a partial link. The objects are joined into one object that still has the relocations:
// words are concatenated, a symbol that is defined moves by the start of its object, an
// 'extern' that another object defines is turned into a reference to that definition, and
// the rest stay externs. Linking the result is the same as linking the pieces.
//
typedef struct
{
    unsigned char  *p;
    size_t          n, cap;
}
obuf_t;

static void  obuf_put (obuf_t *b, const void *data, size_t n)
{
    if (b -> n + n > b -> cap)
    {
        b -> cap = (b -> n + n) * 2 + 256;
        b -> p   = xrealloc (b -> p, b -> cap);
    }

    if (n > 0)
    {
        memcpy (b -> p + b -> n, data, n);
    }

    b -> n += n;
}

static void  obuf_u32 (obuf_t *b, uint32_t v)
{
    unsigned char  t[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16),
                            (unsigned char) (v >> 24) };

    obuf_put (b, t, 4);
}

// strings of the new string table, each stored once
typedef struct
{
    obuf_t        tab;
    const char  **s;
    uint32_t     *off;
    size_t        n, cap;
}
strtab_t;

static uint32_t  strtab_add (strtab_t *t, const char *str)
{
    size_t  i;

    for (i = 0; i < t -> n; i++)
    {
        if (strcmp (t -> s[i], str) == 0)
        {
            return (t -> off[i]);
        }
    }

    if (t -> n == t -> cap)
    {
        t -> cap = t -> cap ? t -> cap * 2 : 256;
        t -> s   = xrealloc (t -> s, t -> cap * sizeof (char *));
        t -> off = xrealloc (t -> off, t -> cap * sizeof (uint32_t));
    }

    t -> s[t -> n]   = str;
    t -> off[t -> n] = (uint32_t) t -> tab.n;
    t -> n++;
    obuf_put (&t -> tab, str, strlen (str) + 1);

    return (t -> off[t -> n - 1]);
}

typedef struct
{
    const char  *name;
    uint32_t     value;
    uint32_t     flags;
}
nsym_t;

static void  put_le32 (unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

static int  partial_link (object_t *obj, int nobj, const char *output)
{
    nsym_t     *ns = NULL;
    size_t      nns = 0, capn = 0, total = 0, i, k, m;
    uint32_t  **map = xrealloc (NULL, (size_t) nobj * sizeof (uint32_t *));
    obuf_t      body = { 0 }, out = { 0 }, tr = { 0 };
    strtab_t    st;
    uint32_t    nrels = 0, ndbg = 0;
    int         v2 = 0, pass, has_vars = 0;
    uint32_t    lflags = 0;
    const char *lang = objects_lang (obj, nobj, &lflags);
    FILE       *fp;

    memset (&st, 0, sizeof (st));

    for (i = 0; i < (size_t) nobj; i++)
    {
        map[i] = xrealloc (NULL, ((size_t) obj[i].nsyms + 1) * sizeof (uint32_t));
        v2    |= obj[i].version >= 2;
        obj[i].start = (uint32_t) total;
        total += obj[i].nwords;
    }

    // pass 0: the defined symbols (a datasym once per name); pass 1: the externs
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < (size_t) nobj; i++)
        {
            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t    s = osym (&obj[i], (uint32_t) k);
                int       defined = (s.flags & SYM_DEFINED) != 0;
                nsym_t    n;

                if (pass == 0 && !defined)  { continue; }
                if (pass == 1 && defined)   { continue; }

                n.name  = s.name;
                n.value = s.flags & SYM_DATASYM ? 0 : s.value + (defined ? obj[i].start : 0);
                n.flags = s.flags;

                if (!defined || (s.flags & SYM_DATASYM))
                {
                    // one entry per name for externs and datasyms; an extern another object defines
                    // as a global is that definition
                    for (m = 0; m < nns; m++)
                    {
                        if (strcmp (ns[m].name, s.name) != 0)  { continue; }

                        if (s.flags & SYM_DATASYM ? (ns[m].flags & SYM_DATASYM) != 0
                                                  : ((ns[m].flags & SYM_DEFINED) ? (ns[m].flags & SYM_GLOBAL) != 0
                                                                                 : 1))
                        {
                            break;
                        }
                    }

                    if (m < nns)
                    {
                        map[i][k] = (uint32_t) m;
                        continue;
                    }
                }

                if (nns == capn)
                {
                    capn = capn ? capn * 2 : 256;
                    ns   = xrealloc (ns, capn * sizeof (nsym_t));
                }

                ns[nns]  = n;
                map[i][k] = (uint32_t) nns;
                nns++;
            }
        }
    }

    for (i = 0; i < nns; i++)
    {
        v2 |= (ns[i].flags & SYM_DATASYM) != 0;
    }

    for (i = 0; i < (size_t) nobj; i++)
    {
        for (k = 0; k < obj[i].nwords; k++)
        {
            obuf_u32 (&body, get32 (obj[i].words + 4 * k));
        }
    }

    for (i = 0; i < nns; i++)
    {
        obuf_u32 (&body, strtab_add (&st, ns[i].name));
        obuf_u32 (&body, ns[i].value);
        obuf_u32 (&body, ns[i].flags);
    }

    for (i = 0; i < (size_t) nobj; i++)
    {
        for (k = 0; k < obj[i].nrels; k++)
        {
            const unsigned char  *p = obj[i].rels + 12 * k;
            orel_t                r = { get32 (p), get32 (p + 4), get32 (p + 8) };
            uint32_t              sym, word;

            if (r.word >= obj[i].nwords || (r.sym != NO_SYMBOL && r.sym >= obj[i].nsyms))
            {
                fprintf (stderr, "v32asld: %s: corrupt object (relocation out of range)\n", obj[i].path);
                return (2);
            }

            word = get32 (obj[i].words + 4 * (size_t) r.word);

            if (r.sym == NO_SYMBOL)
            {
                word = r.addend + obj[i].start;
            }
            else
            {
                sym = map[i][r.sym];

                if (ns[sym].flags & SYM_DATASYM)        { word = r.addend; }
                else if (ns[sym].flags & SYM_DEFINED)   { word = ns[sym].value + r.addend; }
            }

            // the word as assembled at base 0 (an extern's stays as the object had it)
            put_le32 (body.p + 4 * ((size_t) obj[i].start + r.word), word);
        }
    }

    // relocations, in word order (objects are in order and each object's list is)
    {
        obuf_t  rb = { 0 };

        for (i = 0; i < (size_t) nobj; i++)
        {
            for (k = 0; k < obj[i].nrels; k++)
            {
                const unsigned char  *p = obj[i].rels + 12 * k;
                uint32_t              w = get32 (p), sy = get32 (p + 4), ad = get32 (p + 8);

                obuf_u32 (&rb, obj[i].start + w);
                obuf_u32 (&rb, sy == NO_SYMBOL ? NO_SYMBOL : map[i][sy]);
                obuf_u32 (&rb, sy == NO_SYMBOL ? ad + obj[i].start : ad);
                nrels++;
            }
        }

        obuf_put (&body, rb.p, rb.n);
        free (rb.p);
    }

    for (i = 0; i < (size_t) nobj; i++)
    {
        for (k = 0; k < obj[i].ndbg; k++)
        {
            const unsigned char  *p = obj[i].dbg + (size_t) obj[i].dbgsz * k;

            obuf_u32 (&body, obj[i].start + get32 (p));
            obuf_u32 (&body, strtab_add (&st, ostr (&obj[i], get32 (p + 4))));
            obuf_u32 (&body, get32 (p + 8));

            if (v2)
            {
                uint32_t  fn = obj[i].version >= 2 ? get32 (p + 12) : NO_STRING;

                obuf_u32 (&body, (fn == NO_STRING || fn == NO_FUNC) ? fn : strtab_add (&st, ostr (&obj[i], fn)));
            }

            ndbg++;
        }
    }

    // ;@var / ;@scope records of the inputs, moved to their place in the joined program
    {
        uint32_t  nv = 0;

        for (i = 0; i < (size_t) nobj; i++)
        {
            nv += obj[i].nvars;
        }

        if (nv > 0)
        {
            obuf_u32 (&tr, nv);

            for (i = 0; i < (size_t) nobj; i++)
            {
                for (k = 0; k < obj[i].nvars; k++)
                {
                    const unsigned char  *p = obj[i].vars + 36 * k;
                    int                   f;

                    obuf_u32 (&tr, get32 (p));
                    obuf_u32 (&tr, obj[i].start + get32 (p + 4));
                    obuf_u32 (&tr, obj[i].start + get32 (p + 8));

                    for (f = 3; f < 7; f++)
                    {
                        uint32_t  so = get32 (p + 4 * f);

                        obuf_u32 (&tr, so == NO_STRING ? NO_STRING : strtab_add (&st, ostr (&obj[i], so)));
                    }

                    obuf_u32 (&tr, get32 (p + 28));
                    obuf_u32 (&tr, get32 (p + 32) == NO_STRING ? NO_STRING : strtab_add (&st, ostr (&obj[i], get32 (p + 32))));
                }
            }

            v2 = 1;
        }

        has_vars = nv > 0;
    }

    // version 4: the ;@lang in front of the records (the first object's; both flags of all)
    if (lang != NULL)
    {
        obuf_t  t4 = { 0 };

        obuf_u32 (&t4, strtab_add (&st, lang));
        obuf_u32 (&t4, lflags);

        if (tr.n == 0)
        {
            obuf_u32 (&t4, 0);
        }

        obuf_put (&t4, tr.p, tr.n);
        free (tr.p);
        tr = t4;
        v2 = 1;
    }

    obuf_put (&out, "V32-VOBJ", 8);
    obuf_u32 (&out, lang != NULL ? 4 : (has_vars ? 3 : (v2 ? 2 : 1)));
    obuf_u32 (&out, (uint32_t) total);
    obuf_u32 (&out, (uint32_t) nns);
    obuf_u32 (&out, nrels);
    obuf_u32 (&out, ndbg);
    obuf_u32 (&out, (uint32_t) st.tab.n);
    obuf_put (&out, body.p, body.n);
    obuf_put (&out, st.tab.p, st.tab.n);
    obuf_put (&out, tr.p, tr.n);

    fp = fopen (output, "wb");

    if (fp == NULL || fwrite (out.p, 1, out.n, fp) != out.n || fclose (fp) != 0)
    {
        fprintf (stderr, "v32asld: cannot write '%s': %s\n", output, strerror (errno));
        return (2);
    }

    return (0);
}

static void  usage (FILE *fp)
{
    fprintf (fp,
        "usage: v32asld [OPTION...] OBJECT...\n"
        "Link relocatable objects (made by 'v32as -C') into a VBIN file.\n"
        "\n"
        "  -o, --output=FILE    output file (default: a.vbin)\n"
        "  -b, --bios           link for the BIOS (addresses start at 0x10000000)\n"
        "  -c, --cart           link for a cartridge (0x20000000; this is the default)\n"
        "  -O, --origin ADDR    link for an arbitrary base address\n"
        "  -g, --debug[=MODE]   also write FILE.debug (address,file,line[,label]); MODE is\n"
        "                       program (default), vbin or v32, as in 'v32as -g'\n"
        "      --symbols=FILE   write 'address,name,kind' for every label (kind F: code, D: data)\n"
        "      --v32x           write a V32X executable (v32nix, inc/exec.h) instead of a VBIN: an 8-word\n"
        "                       header, then the image. Needs --data; the base defaults to 0x100000 (the\n"
        "                       window base) and is the header's origin. --entry then is a global label or a\n"
        "                       word offset into the image (default 0), recorded in the header, and no\n"
        "                       jump is added\n"
        "      --data=WORDS     with --v32x: zeroed global-variable words behind the image\n"
        "      --entry=SYMBOL   the program starts at SYMBOL: when it is not the first word, a\n"
        "                       two-word 'jmp SYMBOL' is placed in front of the first object\n"
        "  -L DIR, -l NAME      search DIR for NAME.v32a, then libNAME.a (archives given by name or as operands are\n"
        "                       scanned after the objects; a member is linked in only when it defines\n"
        "                       a symbol that is still undefined)\n"
        "  -A, --archive=FILE   make an archive FILE of the objects given, instead of linking\n"
        "  -r, --relocs=FILE    write 'index,value[,kind]' for every word that holds an address (also\n"
        "      --emit-relocs=FILE  spelled --emit-relocs); with a datasym in the objects the kind is C (code\n"
        "                       address) or D (data address), as in 'v32as -r'\n"
        "  -R, --relocatable    do not link: join the objects into one object (-o FILE, default a.v32ob)\n"
        "                       that keeps the relocations and the externs still undefined\n"
        "  -w, --no-warnings    do not warn about an extern that no code uses\n"
        "  -h, --help           display this usage information and exit\n"
        "  -V, --version        display the version and exit\n"
        "\n"
        "Objects are placed in command line order; the first word of the first object is\n"
        "the entry point.\n");
}

int  main (int argc, char **argv)
{
    static const struct option  long_options[] =
    {
        { "bios",        no_argument,       0, 'b' },
        { "cart",        no_argument,       0, 'c' },
        { "origin",      required_argument, 0, 'O' },
        { "output",      required_argument, 0, 'o' },
        { "debug",       optional_argument, 0, 'g' },
        { "relocs",      required_argument, 0, 'r' },
        { "symbols",     required_argument, 0, 1003 },
        { "entry",       required_argument, 0, 1004 },
        { "archive",     required_argument, 0, 'A' },
        { "v32x",        no_argument,       0, 1005 },
        { "data",        required_argument, 0, 1006 },
        { "emit-relocs", required_argument, 0, 'r' },
        { "relocatable", no_argument,       0, 'R' },
        { "no-warnings", no_argument,       0, 'w' },
        { "help",        no_argument,       0, 'h' },
        { "version",     no_argument,       0, 'V' },
        { 0, 0, 0, 0 }
    };

    int          bios = 0, cart = 0, origin_set = 0, debug = 0, no_warn = 0;
    uint32_t     origin = 0, base;
    dbgmode_t    dmode = DBG_PROGRAM;
    const char  *output = "a.vbin", *relocs = NULL, *symbols = NULL, *entry = NULL;
    const char  *archive_out = NULL;
    const char **libdirs = NULL, **libs = NULL;
    int          nlibdirs = 0, nlibs = 0;
    object_t    *obj = NULL, *pool = NULL;
    char        *psel = NULL;
    int          nobj = 0, npool = 0, c, i, errors = 0, rc = 0;
    uint32_t     lead = 0;
    int          v32x = 0, data_set = 0;
    uint32_t     data_words = 0, entry_off = 0;
    gsym_t      *g = NULL;
    size_t       ng = 0, capg = 0, total = 0, k, j;
    uint32_t    *words = NULL;
    const char **reported = NULL;
    size_t       nrep = 0;
    FILE        *fp;
    char        *wkind = NULL;
    int          has_datasym = 0, partial = 0;

    while ((c = getopt_long (argc, argv, "bcO:o:g::r:RwhVL:l:A:", long_options, NULL)) != -1)
    {
        switch (c)
        {
            case 'b':  bios = 1;                  break;
            case 'c':  cart = 1;                  break;
            case 'w':  no_warn = 1;               break;
            case 'R':  partial = 1;               break;
            case 'o':  output = optarg;           break;
            case 'r':  relocs = optarg;           break;
            case 1003: symbols = optarg;          break;
            case 1004: entry = optarg;            break;
            case 'A':  archive_out = optarg;      break;
            case 1005: v32x = 1;                  break;
            case 1006:
            {
                char           *end;
                unsigned long   v = strtoul (optarg, &end, 0);

                if (*optarg == '\0' || *end != '\0' || v > 0x7FFFFFFFul)
                {
                    fprintf (stderr, "v32asld: invalid --data '%s' (a number of words)\n", optarg);
                    return (2);
                }

                data_set = 1;
                data_words = (uint32_t) v;
                break;
            }
            case 'L':
                libdirs = xrealloc (libdirs, (size_t) (nlibdirs + 1) * sizeof (char *));
                libdirs[nlibdirs++] = optarg;
                break;
            case 'l':
                libs = xrealloc (libs, (size_t) (nlibs + 1) * sizeof (char *));
                libs[nlibs++] = optarg;
                break;
            case 'h':  usage (stdout);            return (0);
            case 'V':  printf ("v32asld %s\n", VERSION);  return (0);
            case 'O':
            {
                char           *end;
                unsigned long   v = strtoul (optarg, &end, 0);

                if (*optarg == '\0' || *end != '\0' || v > 0xFFFFFFFFul)
                {
                    fprintf (stderr, "v32asld: invalid --origin '%s'\n", optarg);
                    return (2);
                }

                origin_set = 1;
                origin     = (uint32_t) v;
                break;
            }
            case 'g':
                debug = 1;

                if (optarg == NULL && optind < argc
                    && (strcmp (argv[optind], "program") == 0 || strcmp (argv[optind], "vbin") == 0
                        || strcmp (argv[optind], "v32") == 0))
                {
                    optarg = argv[optind++];
                }

                if (optarg != NULL && strcmp (optarg, "vbin") == 0)         { dmode = DBG_VBIN; }
                else if (optarg != NULL && strcmp (optarg, "v32") == 0)     { dmode = DBG_V32; }
                else if (optarg != NULL && strcmp (optarg, "program") != 0)
                {
                    fprintf (stderr, "v32asld: unrecognized reference mode '%s' for -g "
                                     "(use program, vbin or v32)\n", optarg);
                    return (2);
                }
                break;
            default:
                usage (stderr);
                return (2);
        }
    }

    if (partial && strcmp (output, "a.vbin") == 0)
    {
        output = "a.v32ob";
    }

    if (archive_out != NULL)
    {
        return (make_archive (archive_out, argv + optind, argc - optind));
    }

    if (optind >= argc)
    {
        usage (stderr);
        return (2);
    }

    if ((bios && cart) || (origin_set && (bios || cart)))
    {
        fprintf (stderr, "v32asld: --bios, --cart and --origin are mutually exclusive\n");
        return (2);
    }

    if (v32x && !data_set)
    {
        fprintf (stderr, "v32asld: --v32x needs --data WORDS (the zeroed variable words behind the image)\n");
        return (2);
    }

    if (!v32x && data_set)
    {
        fprintf (stderr, "v32asld: --data only makes sense with --v32x\n");
        return (2);
    }

    if (v32x && (bios || cart || debug_mode_not_program (debug, dmode)))
    {
        fprintf (stderr, "v32asld: --v32x cannot be combined with -b, -c or -g vbin/v32 "
                         "(the base is -O ADDR, default 0x%X; addresses in -g are the program's)\n", V32X_WINDOW_BASE);
        return (2);
    }

    if (partial && (bios || cart || origin_set || debug || relocs || symbols || entry || v32x))
    {
        fprintf (stderr, "v32asld: --relocatable only joins objects: it cannot be combined with -b, -c, -O, -g, -r,\n"
                         "       --symbols, --entry or --v32x (do those when linking the result)\n");
        return (2);
    }

    base = origin_set ? origin : (bios ? BIOS_BASE : (v32x ? V32X_WINDOW_BASE : CART_BASE));
    // objects in command line order; archives (named, or found with -l) are kept apart
    // and mined afterwards
    for (i = optind; i < argc + nlibs; i++)
    {
        char            *path = (char *) (i < argc ? argv[i] : NULL);
        size_t           size;
        unsigned char   *buf;

        if (path == NULL)
        {
            const char  *name = libs[i - argc];
            int          d;

            for (d = 0; d < nlibdirs + 1 && path == NULL; d++)
            {
                const char  *dir = d < nlibdirs ? libdirs[d] : ".";
                char        *cand = xrealloc (NULL, strlen (dir) + strlen (name) + 16);

                sprintf (cand, "%s/%s.v32a", dir, name);

                if (access (cand, R_OK) != 0)
                {
                    sprintf (cand, "%s/lib%s.a", dir, name);
                }

                if (access (cand, R_OK) == 0)  { path = cand; }
                else                           { free (cand); }
            }

            if (path == NULL)
            {
                fprintf (stderr, "v32asld: cannot find -l%s (looked for %s.v32a and lib%s.a in the -L directories and .)\n",
                         name, name, name);
                return (2);
            }
        }

        buf = read_file (path, &size);

        if (buf == NULL)
        {
            return (2);
        }

        if (is_archive (buf, size))
        {
            uint32_t  count = get32 (buf + 12), m;
            size_t    at = 16;

            if (get32 (buf + 8) != 1)
            {
                fprintf (stderr, "v32asld: %s: unsupported archive format version %u\n", path, get32 (buf + 8));
                return (2);
            }

            for (m = 0; m < count; m++)
            {
                uint32_t  nl, ds;
                char     *label;
                size_t    nlp;

                if (at + 4 > size || (nl = get32 (buf + at), 0) || at + 4 + ((size_t) nl + 3) / 4 * 4 + 4 > size)
                {
                    fprintf (stderr, "v32asld: %s: corrupt archive\n", path);
                    return (2);
                }

                nlp = ((size_t) nl + 3) / 4 * 4;
                ds  = get32 (buf + at + 4 + nlp);

                if (at + 8 + nlp + ds > size)
                {
                    fprintf (stderr, "v32asld: %s: corrupt archive\n", path);
                    return (2);
                }

                label = xrealloc (NULL, strlen (path) + nl + 4);
                sprintf (label, "%s(%.*s)", path, (int) nl, (const char *) buf + at + 4);

                pool = xrealloc (pool, (size_t) (npool + 1) * sizeof (object_t));
                memset (&pool[npool], 0, sizeof (object_t));

                if (parse_object (&pool[npool], label, buf + at + 8 + nlp, ds, 0) != 0)
                {
                    return (2);
                }

                npool++;
                at += 8 + nlp + (((size_t) ds + 3) / 4 * 4);
            }
        }
        else
        {
            obj = xrealloc (obj, (size_t) (nobj + 1) * sizeof (object_t));
            memset (&obj[nobj], 0, sizeof (object_t));

            if (parse_object (&obj[nobj], path, buf, size, 1) != 0)
            {
                return (2);
            }

            nobj++;
        }
    }

    if (nobj == 0)
    {
        fprintf (stderr, "v32asld: no object to link\n");
        return (2);
    }

    // pull in archive members that define something still undefined, until nothing changes
    psel = xrealloc (NULL, (size_t) npool + 1);
    memset (psel, 0, (size_t) npool + 1);

    for (;;)
    {
        int  grew = 0, m;

        for (m = 0; m < npool; m++)
        {
            uint32_t  ms;
            int       need = 0;

            if (psel[m])
            {
                continue;
            }

            for (ms = 0; ms < pool[m].nsyms && !need; ms++)
            {
                osym_t  d = osym (&pool[m], ms);
                int     o2;
                uint32_t k2;

                if (!(d.flags & SYM_GLOBAL) || !(d.flags & SYM_DEFINED))
                {
                    continue;
                }

                // does a selected object need d?
                for (o2 = 0; o2 < nobj && !need; o2++)
                {
                    for (k2 = 0; k2 < obj[o2].nsyms && !need; k2++)
                    {
                        osym_t  u = osym (&obj[o2], k2);

                        need = !(u.flags & SYM_DEFINED) && strcmp (u.name, d.name) == 0;
                    }
                }
            }

            if (need)
            {
                psel[m] = 1;
                obj     = xrealloc (obj, (size_t) (nobj + 1) * sizeof (object_t));
                obj[nobj++] = pool[m];
                grew    = 1;
            }
        }

        if (!grew)
        {
            break;
        }
    }

    // --entry: the program starts at the first word; anything else gets a jump in front
    // (--v32x has an entry field in its header instead: handled once the symbols have addresses)
    if (entry != NULL && !v32x)
    {
        int      found = 0, o2;
        uint32_t k2;
        uint32_t off = 0;

        for (o2 = 0; o2 < nobj && !found; o2++)
        {
            for (k2 = 0; k2 < obj[o2].nsyms && !found; k2++)
            {
                osym_t  d = osym (&obj[o2], k2);

                if ((d.flags & SYM_GLOBAL) && (d.flags & SYM_DEFINED) && strcmp (d.name, entry) == 0)
                {
                    found = 1;
                    off   = d.value + (o2 == 0 ? 0 : 1);       // nonzero unless object 0, word 0
                }
            }
        }

        if (!found)
        {
            fprintf (stderr, "v32asld: error: entry symbol '%s' is not defined by any object\n", entry);
            return (1);
        }

        lead = (off == 0) ? 0 : 2;
    }

    total = lead;

    for (i = 0; i < nobj; i++)
    {
        obj[i].start = (uint32_t) total;
        total       += obj[i].nwords;
    }

    // the table of global symbols
    for (i = 0; i < nobj; i++)
    {
        for (k = 0; k < obj[i].nsyms; k++)
        {
            osym_t  s = osym (&obj[i], (uint32_t) k);

            if (!(s.flags & SYM_GLOBAL) || !(s.flags & SYM_DEFINED))
            {
                continue;
            }

            if (ng == capg)
            {
                capg = capg ? capg * 2 : 64;
                g    = xrealloc (g, capg * sizeof (gsym_t));
            }

            g[ng].name  = s.name;
            g[ng].addr  = base + obj[i].start + s.value;
            g[ng].where = obj[i].path;
            ng++;
        }
    }

    if (ng > 1)
    {
        qsort (g, ng, sizeof (gsym_t), gsym_cmp);
    }

    for (k = 1; k < ng; k++)
    {
        if (strcmp (g[k].name, g[k - 1].name) == 0)
        {
            fprintf (stderr, "v32asld: error: '%s' is defined in both %s and %s\n",
                     g[k].name, g[k - 1].where, g[k].where);
            errors++;
        }
    }

    if (partial)
    {
        return (errors > 0 ? 1 : partial_link (obj, nobj, output));
    }

    if (v32x)
    {
        if (entry != NULL)
        {
            char           *end;
            unsigned long   v = strtoul (entry, &end, 0);

            if (*entry != '\0' && *end == '\0')
            {
                entry_off = (uint32_t) v;           // a word offset
            }
            else
            {
                gsym_t   key = { entry, 0, NULL };
                gsym_t  *hit = bsearch (&key, g, ng, sizeof (gsym_t), gsym_cmp);

                if (hit == NULL)
                {
                    fprintf (stderr, "v32asld: error: entry symbol '%s' is not defined by any object\n", entry);
                    return (1);
                }

                entry_off = hit -> addr - base;
            }
        }

        if (entry_off >= total)
        {
            fprintf (stderr, "v32asld: error: the entry point (word %u) is outside the image (%zu words)\n",
                     entry_off, total);
            return (1);
        }
    }

    // the image: the objects' words, then every relocation applied
    words = xrealloc (NULL, total * sizeof (uint32_t));
    wkind = xrealloc (NULL, total + 1);

    memset (words, 0, total * sizeof (uint32_t));
    memset (wkind, 'C', total + 1);

    for (i = 0; i < nobj; i++)
    {
        for (k = 0; k < obj[i].nsyms; k++)
        {
            has_datasym |= (osym (&obj[i], (uint32_t) k).flags & SYM_DATASYM) != 0;
        }
    }

    for (i = 0; i < nobj; i++)
    {
        for (k = 0; k < obj[i].nwords; k++)
        {
            words[obj[i].start + k] = get32 (obj[i].words + 4 * k);
        }
    }

    if (lead > 0)
    {
        gsym_t   key = { entry, 0, NULL };
        gsym_t  *hit = bsearch (&key, g, ng, sizeof (gsym_t), gsym_cmp);

        words[0] = 0x0A000000u;                 // jmp IMMEDIATE
        words[1] = hit != NULL ? hit -> addr : 0;
    }

    for (i = 0; i < nobj; i++)
    {
        for (k = 0; k < obj[i].nrels; k++)
        {
            const unsigned char  *p = obj[i].rels + 12 * k;
            orel_t                r = { get32 (p), get32 (p + 4), get32 (p + 8) };
            uint32_t              value;

            if (r.word >= obj[i].nwords || (r.sym != NO_SYMBOL && r.sym >= obj[i].nsyms))
            {
                fprintf (stderr, "v32asld: %s: corrupt object (relocation out of range)\n", obj[i].path);
                return (2);
            }

            if (r.sym == NO_SYMBOL)
            {
                value = base + obj[i].start + r.addend;     // relative to the object itself
            }
            else
            {
                osym_t  s = osym (&obj[i], r.sym);

                if (s.flags & SYM_DATASYM)
                {
                    value = r.addend;                           // the base of the data: not moved by the link
                    wkind[obj[i].start + r.word] = 'D';
                }
                else if (s.flags & SYM_DEFINED)
                {
                    value = base + obj[i].start + s.value + r.addend;
                }
                else
                {
                    gsym_t   key = { s.name, 0, NULL };
                    gsym_t  *hit = bsearch (&key, g, ng, sizeof (gsym_t), gsym_cmp);

                    if (hit == NULL)
                    {
                        // once per symbol, naming the first object that used it
                        size_t  q;

                        for (q = 0; q < nrep && strcmp (reported[q], s.name) != 0; q++)
                        {
                        }

                        if (q == nrep)
                        {
                            reported = xrealloc (reported, (nrep + 1) * sizeof (char *));
                            reported[nrep++] = s.name;
                            fprintf (stderr, "v32asld: error: undefined reference to `%s' (first used in %s)\n",
                                     s.name, obj[i].path);
                            errors++;
                        }

                        continue;
                    }

                    value = hit -> addr + r.addend;
                }
            }

            words[obj[i].start + r.word] = value;
        }
    }

    if (errors > 0)
    {
        return (1);
    }

    if (!no_warn)
    {
        // an 'extern' that no word refers to is worth a note
        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t  s = osym (&obj[i], (uint32_t) k);
                int     used = 0;

                if (!(s.flags & SYM_EXTERN))
                {
                    continue;
                }

                for (j = 0; j < obj[i].nrels && !used; j++)
                {
                    used = (get32 (obj[i].rels + 12 * j + 4) == k);
                }

                if (!used)
                {
                    fprintf (stderr, "v32asld: warning: %s: extern '%s' is never used\n",
                             obj[i].path, s.name);
                }
            }
        }
    }

    // output: the VBIN
    fp = fopen (output, "wb");

    if (fp == NULL)
    {
        fprintf (stderr, "v32asld: cannot write '%s': %s\n", output, strerror (errno));
        return (2);
    }

    if (v32x)
    {
        // magic, version, header words, text words, data words, entry offset, origin, flags
        uint32_t  hdr[8] = { V32X_MAGIC, 1, 8, (uint32_t) total, data_words, entry_off, base, 0 };

        for (k = 0; k < 8; k++)
        {
            for (j = 0; j < 4; j++)
            {
                fputc ((int) ((hdr[k] >> (8 * j)) & 0xFF), fp);
            }
        }
    }
    else
    {
        fwrite ("V32-VBIN", 1, 8, fp);

        for (k = 0; k < 4; k++)
        {
            fputc ((int) (((uint32_t) total >> (8 * k)) & 0xFF), fp);
        }
    }

    for (k = 0; k < total; k++)
    {
        for (j = 0; j < 4; j++)
        {
            fputc ((int) ((words[k] >> (8 * j)) & 0xFF), fp);
        }
    }

    if (fclose (fp) != 0)
    {
        fprintf (stderr, "v32asld: error writing '%s'\n", output);
        return (2);
    }

    if (relocs != NULL)
    {
        fp = fopen (relocs, "w");

        if (fp == NULL)
        {
            fprintf (stderr, "v32asld: cannot write '%s': %s\n", relocs, strerror (errno));
            return (2);
        }

        if (lead > 0)
        {
            fprintf (fp, has_datasym ? "1,0x%08X,C\n" : "1,0x%08X\n", words[1]);
        }

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nrels; k++)
            {
                uint32_t  w = get32 (obj[i].rels + 12 * k);

                if (has_datasym)
                {
                    fprintf (fp, "%u,0x%08X,%c\n", obj[i].start + w, words[obj[i].start + w], wkind[obj[i].start + w]);
                }
                else
                {
                    fprintf (fp, "%u,0x%08X\n", obj[i].start + w, words[obj[i].start + w]);
                }
            }
        }

        fclose (fp);
    }

    if (symbols != NULL)
    {
        label_t  *lab = NULL;
        size_t    nl = 0, capl = 0;

        fp = fopen (symbols, "w");

        if (fp == NULL)
        {
            fprintf (stderr, "v32asld: cannot write '%s': %s\n", symbols, strerror (errno));
            return (2);
        }

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t  s = osym (&obj[i], (uint32_t) k);

                if (!(s.flags & SYM_DEFINED) || (s.flags & SYM_DATASYM))
                {
                    continue;
                }

                if (nl == capl)
                {
                    capl = capl ? capl * 2 : 256;
                    lab  = xrealloc (lab, capl * sizeof (label_t));
                }

                lab[nl].addr = base + obj[i].start + s.value;
                lab[nl].name = s.name;
                lab[nl].kind = (s.flags & SYM_CODE) ? 'F' : 'D';
                nl++;
            }
        }

        if (nl > 1)
        {
            qsort (lab, nl, sizeof (label_t), label_cmp);
        }

        for (k = 0; k < nl; k++)
        {
            fprintf (fp, "0x%08X,%s,%c\n", lab[k].addr, lab[k].name, lab[k].kind);
        }

        fclose (fp);
        free   (lab);
    }

    if (debug)
    {
        label_t  *lab = NULL;
        size_t    nl = 0, capl = 0;
        char     *path = xrealloc (NULL, strlen (output) + 8);
        uint32_t  base_off = (dmode == DBG_V32) ? 0x8C : (dmode == DBG_VBIN ? 0x0C : 0);
        uint32_t  unit     = (dmode == DBG_PROGRAM) ? 1 : 4;
        uint32_t  lflags = 0;
        const char  *lang;

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t  s = osym (&obj[i], (uint32_t) k);

                if (!(s.flags & SYM_DEFINED) || (s.flags & SYM_DATASYM))
                {
                    continue;
                }

                if (nl == capl)
                {
                    capl = capl ? capl * 2 : 256;
                    lab  = xrealloc (lab, capl * sizeof (label_t));
                }

                lab[nl].addr = base + obj[i].start + s.value;
                lab[nl].name = s.name;
                nl++;
            }
        }

        if (nl > 1)
        {
            qsort (lab, nl, sizeof (label_t), label_cmp);
        }

        sprintf (path, "%s.debug", output);
        fp = fopen (path, "w");

        if (fp == NULL)
        {
            fprintf (stderr, "v32asld: cannot write '%s': %s\n", path, strerror (errno));
            return (2);
        }

        lang = objects_lang (obj, (int) nobj, &lflags);

        if (lang != NULL && (lflags & 1))
        {
            fprintf (fp, "# lang %s\n", lang);
        }

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].ndbg; k++)
            {
                const unsigned char  *p = obj[i].dbg + (size_t) obj[i].dbgsz * k;
                uint32_t              fn = obj[i].version >= 2 ? get32 (p + 12) : NO_STRING;
                uint32_t              word = obj[i].start + get32 (p);
                uint32_t              addr = base + word;
                size_t                lo = 0, hi = nl;

                while (lo < hi)
                {
                    size_t  mid = (lo + hi) / 2;

                    if (lab[mid].addr < addr)  { lo = mid + 1; }
                    else                       { hi = mid; }
                }

                fprintf (fp, "0x%08X,", dmode == DBG_PROGRAM ? addr : base_off + word * unit);
                put_path (fp, ostr (&obj[i], get32 (p + 4)));
                fprintf (fp, ",%u", get32 (p + 8));

                if (fn == NO_FUNC)
                {
                    // ;@src entry with no function: nothing more on the line
                }
                else if (fn != NO_STRING)
                {
                    fprintf (fp, ",%s", ostr (&obj[i], fn));            // the function, from --map or ;@src
                }
                else if (lo < nl && lab[lo].addr == addr)
                {
                    fprintf (fp, ",%s", lab[lo].name);
                }

                fputc ('\n', fp);
            }
        }

        fclose (fp);

        // FILE.vars: the ;@var / ;@scope ranges the objects carry (same lines as 'v32as -g' writes)
        {
            size_t  nv = 0;

            for (i = 0; i < nobj; i++)
            {
                nv += obj[i].nvars;
            }

            sprintf (path, "%s.vars", output);

            if (nv == 0)
            {
                unlink (path);                  // an old file would mislead
            }
            else if ((fp = fopen (path, "w")) == NULL)
            {
                fprintf (stderr, "v32asld: cannot write '%s': %s\n", path, strerror (errno));
                return (2);
            }
            else
            {
                if (lang != NULL && (lflags & 2))
                {
                    fprintf (fp, "# lang %s\n", lang);
                }

                for (i = 0; i < nobj; i++)
                {
                    for (k = 0; k < obj[i].nvars; k++)
                    {
                        const unsigned char  *p = obj[i].vars + 36 * k;
                        uint32_t              ws = obj[i].start + get32 (p + 4), we = obj[i].start + get32 (p + 8);
                        uint32_t              fno = get32 (p + 32);
                        const char           *tp = get32 (p + 20) == NO_STRING ? "-" : ostr (&obj[i], get32 (p + 20));
                        const char           *fl = get32 (p + 24) == NO_STRING ? "-" : ostr (&obj[i], get32 (p + 24));
                        char                  lbuf[160];
                        const char           *lc = get32 (p + 16) == NO_STRING ? "-" : ostr (&obj[i], get32 (p + 16));

                        if (get32 (p) == 0)
                        {
                            fprintf (fp, "scope,0x%08X,0x%08X,%u", dmode == DBG_PROGRAM ? base + ws : base_off + ws * unit,
                                     dmode == DBG_PROGRAM ? base + we : base_off + we * unit, get32 (p + 28));
                        }
                        else
                        {
                            fprintf (fp, "var,0x%08X,0x%08X,%s,%s,%s,%s,%u", dmode == DBG_PROGRAM ? base + ws : base_off + ws * unit,
                                     dmode == DBG_PROGRAM ? base + we : base_off + we * unit, ostr (&obj[i], get32 (p + 12)),
                                     resolve_loc (obj, (int) nobj, &obj[i], lc, base, dmode, lbuf, sizeof (lbuf)), tp, fl, get32 (p + 28));
                        }

                        if (fno != NO_STRING)
                        {
                            fprintf (fp, ",%s", ostr (&obj[i], fno));
                        }

                        fputc ('\n', fp);
                    }
                }

                fclose (fp);
            }
        }

        // FILE.sym: the functions and data with final addresses and sizes (as 'v32as -g' writes it)
        {
            vsym_t       *vs = NULL;
            size_t        nvs = 0, capv = 0, nfn = 0, capf = 0, f;
            const char  **fnames = NULL;
            uint32_t      end = base + (uint32_t) total;

            // the function names the debug records carry (v32cc: NAME, label __function_NAME)
            for (i = 0; i < nobj; i++)
            {
                for (k = 0; k < obj[i].ndbg; k++)
                {
                    const unsigned char  *p = obj[i].dbg + (size_t) obj[i].dbgsz * k;
                    uint32_t              fn = obj[i].version >= 2 ? get32 (p + 12) : NO_STRING;

                    if (fn == NO_STRING || fn == NO_FUNC)
                    {
                        continue;
                    }

                    if (nfn == capf)
                    {
                        capf   = capf ? capf * 2 : 64;
                        fnames = xrealloc (fnames, capf * sizeof (char *));
                    }

                    fnames[nfn++] = ostr (&obj[i], fn);
                }
            }

            if (nfn > 1)
            {
                qsort (fnames, nfn, sizeof (char *), str_cmp);
            }

            for (i = 0; i < nobj; i++)
            {
                for (k = 0; k < obj[i].nsyms; k++)
                {
                    osym_t       s = osym (&obj[i], (uint32_t) k);
                    const char  *nm;
                    int          isfn = 0;

                    if (!(s.flags & SYM_DEFINED) || (s.flags & SYM_DATASYM))
                    {
                        continue;
                    }

                    nm = s.name;

                    if (s.flags & SYM_CODE)
                    {
                        const char  *key = strncmp (nm, "__function_", 11) == 0 ? nm + 11 : nm;

                        // a code label is a function when a debug record names it; with no function names at all, every one is
                        isfn = nfn == 0 || bsearch (&key, fnames, nfn, sizeof (char *), str_cmp) != NULL;

                        if (!isfn)
                        {
                            continue;       // a loop label inside a function
                        }

                        nm = key;
                    }

                    if (nvs == capv)
                    {
                        capv = capv ? capv * 2 : 256;
                        vs   = xrealloc (vs, capv * sizeof (vsym_t));
                    }

                    vs[nvs].name = nm;
                    vs[nvs].addr = base + obj[i].start + s.value;
                    vs[nvs].kind = isfn ? 'F' : 'R';
                    nvs++;
                }
            }

            if (nvs > 1)
            {
                qsort (vs, nvs, sizeof (vsym_t), vsym_cmp);
            }

            sprintf (path, "%s.sym", output);
            fp = fopen (path, "w");

            if (fp == NULL)
            {
                fprintf (stderr, "v32asld: cannot write '%s': %s\n", path, strerror (errno));
                return (2);
            }

            if (lang != NULL && (lflags & 1))
            {
                fprintf (fp, "# lang %s\n", lang);
            }

            fputs ("# v32as symbols v1: kind,name,address,words,cfile,cline\n", fp);

            for (f = 0; f < nvs; f++)
            {
                uint32_t     next = f + 1 < nvs ? vs[f + 1].addr : end;
                const char  *file = NULL;
                uint32_t     line = 0;

                if (vs[f].kind == 'F')
                {
                    // the first source position inside the function
                    uint32_t  bestw = 0xFFFFFFFFu;

                    for (i = 0; i < nobj; i++)
                    {
                        for (k = 0; k < obj[i].ndbg; k++)
                        {
                            const unsigned char  *p = obj[i].dbg + (size_t) obj[i].dbgsz * k;
                            uint32_t              a = base + obj[i].start + get32 (p);

                            if (a >= vs[f].addr && a < next && a < bestw && get32 (p + 8) != 0)
                            {
                                bestw = a;
                                file  = ostr (&obj[i], get32 (p + 4));
                                line  = get32 (p + 8);
                            }
                        }
                    }
                }

                fprintf (fp, "%c,%s,0x%08X,%u,", vs[f].kind, vs[f].name, vs[f].addr, (unsigned) (next > vs[f].addr ? next - vs[f].addr : 0));

                if (file != NULL)
                {
                    put_path (fp, file);
                    fprintf (fp, ",%u", line);
                }
                else
                {
                    fputc (',', fp);
                }

                fputc ('\n', fp);
            }

            fclose (fp);
            free (vs);
            free (fnames);
        }

        free   (path);
        free   (lab);
    }

    for (i = 0; i < nobj; i++)
    {
        free (obj[i].raw);
    }

    free (obj);
    free (g);
    free (reported);
    free (words);

    return (rc);
}
