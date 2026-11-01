//
// main.c: command-line front end for v32as, the C implementation of the Vircon32
// assembler. Handles option parsing, file and STDIN input, and the interactive mode.
//
// run for help:          ./v32as --help
// assemble to a listing: ./v32as --cart program.asm
// assemble to a VBIN:    ./v32as --cart --binary -o program.vbin program.asm
// interactive mode:      ./v32as            (when STDIN is a terminal)
//
// The bash v32as options (bios, cart, none, binary, step, verbose, help) are still
// accepted as bare words, as long as no file with that name exists.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "v32as.h"

typedef struct
{
    int          bios;
    int          debug;
    dbgmode_t    debug_mode;
    int          origin_set;
    uint32_t     origin;
    int          cart;
    int          binary;
    int          step;
    int          verbose;
    int          help;
    int          version;
    int          text;              // -T: print a listing instead of writing a VBIN
    int          none;              // -n: no base address (implies --text)
    int          binary_explicit;   // -B was given
    int          v32x;              // --v32x: a v32nix executable (8-word header, then the words), needs --origin
    long         v32x_data;         // --v32x-data=N: zeroed data words after the image (else %define __V32X_DATAWORDS, else 0)
    int          v32x_data_set;
    int          standard;          // --standard: strict official-assembler behaviour
    int          no_warn;           // -w
    int          preprocess;        // -E: stop after preprocessing
    int          no_markers;        // -P: no line markers in -E output
    const char  *output;
    int          object;            // -C: write a relocatable object
    const char  *relocs;            // -r FILE: list of address-holding words
    const char  *map;               // --map FILE: compiler debug file to merge into -g
    const char  *symbols;           // --symbols FILE: label table
    int          symbols_all;       // --symbols-all: also .local labels and compiler variables
    int          dep;               // -MD: write a dependency file
    const char  *dep_file;          // -MF FILE
    const char  *dep_target;        // -MT TARGET
    int          dep_phony;         // -MP
    int          no_redefine;       // -Wno-redefine
    int          warn_markers;      // -Wmarkers: warn about a malformed ;@src line
}
options_t;

// -D / -U in command line order
typedef struct
{
    const char  *arg;
    int          undef;
}
macro_opt_t;

////////////////////////////////////////////////////////////////////////////////////////
//
// Usage
//
static void  usage (FILE *fp)
{
    fprintf (fp,
        "usage: v32as [OPTION...] [ASMFILE...]\n"
        "Assemble Vircon32 assembly into a VBIN file (FILE.vbin by default), like the\n"
        "official 'assemble' tool.\n"
        "\n"
        "  -o, --output=FILE    output file (default: the input file with a .vbin extension;\n"
        "                       STDOUT when reading STDIN)\n"
        "  -b, --bios           assemble for BIOS (addresses start at 0x10000000)\n"
        "  -c, --cart           assemble for CART (0x20000000; this is the default)\n"
        "  -O, --origin ADDR    assemble for an arbitrary base address (e.g. code that will run from RAM)\n"
        "      --v32x           write a v32nix executable (V32X: an 8-word header, then the program) instead of a\n"
        "                       VBIN; needs --origin.  The data words that follow the program come from\n"
        "      --v32x-data=N    or, without it, from the source's %%define __V32X_DATAWORDS (v32cc --data-after-text)\n"
        "  -g, --debug[=MODE]   also write FILE.debug (CSV: address,file,line[,label]) next to the\n"
        "                       output file; MODE is program (default), vbin or v32, as in the\n"
        "                       official assembler's \"-g MODE\"; when the source has ';@src FILE:LINE'\n"
        "                       lines (v32lua -g, v32cc -g) the entries come from them: one per marker,\n"
        "                       at its first instruction, with the function (__function_NAME) as 4th\n"
        "                       column; --map wins for the instructions it names. ';@var'/';@scope' lines\n"
        "                       also write FILE.vars (the locals as address ranges)\n"
        "  -C, --object         write a relocatable object (FILE.v32ob) for the linker v32asld; labels\n"
        "                       are relative to the object, 'global'/'extern' name the symbols shared\n"
        "                       with other objects, debug info is stored in the object\n"
        "  -r, --relocs=FILE    write the list of words that hold an address (label references,\n"
        "                       code and data) as 'index,value' lines; for relocatable modules\n"
        "      --map=FILE       FILE is v32cc's debug file (asmfile,asmline,cfile,cline[,func]): with -g\n"
        "                       the .debug file names the C file and line of each instruction, with -C\n"
        "                       the object does, and errors and warnings say which C line they come from\n"
        "      --symbols=FILE   write the label table: one 'address,name,kind' line per label (kind F:\n"
        "                       code, D: data); file-local labels only with --symbols-all, which\n"
        "                       also lists the compiler's global_* variables and\n"
        "                       DATASYM+N references (kind V, address column = N)\n"
        "      --symbols-all    with --symbols: add 'function.local' labels, global_* and DATASYM+N variables\n"
        "  -MD, -MMD            write the files read (%%include, datafile) as a Makefile rule to\n"
        "                       OUTPUT with a .d extension; -MF FILE names it, -MT TARGET the target,\n"
        "                       -MP adds an empty rule for each dependency\n"
        "  -T, --text           print a human-readable listing (instruction encodings) instead\n"
        "                       of writing a VBIN; addresses start at 0 unless -b/-c/-O is given\n"
        "  -n, --none           same as --text with no base address\n"
        "  -w, --no-warnings    inhibit all warnings\n"
        "  -D, --define=NAME[=VALUE]\n"
        "                       predefine a preprocessor symbol (VALUE defaults to 1); a %%define of\n"
        "                       the same name in the source warns and wins, unless the option is\n"
        "                       written -D!NAME=VALUE (or --force-define), when the command line wins\n"
        "  -Wno-redefine        do not warn when a %%define changes a macro's value (-Wredefine: do)\n"
        "  -Wmarkers            warn about a malformed ;@src, ;@var or ;@scope line (off by default)\n"
        "  -U, --undef=NAME     remove a predefined symbol (processed in command line order)\n"
        "  -I, --include=DIR    add DIR to the %%include search path (may be repeated)\n"
        "  -E, --preprocess     only run the preprocessor and print the result\n"
        "  -P                   with -E: omit the '# line \"file\"' markers\n"
        "      --standard       strict compatibility with the official 'assemble': no language\n"
        "                       extensions, no extended preprocessor, official -v messages\n"
        "  -s, --step           pause after each statement (files only)\n"
        "  -v, --verbose        trace the assembly of every instruction to STDERR\n"
        "  -h, --help           display this usage information and exit\n"
        "  -V, --version        display the version and exit\n"
        "\n"
        "With no ASMFILE (or with '-'), input comes from STDIN. When STDIN is a terminal\n"
        "this starts the interactive mode: each line is assembled as soon as it is\n"
        "entered; type .help for the commands available there.\n"
        "\n"
        "The bash-style words bios, cart, none, binary, step, verbose and help are also\n"
        "accepted in place of the options above.\n");
}

