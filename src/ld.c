//
// ld.c: v32ld, the linker for the relocatable objects (.v32ob) written by "v32as -C"
//
//     v32ld [-b|-c|-O ADDR] [-o FILE] [-g[MODE]] [-r FILE] [-w] OBJECT...
//
// Objects are placed one after the other, in command line order (the first word of the
// first object is the program's entry point). Every word the objects mark as an address
// is rewritten with the final address of its symbol; symbols are the labels of the
// objects, and 'global' ones are shared between objects. The output is a VBIN, exactly
// what "v32as" writes. The format of the objects is described in docs/OBJECT.md.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define  VERSION      "20261024-dev"
#define  BIOS_BASE    0x10000000u
#define  CART_BASE    0x20000000u

#define  SYM_DEFINED  1u
#define  SYM_GLOBAL   2u
#define  SYM_EXTERN   4u
#define  NO_SYMBOL    0xFFFFFFFFu

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
    uint32_t        nwords, nsyms, nrels, ndbg, strsize;
    const unsigned char  *words, *syms, *rels, *dbg;
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
        fprintf (stderr, "v32ld: out of memory\n");
        exit    (2);
    }

    return (p);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Reading and checking an object
//
static int  load_object (object_t *o, const char *path)
{
    FILE           *fp = fopen (path, "rb");
    unsigned char  *buf;
    long            size;
    size_t          need;

    o -> path = path;

    if (fp == NULL)
    {
        fprintf (stderr, "v32ld: cannot read '%s': %s\n", path, strerror (errno));
        return (-1);
    }

    fseek (fp, 0, SEEK_END);
    size = ftell (fp);
    fseek (fp, 0, SEEK_SET);

    buf = xrealloc (NULL, (size_t) size + 1);

    if (size < 32 || fread (buf, 1, (size_t) size, fp) != (size_t) size)
    {
        fprintf (stderr, "v32ld: %s: not a v32as object (too short)\n", path);
        fclose (fp);
        free   (buf);
        return (-1);
    }

    fclose (fp);

    if (memcmp (buf, "V32-VOBJ", 8) != 0)
    {
        fprintf (stderr, "v32ld: %s: not a v32as object (bad magic)\n", path);
        free (buf);
        return (-1);
    }

    if (get32 (buf + 8) != 1)
    {
        fprintf (stderr, "v32ld: %s: unsupported object format version %u\n", path, get32 (buf + 8));
        free (buf);
        return (-1);
    }

    o -> raw     = buf;
    o -> nwords  = get32 (buf + 12);
    o -> nsyms   = get32 (buf + 16);
    o -> nrels   = get32 (buf + 20);
    o -> ndbg    = get32 (buf + 24);
    o -> strsize = get32 (buf + 28);

    need = 32 + 4 * (size_t) o -> nwords + 12 * ((size_t) o -> nsyms + o -> nrels + o -> ndbg)
           + o -> strsize;

    if (need != (size_t) size)
    {
        fprintf (stderr, "v32ld: %s: corrupt object (size does not match its header)\n", path);
        free (buf);
        return (-1);
    }

    o -> words = buf + 32;
    o -> syms  = o -> words + 4 * (size_t) o -> nwords;
    o -> rels  = o -> syms + 12 * (size_t) o -> nsyms;
    o -> dbg   = o -> rels + 12 * (size_t) o -> nrels;
    o -> str   = (const char *) (o -> dbg + 12 * (size_t) o -> ndbg);

    if (o -> strsize == 0 || o -> str[o -> strsize - 1] != 0)
    {
        fprintf (stderr, "v32ld: %s: corrupt object (string table)\n", path);
        free (buf);
        return (-1);
    }

    return (0);
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

static void  usage (FILE *fp)
{
    fprintf (fp,
        "usage: v32ld [OPTION...] OBJECT...\n"
        "Link relocatable objects (made by 'v32as -C') into a VBIN file.\n"
        "\n"
        "  -o, --output=FILE    output file (default: a.vbin)\n"
        "  -b, --bios           link for the BIOS (addresses start at 0x10000000)\n"
        "  -c, --cart           link for a cartridge (0x20000000; this is the default)\n"
        "  -O, --origin ADDR    link for an arbitrary base address\n"
        "  -g, --debug[=MODE]   also write FILE.debug (address,file,line[,label]); MODE is\n"
        "                       program (default), vbin or v32, as in 'v32as -g'\n"
        "  -r, --relocs=FILE    write 'index,value' for every word that holds an address\n"
        "  -w, --no-warnings    do not warn about unused global symbols\n"
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
        { "no-warnings", no_argument,       0, 'w' },
        { "help",        no_argument,       0, 'h' },
        { "version",     no_argument,       0, 'V' },
        { 0, 0, 0, 0 }
    };

    int          bios = 0, cart = 0, origin_set = 0, debug = 0, no_warn = 0;
    uint32_t     origin = 0, base;
    dbgmode_t    dmode = DBG_PROGRAM;
    const char  *output = "a.vbin", *relocs = NULL;
    object_t    *obj;
    int          nobj, c, i, errors = 0, rc = 0;
    gsym_t      *g = NULL;
    size_t       ng = 0, capg = 0, total = 0, k, j;
    uint32_t    *words = NULL;
    FILE        *fp;

    while ((c = getopt_long (argc, argv, "bcO:o:g::r:whV", long_options, NULL)) != -1)
    {
        switch (c)
        {
            case 'b':  bios = 1;                  break;
            case 'c':  cart = 1;                  break;
            case 'w':  no_warn = 1;               break;
            case 'o':  output = optarg;           break;
            case 'r':  relocs = optarg;           break;
            case 'h':  usage (stdout);            return (0);
            case 'V':  printf ("v32ld %s\n", VERSION);  return (0);
            case 'O':
            {
                char           *end;
                unsigned long   v = strtoul (optarg, &end, 0);

                if (*optarg == '\0' || *end != '\0' || v > 0xFFFFFFFFul)
                {
                    fprintf (stderr, "v32ld: invalid --origin '%s'\n", optarg);
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
                    fprintf (stderr, "v32ld: unrecognized reference mode '%s' for -g "
                                     "(use program, vbin or v32)\n", optarg);
                    return (2);
                }
                break;
            default:
                usage (stderr);
                return (2);
        }
    }

    if (optind >= argc)
    {
        usage (stderr);
        return (2);
    }

    if ((bios && cart) || (origin_set && (bios || cart)))
    {
        fprintf (stderr, "v32ld: --bios, --cart and --origin are mutually exclusive\n");
        return (2);
    }

    base = origin_set ? origin : (bios ? BIOS_BASE : CART_BASE);
    nobj = argc - optind;
    obj  = xrealloc (NULL, (size_t) nobj * sizeof (object_t));
    memset (obj, 0, (size_t) nobj * sizeof (object_t));

    for (i = 0; i < nobj; i++)
    {
        if (load_object (&obj[i], argv[optind + i]) != 0)
        {
            return (2);
        }

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

    qsort (g, ng, sizeof (gsym_t), gsym_cmp);

    for (k = 1; k < ng; k++)
    {
        if (strcmp (g[k].name, g[k - 1].name) == 0)
        {
            fprintf (stderr, "v32ld: error: '%s' is defined in both %s and %s\n",
                     g[k].name, g[k - 1].where, g[k].where);
            errors++;
        }
    }

    // the image: the objects' words, then every relocation applied
    words = xrealloc (NULL, total * sizeof (uint32_t));

    for (i = 0; i < nobj; i++)
    {
        for (k = 0; k < obj[i].nwords; k++)
        {
            words[obj[i].start + k] = get32 (obj[i].words + 4 * k);
        }
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
                fprintf (stderr, "v32ld: %s: corrupt object (relocation out of range)\n", obj[i].path);
                return (2);
            }

            if (r.sym == NO_SYMBOL)
            {
                value = base + obj[i].start + r.addend;     // relative to the object itself
            }
            else
            {
                osym_t  s = osym (&obj[i], r.sym);

                if (s.flags & SYM_DEFINED)
                {
                    value = base + obj[i].start + s.value + r.addend;
                }
                else
                {
                    gsym_t   key = { s.name, 0, NULL };
                    gsym_t  *hit = bsearch (&key, g, ng, sizeof (gsym_t), gsym_cmp);

                    if (hit == NULL)
                    {
                        fprintf (stderr, "v32ld: error: %s: undefined symbol '%s'\n",
                                 obj[i].path, s.name);
                        errors++;
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
                    fprintf (stderr, "v32ld: warning: %s: extern '%s' is never used\n",
                             obj[i].path, s.name);
                }
            }
        }
    }

    // output: the VBIN
    fp = fopen (output, "wb");

    if (fp == NULL)
    {
        fprintf (stderr, "v32ld: cannot write '%s': %s\n", output, strerror (errno));
        return (2);
    }

    fwrite ("V32-VBIN", 1, 8, fp);

    for (k = 0; k < 4; k++)
    {
        fputc ((int) (((uint32_t) total >> (8 * k)) & 0xFF), fp);
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
        fprintf (stderr, "v32ld: error writing '%s'\n", output);
        return (2);
    }

    if (relocs != NULL)
    {
        fp = fopen (relocs, "w");

        if (fp == NULL)
        {
            fprintf (stderr, "v32ld: cannot write '%s': %s\n", relocs, strerror (errno));
            return (2);
        }

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nrels; k++)
            {
                uint32_t  w = get32 (obj[i].rels + 12 * k);

                fprintf (fp, "%u,0x%08X\n", obj[i].start + w, words[obj[i].start + w]);
            }
        }

        fclose (fp);
    }

    if (debug)
    {
        label_t  *lab = NULL;
        size_t    nl = 0, capl = 0;
        char     *path = xrealloc (NULL, strlen (output) + 8);
        uint32_t  base_off = (dmode == DBG_V32) ? 0x8C : (dmode == DBG_VBIN ? 0x0C : 0);
        uint32_t  unit     = (dmode == DBG_PROGRAM) ? 1 : 4;

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].nsyms; k++)
            {
                osym_t  s = osym (&obj[i], (uint32_t) k);

                if (!(s.flags & SYM_DEFINED))
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

        qsort (lab, nl, sizeof (label_t), label_cmp);

        sprintf (path, "%s.debug", output);
        fp = fopen (path, "w");

        if (fp == NULL)
        {
            fprintf (stderr, "v32ld: cannot write '%s': %s\n", path, strerror (errno));
            return (2);
        }

        for (i = 0; i < nobj; i++)
        {
            for (k = 0; k < obj[i].ndbg; k++)
            {
                const unsigned char  *p = obj[i].dbg + 12 * k;
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

                if (lo < nl && lab[lo].addr == addr)
                {
                    fprintf (fp, ",%s", lab[lo].name);
                }

                fputc ('\n', fp);
            }
        }

        fclose (fp);
        free   (path);
        free   (lab);
    }

    for (i = 0; i < nobj; i++)
    {
        free (obj[i].raw);
    }

    free (obj);
    free (g);
    free (words);

    return (rc);
}