////////////////////////////////////////////////////////////////////////////////////////
//
// --v32x: the v32nix executable (v32nix inc/exec.h): magic "V32X" (0x58323356), version 1, header words 8, text words,
// data words, entry offset 0, origin, flags 0; then the program's words.  Written as 32-bit little-endian words on a
// host; on the console, where a char is a word, one file word per machine word, which is what v32nix's exec reads.
//
typedef struct { int found; long value; } v32x_find_t;

static void  v32x_find_data (void *ud, const char *name, const char *body)
{
    v32x_find_t *f = ud;

    if (strcmp (name, "__V32X_DATAWORDS") == 0 && body != NULL)
    {
        f -> found = 1;
        f -> value = strtol (body, NULL, 0);
    }
}

static int  put_word (FILE *fp, uint32_t w)
{
#ifdef __V32CC__
    fputc ((int) w, fp);                        // (a word of all ones is EOF's value: only ferror tells a failure)
    return (ferror (fp) ? -1 : 0);
#else
    unsigned char  b[4];

    b[0] = (unsigned char) (w & 0xFF);          b[1] = (unsigned char) ((w >> 8) & 0xFF);
    b[2] = (unsigned char) ((w >> 16) & 0xFF);  b[3] = (unsigned char) ((w >> 24) & 0xFF);
    return (fwrite (b, 1, 4, fp) == 4 ? 0 : -1);
#endif
}

static int  write_v32x (asm_t *a, FILE *fp, uint32_t origin, long data)
{
    uint32_t  hdr[8] = { 0x58323356u, 1, 8, 0, 0, 0, 0, 0 };
    size_t    i;

    hdr[3] = (uint32_t) a -> nwords;
    hdr[4] = (uint32_t) data;
    hdr[6] = origin;
    for (i = 0; i < 8; i++)
    {
        if (put_word (fp, hdr[i]) != 0)
        {
            return (-1);
        }
    }
    for (i = 0; i < a -> nwords; i++)
    {
        if (put_word (fp, a -> words[i]) != 0)
        {
            return (-1);
        }
    }
    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// FILE.ext -> FILE.vbin, as the official assembler names its output
//
static char  *vbin_name (const char *input, const char *ext)
{
    const char  *slash = strrchr (input, '/');
    const char  *base  = slash ? slash + 1 : input;
    const char  *dot   = strrchr (base, '.');
    size_t       stem  = (dot != NULL && dot != base) ? (size_t) (dot - input) : strlen (input);
    char        *name  = malloc (stem + strlen (ext) + 1);

    if (name != NULL)
    {
        memcpy (name, input, stem);
        strcpy (name + stem, ext);
    }

    return (name);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Input
//
static char  *read_stream (FILE *fp, size_t *len)
{
    size_t  cap = 1 << 16;
    size_t  n   = 0;
    char   *buf;
    long    size;

    // a file whose size is known is read into one block of that size: doubling a buffer needs the old and the new one at once,
    // which on v32nix (a character is a word, a process has at most ~300 K words) does not fit for a linked program's assembly
    if (fp != stdin && fseek (fp, 0, SEEK_END) == 0 && (size = ftell (fp)) >= 0 && fseek (fp, 0, SEEK_SET) == 0)
    {
        cap = (size_t) size + 1;
    }
    buf = malloc (cap);

    if (buf == NULL)
    {
        return (NULL);
    }

    for (;;)
    {
        size_t  got;

        if (n == cap)
        {
            char  *bigger = realloc (buf, cap * 2);

            if (bigger == NULL)
            {
                free (buf);
                return (NULL);
            }

            buf  = bigger;
            cap *= 2;
        }

        got = fread (buf + n, 1, cap - n, fp);

        if (got == 0)
        {
            break;
        }

        n += got;
    }

    if (ferror (fp))
    {
        free (buf);
        return (NULL);
    }

    *len = n;
    return (buf);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Interactive mode: assemble each line as it is typed
//
static char  *read_line (const char *prompt)
{
#ifdef HAVE_READLINE
    return (readline (prompt));
#else
    char    *line = NULL;
    size_t   cap  = 0;
    ssize_t  got;

    fputs  (prompt, stdout);
    fflush (stdout);

    got = getline (&line, &cap, stdin);

    if (got < 0)
    {
        free (line);
        return (NULL);
    }

    if (got > 0 && line[got - 1] == '\n')
    {
        line[got - 1] = 0;
    }

    return (line);
#endif
}

static void  repl_help (void)
{
    puts ("Type any instruction or data directive to assemble it. Labels (_name:) work\n"
          "too; references to labels defined later stay symbolic in the listing.\n"
          "\n"
          "  .help      show this text\n"
          "  .labels    list the labels defined so far\n"
          "  .dump      show every word assembled so far\n"
          "  .reset     forget everything assembled so far\n"
          "  .quit      leave (so does Ctrl-D)");
}

static void  repl_dump (const asm_t *a)
{
    size_t  i;

    for (i = 0; i < a -> nwords; i++)
    {
        printf ("0x%08X  0x%08X\n", a -> base + (uint32_t) i, a -> words[i]);
    }
}

// returns 0 to leave the interactive mode
static int  repl_command (asm_t *a, const char *cmd)
{
    if (strcmp (cmd, ".help") == 0)
    {
        repl_help ();
    }
    else if (strcmp (cmd, ".labels") == 0)
    {
        asm_print_labels (a, stdout);
    }
    else if (strcmp (cmd, ".dump") == 0)
    {
        repl_dump (a);
    }
    else if (strcmp (cmd, ".reset") == 0)
    {
        asm_reset (a);
        puts ("(cleared)");
    }
    else if (strcmp (cmd, ".quit") == 0 || strcmp (cmd, ".exit") == 0)
    {
        return (0);
    }
    else
    {
        fprintf (stderr, "unknown command '%s' (try .help)\n", cmd);
    }

    return (1);
}

static int  repl (asm_t *a, pp_t *pp)
{
    int  lineno = 0;

    a -> interactive = 1;
    a -> step        = 0;

    printf ("v32as %s interactive mode: .help for commands, Ctrl-D to quit\n", VERSION);

    pp_stream_begin (pp, "<stdin>");

    for (;;)
    {
        char   prompt[32];
        char  *line;
        char  *p;
        char  *end;

        if (pp_pending (pp))
        {
            snprintf (prompt, sizeof (prompt), "   ...> ");
        }
        else if (a -> has_base)
        {
            snprintf (prompt, sizeof (prompt), "[0x%08X] > ", a -> base + (uint32_t) a -> nwords);
        }
        else
        {
            snprintf (prompt, sizeof (prompt), "v32as> ");
        }

        line = read_line (prompt);

        if (line == NULL)
        {
            putchar ('\n');
            break;
        }

        p = line;

        while (isspace ((unsigned char) *p))
        {
            p++;
        }

        end = p + strlen (p);

        while (end > p && isspace ((unsigned char) end[-1]))
        {
            *--end = 0;
        }

        if (*p == 0)
        {
            free (line);
            continue;
        }

#ifdef HAVE_READLINE
        add_history (p);
#endif

        if (*p == '.')
        {
            int  keep = repl_command (a, p);

            free (line);

            if (!keep)
            {
                break;
            }

            continue;
        }

        lineno++;

        if (pp_stream_line (pp, p, strlen (p), lineno) == 0)
        {
            size_t           len;
            size_t           nmap;
            const char      *text = pp_output (pp, &len);
            const srcloc_t  *map  = pp_linemap (pp, &nmap);

            if (len > 0)
            {
                asm_parse_map (a, "<stdin>", text, len, map, nmap);
            }
        }

        pp_output_clear (pp);
        free (line);
    }

    if (a -> nfix > 0)
    {
        fprintf (stderr, "note: %zu label reference(s) were never resolved\n", a -> nfix);
    }

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Run one source text through the preprocessor and then (unless -E) the assembler.
// Returns 0, or 1 if the preprocessor found errors (nothing is assembled then).
//
// takes text: it is freed as soon as the preprocessor is done with it, and the preprocessor's output is assembled in place, so at
// most one copy of the program's text is alive at a time (v32nix: a linked hello world is 75 K characters, a character a word)
static int  process_source (asm_t *a, pp_t *pp, const options_t *opt, FILE *out,
                            const char *name, char *text, size_t len)
{
    size_t           olen;
    size_t           nmap;
    const srcloc_t  *map;
    int              failed;

    pp_reserve_output (pp, len + len / 2);
    failed = (pp_process (pp, name, text, len) != 0);

    free (text);
    if (!failed)
    {
        map   = pp_linemap (pp, &nmap);

        if (opt -> preprocess)
        {
            pp_write_expanded (pp, out, !opt -> no_markers);
        }
        else
        {
            char  *otext = pp_output_take (pp, &olen);

            asm_parse_map_owned (a, name, otext, olen, map, nmap);
        }
    }

    pp_output_clear (pp);

    return (failed);
}

// a file, read by the preprocessor a line at a time; otherwise as process_source
static int  process_file (asm_t *a, pp_t *pp, const options_t *opt, FILE *out, const char *name, FILE *fp)
{
    size_t           olen;
    size_t           nmap;
    const srcloc_t  *map;
    int              failed;
    long             size;

    // a file of known size: the output buffer is allocated once at about its final size, not doubled (v32nix: a character is a word)
    if (fp != stdin && fseek (fp, 0, SEEK_END) == 0 && (size = ftell (fp)) > 0 && fseek (fp, 0, SEEK_SET) == 0)
    {
        pp_reserve_output (pp, (size_t) size + (size_t) size / 2);
    }

    failed = (pp_process_file (pp, name, fp) != 0) || ferror (fp);

    if (!failed)
    {
        map = pp_linemap (pp, &nmap);

        if (opt -> preprocess)
        {
            pp_write_expanded (pp, out, !opt -> no_markers);
        }
        else
        {
            char  *otext = pp_output_take (pp, &olen);

            asm_parse_map_owned (a, name, otext, olen, map, nmap);
        }
    }

    pp_output_clear (pp);

    return (failed);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// --symbols: the compiler's variables (%define global_NAME number) and local labels
//
typedef struct
{
    symline_t  *v;
    size_t      n;
    size_t      cap;
}
symbols_t;

static void  collect_variable (void *ud, const char *name, const char *body)
{
    symbols_t      *x = ud;
    char           *end;
    unsigned long   v;

    if (strncmp (name, "global_", 7) != 0)
    {
        return;
    }

    while (isspace ((unsigned char) *body))  { body++; }

    if (*body == 0)
    {
        return;
    }

    v = strtoul (body, &end, 0);

    while (isspace ((unsigned char) *end))  { end++; }

    if (end == body || *end != 0 || v > 0xFFFFFFFFul)
    {
        return;
    }

    if (x -> n == x -> cap)
    {
        x -> cap = x -> cap ? x -> cap * 2 : 64;
        x -> v   = realloc (x -> v, x -> cap * sizeof (symline_t));
    }

    x -> v[x -> n].name = name;
    x -> v[x -> n].addr = (uint32_t) v;
    x -> v[x -> n].kind = 'V';
    x -> n++;
}

static const char  *local_name (void *ud, const char *name)
{
    return (pp_local_name ((pp_t *) ud, name));
}

////////////////////////////////////////////////////////////////////////////////////////
//
// -MD / -MF / -MT / -MP: the files read, as a Makefile rule
//
static void  dep_word (FILE *fp, const char *w)
{
    for (; *w; w++)
    {
        if (*w == ' ' || *w == '#')  { fputc ('\\', fp); }
        if (*w == '$')               { fputc ('$', fp); }
        fputc (*w, fp);
    }
}

static int  write_deps (const options_t *opt, const char **files, int nfiles, pp_t *pp, asm_t *a)
{
    const char        *target = opt -> dep_target != NULL ? opt -> dep_target : opt -> output;
    char              *derived = NULL;
    const char        *path = opt -> dep_file;
    const char *const *ppd;
    size_t             nppd, i, k, ndep = 0, cap;
    const char       **dep;
    FILE              *fp;

    if (path == NULL)
    {
        derived = vbin_name (opt -> output, ".d");
        path    = derived;
    }

    ppd = pp_deps (pp, &nppd);
    cap = (size_t) nfiles + nppd + a -> ndeps + 1;
    dep = malloc (cap * sizeof (char *));

    // the sources first, then what they pulled in; each file once
    for (i = 0; i < cap - 1; i++)
    {
        const char  *f = i < (size_t) nfiles ? files[i]
                       : (i < (size_t) nfiles + nppd ? ppd[i - (size_t) nfiles]
                                                     : a -> deps[i - (size_t) nfiles - nppd]);

        if (strcmp (f, "-") == 0)
        {
            continue;
        }

        for (k = 0; k < ndep && strcmp (dep[k], f) != 0; k++)
        {
        }

        if (k == ndep)
        {
            dep[ndep++] = f;
        }
    }

    fp = fopen (path, "w");

    if (fp == NULL)
    {
        fprintf (stderr, "v32as: cannot write '%s': %s\n", path, strerror (errno));
        free (dep);
        free (derived);
        return (2);
    }

    dep_word (fp, target);
    fputc (':', fp);

    for (i = 0; i < ndep; i++)
    {
        fputs (" \\\n  ", fp);
        dep_word (fp, dep[i]);
    }

    fputc ('\n', fp);

    for (i = opt -> dep_phony ? (size_t) nfiles : ndep; i < ndep; i++)
    {
        fputc ('\n', fp);
        dep_word (fp, dep[i]);
        fputs (":\n", fp);
    }

    fclose (fp);
    free (dep);
    free (derived);

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Command line
//
static int  legacy_word (const char *arg, options_t *opt)
{
    // the bash version took bare words; honor them unless a file has that name
    if (access (arg, F_OK) == 0)
    {
        return (0);
    }

    if (strcasecmp (arg, "bios")    == 0)  { opt -> bios    = 1;  return (1); }
    if (strcasecmp (arg, "cart")    == 0)  { opt -> cart    = 1;  return (1); }
    if (strcasecmp (arg, "none")    == 0)  { opt -> none    = 1;  return (1); }
    if (strcasecmp (arg, "binary")  == 0)  { opt -> binary_explicit = 1;  return (1); }
    if (strcasecmp (arg, "step")    == 0)  { opt -> step    = 1;  return (1); }
    if (strcasecmp (arg, "verbose") == 0)  { opt -> verbose = 1;  return (1); }
    if (strcasecmp (arg, "help")    == 0)  { opt -> help    = 1;  return (1); }

    return (0);
}

int  main (int  argc, char **argv)
{
    static const struct option  long_options[] =
    {
        { "bios",     no_argument,       0, 'b' },
        { "cart",     no_argument,       0, 'c' },
        { "none",     no_argument,       0, 'n' },
        { "text",     no_argument,       0, 'T' },
        { "no-warnings", no_argument,    0, 'w' },
        { "include",  required_argument, 0, 'I' },
        { "standard", no_argument,       0, 1001 },
        { "relocs",   required_argument, 0, 'r' },
        { "object",   no_argument,       0, 'C' },
        { "map",      required_argument, 0, 1002 },
        { "symbols",  required_argument, 0, 1003 },
        { "symbols-all", no_argument,    0, 1004 },
        { "force-define", required_argument, 0, 1005 },
        { "v32x",     no_argument,       0, 1006 },
        { "v32x-data", required_argument, 0, 1007 },
        { "binary",   no_argument,       0, 'B' },
        { "debug",    optional_argument, 0, 'g' },
        { "origin",   required_argument, 0, 'O' },
        { "output",   required_argument, 0, 'o' },
        { "define",   required_argument, 0, 'D' },
        { "undef",    required_argument, 0, 'U' },
        { "preprocess", no_argument,     0, 'E' },
        { "step",     no_argument,       0, 's' },
        { "verbose",  no_argument,       0, 'v' },
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'V' },
        { 0,          0,                 0,  0  }
    };

    options_t    opt   = { 0 };
    const char **files = calloc ((size_t) argc + 1, sizeof (char *));
    int          nfiles = 0;
    int          c;
    int          i;
    asm_t        a;
    pp_t        *pp;
    macro_opt_t *mopts  = calloc ((size_t) argc + 1, sizeof (macro_opt_t));
    const char **incdirs = calloc ((size_t) argc + 1, sizeof (char *));
    int          nincdirs = 0;
    int          will_repl;
    char        *derived = NULL;
    int          nmopts = 0;
    int          pp_failed = 0;
    FILE        *out   = stdout;
    int          rc    = 0;

    if (files == NULL || mopts == NULL || incdirs == NULL)
    {
        return (2);
    }

    // gcc-style dependency options (-MD -MMD -MP -MF file -MT target) are taken out first:
    // getopt would read them as the option -M
    {
        int  w = 1;

        for (i = 1; i < argc; i++)
        {
            const char  *x = argv[i];

            if (strcmp (x, "--") == 0)
            {
                while (i < argc)  { argv[w++] = argv[i++]; }
                break;
            }
            else if (strcmp (x, "-MD") == 0 || strcmp (x, "-MMD") == 0)  { opt.dep = 1; }
            else if (strcmp (x, "-MP") == 0)                              { opt.dep_phony = 1; }
            else if ((strncmp (x, "-MF", 3) == 0 || strncmp (x, "-MT", 3) == 0) && (x[3] || i + 1 < argc))
            {
                const char  *val = x[3] ? x + 3 : argv[++i];

                if (x[2] == 'F')  { opt.dep_file = val;  opt.dep = 1; }
                else              { opt.dep_target = val;  opt.dep = 1; }
            }
            else
            {
                argv[w++] = argv[i];
            }
        }

        argc = w;
        argv[argc] = NULL;
    }

    while ((c = getopt_long (argc, argv, "bcnCTBgwO:o:r:D:U:I:W:EPsvhV", long_options, NULL)) != -1)
    {
        switch (c)
        {
            case 'b':  opt.bios    = 1;       break;
            case 'c':  opt.cart    = 1;       break;
            case 'n':  opt.none    = 1;       break;
            case 'T':  opt.text    = 1;       break;
            case 'B':  opt.binary_explicit = 1;  break;
            case 'w':  opt.no_warn = 1;       break;
            case 'I':  incdirs[nincdirs++] = optarg;  break;
            case 1001: opt.standard = 1;      break;
            case 1002: opt.map     = optarg;  break;
            case 1003: opt.symbols = optarg;  break;
            case 1004: opt.symbols_all = 1;   break;
            case 1006: opt.v32x = 1;          break;
            case 1007: opt.v32x_data = strtol (optarg, NULL, 0);  opt.v32x_data_set = 1;  break;
            case 1005:
            {
                char  *spec = malloc (strlen (optarg) + 2);

                sprintf (spec, "!%s", optarg);
                mopts[nmopts].arg = spec;  mopts[nmopts++].undef = 0;
                break;
            }
            case 'W':
                if (strcmp (optarg, "no-redefine") == 0)       { opt.no_redefine = 1; }
                else if (strcmp (optarg, "redefine") == 0)     { opt.no_redefine = 0; }
                else if (strcmp (optarg, "markers") == 0)      { opt.warn_markers = 1; }
                else if (strcmp (optarg, "no-markers") == 0)   { opt.warn_markers = 0; }
                else
                {
                    fprintf (stderr, "v32as: unknown warning option -W%s (use -Wredefine, -Wno-redefine, -Wmarkers or -Wno-markers)\n", optarg);
                    return (2);
                }
                break;
            case 'r':  opt.relocs  = optarg;  break;
            case 'C':  opt.object  = 1;       break;
            case 'g':
            {
                const char  *m = optarg;

                // "-g" takes its mode as the next word, like the official assembler's
                // "-g program"; the mode is optional here and defaults to "program"
                if (m == NULL && optind < argc
                    && (strcmp (argv[optind], "program") == 0 || strcmp (argv[optind], "vbin") == 0
                        || strcmp (argv[optind], "v32") == 0))
                {
                    m = argv[optind++];
                }

                opt.debug      = 1;
                opt.debug_mode = DBG_PROGRAM;

                if (m != NULL && strcmp (m, "vbin") == 0)         { opt.debug_mode = DBG_VBIN; }
                else if (m != NULL && strcmp (m, "v32") == 0)     { opt.debug_mode = DBG_V32; }
                else if (m != NULL && strcmp (m, "program") != 0)
                {
                    fprintf (stderr, "v32as: unrecognized reference mode '%s' for -g "
                                     "(use program, vbin or v32)\n", m);
                    return (2);
                }
                break;
            }
            case 'O':
            {
                char  *end;
                unsigned long  v = strtoul (optarg, &end, 0);
                if (*optarg == '\0' || *end != '\0' || v > 0xFFFFFFFFul)
                {
                    fprintf (stderr, "v32as: invalid --origin '%s'\n", optarg);
                    return (2);
                }
                opt.origin_set = 1;
                opt.origin     = (uint32_t) v;
                break;
            }
            case 'o':  opt.output  = optarg;  break;
            case 'D':  mopts[nmopts].arg = optarg;  mopts[nmopts++].undef = 0;  break;
            case 'U':  mopts[nmopts].arg = optarg;  mopts[nmopts++].undef = 1;  break;
            case 'E':  opt.preprocess = 1;    break;
            case 'P':  opt.no_markers = 1;    break;
            case 's':  opt.step    = 1;       break;
            case 'v':  opt.verbose = 1;       break;
            case 'h':  opt.help    = 1;       break;
            case 'V':  opt.version = 1;       break;
            default:
                usage (stderr);
                return (2);
        }
    }

    for (i = optind; i < argc; i++)
    {
        if (!legacy_word (argv[i], &opt))
        {
            files[nfiles++] = argv[i];
        }
    }

    if (opt.help)
    {
        usage (stdout);
        return (0);
    }

    if (opt.version)
    {
        printf ("v32as %s\n", VERSION);
        return (0);
    }

    // what are we producing? A VBIN unless a listing (--text/--none) or only the
    // preprocessed source (-E) was asked for. Reading a terminal means the interactive mode.
    if (opt.binary_explicit && (opt.text || opt.none))
    {
        fprintf (stderr, "v32as: --binary cannot be combined with --text or --none\n");
        return (2);
    }

    if (opt.binary_explicit && opt.preprocess)
    {
        fprintf (stderr, "v32as: --preprocess cannot be combined with --binary\n");
        return (2);
    }

    if (opt.standard && nfiles > 1)
    {
        fprintf (stderr, "v32as: error: too many input files, only 1 is supported (--standard)\n");
        return (1);
    }

    will_repl  = (nfiles == 0 && isatty (STDIN_FILENO) && !opt.binary_explicit && !opt.preprocess);

    if (opt.object)
    {
        if (opt.bios || opt.cart || opt.origin_set || opt.debug || opt.relocs
            || opt.text || opt.none || opt.preprocess || opt.binary_explicit)
        {
            fprintf (stderr, "v32as: --object cannot be combined with -b, -c, -O, -g, -r, -T, -n, -E or -B "
                             "(addresses and debug info are decided by the linker)\n");
            return (2);
        }

        if (opt.standard)
        {
            fprintf (stderr, "v32as: --object is not part of the official assembler (--standard)\n");
            return (2);
        }

        if (will_repl)
        {
            fprintf (stderr, "v32as: --object needs an input file\n");
            return (2);
        }

    }
    {
        // -E reports the symbols (__CART__ ...) of the build it stands in for
        int  makes_vbin = !(opt.text || opt.none || will_repl);

        opt.binary = makes_vbin && !opt.preprocess;
        opt.cart  |= (makes_vbin && !opt.object && !opt.bios && !opt.origin_set && !opt.cart);
    }

    if (opt.v32x && (!opt.origin_set || opt.object || !opt.binary))
    {
        fprintf (stderr, "v32as: --v32x needs --origin ADDR and binary output (not --object, --text or -E)\n");
        return (2);
    }

    if (opt.origin_set && (opt.bios || opt.cart))
    {
        fprintf (stderr, "v32as: --origin cannot be combined with --bios or --cart\n");
        return (2);
    }

    if (opt.bios && opt.cart)
    {
        fprintf (stderr, "v32as: --bios and --cart cannot be combined\n");
        return (2);
    }

    // the output file: -o, or the input file's name with a .vbin extension
    if (opt.binary && opt.output == NULL && nfiles == 1 && strcmp (files[0], "-") != 0)
    {
        derived    = vbin_name (files[0], opt.object ? ".v32ob" : ".vbin");

        opt.output = derived;

        if (opt.standard && opt.verbose && derived != NULL)
        {
            printf ("using output path: \"%s\"\n", derived);
        }
    }

    if (opt.debug && (!opt.binary || opt.output == NULL))
    {
        fprintf (stderr, "v32as: -g needs binary output to a file (give -o FILE or a single input file; "
                         "the debug info goes to FILE.debug)\n");
        return (2);
    }

    if (opt.symbols_all && !opt.symbols)
    {
        fprintf (stderr, "v32as: --symbols-all needs --symbols FILE\n");
        return (2);
    }

    if (opt.symbols && (opt.object || opt.preprocess || will_repl))
    {
        fprintf (stderr, "v32as: --symbols needs assembled input (use 'v32asld --symbols' for objects)\n");
        return (2);
    }

    if (opt.dep && opt.preprocess)
    {
        fprintf (stderr, "v32as: -MD cannot be combined with -E\n");
        return (2);
    }

    if (opt.dep && (!opt.dep_file && (!opt.binary || opt.output == NULL)))
    {
        fprintf (stderr, "v32as: -MD needs -MF FILE, or binary output to a file to name the dependency file after\n");
        return (2);
    }

    if (opt.dep && opt.dep_target == NULL && opt.output == NULL)
    {
        fprintf (stderr, "v32as: -MD needs -MT TARGET (there is no output file name to use)\n");
        return (2);
    }

    if (opt.map != NULL)                // loaded now: errors and warnings use it too
    {
        FILE  *mfp = fopen (opt.map, "r");
        int    bad;

        if (mfp == NULL)
        {
            fprintf (stderr, "v32as: cannot read '%s': %s\n", opt.map, strerror (errno));
            return (2);
        }

        bad = asm_cmap_load (mfp, opt.map);
        fclose (mfp);

        if (bad != 0)
        {
            return (2);
        }
    }

    if (opt.relocs && (!opt.binary || opt.output == NULL))
    {
        fprintf (stderr, "v32as: -r needs binary output to a file (give -o FILE or a single input file)\n");
        return (2);
    }

    pp = pp_new ();
    pp_set_standard (pp, opt.standard);
    pp_set_warnings (pp, !opt.no_warn);
    pp_set_redefine_warning (pp, !opt.no_redefine && !opt.standard);
    pp_set_target   (pp, opt.bios ? "BIOS" : (opt.cart ? "CART" : (opt.origin_set ? "ORIGIN" : "NONE")),
                     opt.origin_set ? opt.origin : (opt.bios ? BIOS_BASE : (opt.cart ? CART_BASE : 0)));

    for (i = 0; i < nincdirs; i++)
    {
        pp_add_include_dir (pp, incdirs[i]);
    }

    for (i = 0; i < nmopts; i++)
    {
        if (mopts[i].undef)
        {
            pp_undefine (pp, mopts[i].arg);
        }
        else if (pp_define_cmdline (pp, mopts[i].arg) != 0)
        {
            fprintf (stderr, "v32as: invalid macro name in -D '%s'\n", mopts[i].arg);
            return (2);
        }
    }

    asm_init (&a);

    a.has_base = (opt.bios || opt.cart || opt.origin_set || opt.object);
    a.object   = opt.object;
    a.base     = opt.origin_set ? opt.origin : (opt.bios ? BIOS_BASE : (opt.cart ? CART_BASE : 0));
    a.verbose  = opt.verbose && !opt.standard;      // --standard: -v is the official stage log
    a.step     = opt.step && !opt.standard;         // official: -s has no effect
    a.standard = opt.standard;
    a.src_markers  = !opt.standard;                 // ;@src comments are read except under --standard
    a.warn_markers = opt.warn_markers && !opt.standard;
    a.no_warn  = opt.no_warn;
    a.incdirs  = incdirs;
    a.nincdirs = nincdirs;
    a.listing  = !opt.binary;
    // what only some outputs need is not kept (a linked program's assembly on v32nix, a character a word, needs every word)
    a.no_rel   = opt.binary && !opt.relocs && !opt.object && !opt.symbols_all;
    a.no_rec   = opt.binary && !opt.debug && !opt.object;

    // text output goes out as the assembly proceeds; binary output only after success
    if (opt.output != NULL && !opt.binary)
    {
        out = fopen (opt.output, "w");

        if (out == NULL)
        {
            fprintf (stderr, "v32as: cannot write '%s': %s\n", opt.output, strerror (errno));
            return (2);
        }
    }

    a.out = out;

    if (opt.standard && opt.verbose && opt.binary)
    {
        printf ("stage 1: running lexer\nstage 2: running preprocessor\n"
                "stage 3: running parser\nstage 4: running emitter\n");
    }

    if (nfiles == 0 || (nfiles == 1 && strcmp (files[0], "-") == 0))
    {
        if (will_repl)
        {
            rc = repl (&a, pp);
            asm_free (&a);
            pp_free (pp);
            free (files);
            free (mopts);
            free (incdirs);
            free (derived);
            return (rc);
        }

        // single shot from STDIN: stepping makes no sense here
        {
            size_t  len  = 0;
            char   *text = read_stream (stdin, &len);

            if (text == NULL)
            {
                fprintf (stderr, "v32as: unable to read STDIN\n");
                return (2);
            }

            a.step = 0;

            if (process_source (&a, pp, &opt, out, "<stdin>", text, len))
            {
                rc = 1;
                pp_failed = 1;
            }
        }
    }
    else
    {
        if (opt.step && !isatty (STDIN_FILENO))
        {
            fprintf (stderr, "v32as: warning: STDIN is not a terminal; ignoring --step\n");
            a.step = 0;
        }

        for (i = 0; i < nfiles; i++)
        {
            FILE   *fp   = (strcmp (files[i], "-") == 0) ? stdin : fopen (files[i], "rb");
            size_t  len  = 0;
            char   *text;

            if (fp == NULL)
            {
                fprintf (stderr, "v32as: unable to read '%s': %s\n", files[i], strerror (errno));
                rc = 1;
                continue;
            }

            if (fp != stdin)
            {
                // a named file goes through the preprocessor a line at a time (it is never all in memory)
                int  failed = process_file (&a, pp, &opt, out, files[i], fp);

                fclose (fp);
                if (failed)
                {
                    rc = 1;
                    pp_failed = 1;
                }
                continue;
            }

            text = read_stream (fp, &len);

            if (fp != stdin)
            {
                fclose (fp);
            }

            if (text == NULL)
            {
                fprintf (stderr, "v32as: unable to read '%s'\n", files[i]);
                rc = 1;
                continue;
            }

            if (process_source (&a, pp, &opt, out, files[i], text, len))
            {
                rc = 1;
                pp_failed = 1;
            }
        }
    }

    // after a preprocessor error part of the program was never assembled: don't pile
    // "undefined label" complaints on top of it
    if (opt.preprocess || pp_failed)
    {
        if (out != stdout)
        {
            fclose (out);
        }
        else
        {
            fflush (stdout);
        }

        asm_free (&a);
        pp_free  (pp);
        free     (files);
        free     (mopts);
        free     (incdirs);
        free     (derived);

        return (rc);
    }

    asm_finish (&a);

    if (a.errors > 0)
    {
        rc = 1;
    }

    if (rc == 0 && opt.binary)
    {
        if (opt.standard && opt.verbose)
        {
            printf ("saving binary file\n");
        }

        if (opt.output != NULL)
        {
            out = fopen (opt.output, "wb");

            if (out == NULL)
            {
                fprintf (stderr, "v32as: cannot write '%s': %s\n", opt.output, strerror (errno));
                rc = 2;
            }
        }
        else if (isatty (STDOUT_FILENO))
        {
            fprintf (stderr, "v32as: refusing to write binary data to a terminal "
                             "(use -o FILE or redirect STDOUT)\n");
            rc = 2;
        }

        if (rc == 0 && opt.v32x && !opt.v32x_data_set)
        {
            v32x_find_t  f = { 0, 0 };

            pp_each_define (pp, v32x_find_data, &f);
            opt.v32x_data = f.found ? f.value : 0;
        }

        if (rc == 0 && (opt.object ? asm_write_object (&a, out)
                                   : opt.v32x ? write_v32x (&a, out, opt.origin, opt.v32x_data)
                                              : asm_write_vbin (&a, out)) != 0)
        {
            fprintf (stderr, "v32as: error writing the output\n");
            rc = 2;
        }
        else if (rc == 0 && opt.standard && opt.verbose)
        {
            printf ("output file created, size: %zu dwords = %zu bytes\n", a.nwords, a.nwords * 4);
        }
    }

    if (rc == 0 && opt.binary && opt.debug)
    {
        char   *path = malloc (strlen (opt.output) + 8);
        FILE   *dfp;

        sprintf (path, "%s.debug", opt.output);
        dfp = fopen (path, "w");

        if (dfp == NULL
            || (asm_cmap_loaded () ? asm_write_debug_map (&a, dfp, opt.debug_mode)
                                   : asm_write_debug (&a, dfp, opt.debug_mode)) != 0)
        {
            fprintf (stderr, "v32as: cannot write '%s': %s\n", path, strerror (errno));
            rc = 2;
        }

        if (dfp != NULL)
        {
            fclose (dfp);
        }

        free (path);
    }

    // FILE.vars: the ranges of the ;@var / ;@scope lines (only when there are some; an old file would mislead)
    if (rc == 0 && opt.binary && opt.debug)
    {
        char   *path = malloc (strlen (opt.output) + 8);

        sprintf (path, "%s.vars", opt.output);

        if (asm_vars_finish (&a) > 0)
        {
            FILE  *vfp = fopen (path, "w");

            if (vfp == NULL || asm_write_vars (&a, vfp, opt.debug_mode) != 0)
            {
                fprintf (stderr, "v32as: cannot write '%s': %s\n", path, strerror (errno));
                rc = 2;
            }

            if (vfp != NULL)
            {
                fclose (vfp);
            }
        }
        else
        {
            unlink (path);
        }

        free (path);
    }

    if (rc == 0 && opt.binary && opt.relocs)
    {
        FILE  *rfp = fopen (opt.relocs, "w");

        if (rfp == NULL || asm_write_relocs (&a, rfp) != 0)
        {
            fprintf (stderr, "v32as: cannot write '%s': %s\n", opt.relocs, strerror (errno));
            rc = 2;
        }

        if (rfp != NULL)
        {
            fclose (rfp);
        }
    }


    if (rc == 0 && opt.symbols)
    {
        symbols_t  ex = { 0 };
        FILE      *sfp = fopen (opt.symbols, "w");

        if (sfp == NULL)
        {
            fprintf (stderr, "v32as: cannot write '%s': %s\n", opt.symbols, strerror (errno));
            rc = 2;
        }
        else
        {
            if (opt.symbols_all)
            {
                pp_each_define (pp, collect_variable, &ex);
            }

            if (asm_write_symbols (&a, sfp, opt.symbols_all, local_name, pp, ex.v, ex.n) != 0)
            {
                fprintf (stderr, "v32as: cannot write '%s': %s\n", opt.symbols, strerror (errno));
                rc = 2;
            }

            fclose (sfp);
        }

        free (ex.v);
    }

    if (rc == 0 && opt.dep)
    {
        rc = write_deps (&opt, files, nfiles, pp, &a);
    }

    if (out != stdout)
    {
        fclose (out);
    }
    else
    {
        fflush (stdout);
    }

    if (rc == 0 && opt.standard && opt.verbose && opt.binary)
    {
        printf ("assembly successful\n");
    }

    asm_free (&a);
    pp_free  (pp);
    free     (files);
    free     (mopts);
    free     (incdirs);
    free     (derived);

    return (rc);
}
