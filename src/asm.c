//
// asm.c: the assembler core for v32as
//
// Holds everything that happens after parsing: the symbol table, the output word
// buffer, label fixups, instruction encoding, data directives, listings and the
// VBIN writer. The bison grammar (parser.y) calls into this file from its actions.
//
// Instruction word layout (MSB to LSB), as defined by the Vircon32 CPU specification:
//
//     opcode:6  immediate:1  register1:4  register2:4  addressing-mode:3  port:14
//
////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "v32as.h"
#include "parser.tab.h"

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

////////////////////////////////////////////////////////////////////////////////////////
//
// Source positions: lines seen by the scanner are lines of the preprocessor's output;
// the line map says where each one really came from
//
void  asm_resolve (const asm_t *a, int line, const char **file, int *src_line, int *expanded)
{
    if (a -> map != NULL && line >= 1 && (size_t) line <= a -> nmap)
    {
        *file     = a -> map[line - 1].file;
        *src_line = a -> map[line - 1].line;
        *expanded = a -> map[line - 1].expanded;
    }
    else
    {
        *file     = a -> cur_file ? a -> cur_file : "<input>";
        *src_line = line;
        *expanded = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Diagnostics
//
static void  report (asm_t *a, const loc_t *loc, const char *kind, const char *fmt,
                     va_list ap)
{
    const char  *nfile = NULL;
    int          nline = 0;

    fflush (a -> out);

    if (loc != NULL)
    {
        const char  *file;
        int          line;
        int          expanded;

        asm_resolve (a, loc -> first_line, &file, &line, &expanded);

        nfile = file;
        nline = line;

        // a column inside macro-expanded text means nothing to the programmer
        if (expanded || loc -> first_column <= 0)
        {
            fprintf (stderr, "%s:%d: %s: ", file, line, kind);
        }
        else
        {
            fprintf (stderr, "%s:%d:%d: %s: ", file, line, loc -> first_column, kind);
        }
    }
    else
    {
        fprintf (stderr, "v32as: %s: ", kind);
    }

    vfprintf (stderr, fmt, ap);
    fputc    ('\n', stderr);

    asm_cmap_note (nfile, nline);               // --map: the C line this assembly line came from
}

void  asm_error (asm_t *a, const loc_t *loc, const char *fmt, ...)
{
    va_list  ap;

    va_start (ap, fmt);
    report   (a, loc, "error", fmt, ap);
    va_end   (ap);

    a -> errors   = a -> errors + 1;
    a -> stmt_bad = 1;
}

void  asm_warn (asm_t *a, const loc_t *loc, const char *fmt, ...)
{
    va_list  ap;

    if (a -> no_warn)
    {
        return;
    }

    va_start (ap, fmt);
    report   (a, loc, "warning", fmt, ap);
    va_end   (ap);

    a -> warnings = a -> warnings + 1;
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Context life cycle
//
void  asm_init (asm_t *a)
{
    memset (a, 0, sizeof (*a));

    a -> tabcap  = 256;
    a -> tab     = xrealloc (NULL, a -> tabcap * sizeof (sym_t *));
    memset (a -> tab, 0, a -> tabcap * sizeof (sym_t *));

    a -> out     = stdout;
    a -> dbg     = stderr;
    a -> listing = 1;
}

void  asm_free (asm_t *a)
{
    sym_t  *s = a -> first;

    while (s != NULL)
    {
        sym_t  *next = s -> order;

        free (s -> name);
        free (s);
        s = next;
    }

    free (a -> tab);
    free (a -> words);
    free (a -> fix);
    free (a -> rel);
    free (a -> rec);
    free (a -> pend);

    while (a -> ndeps > 0)
    {
        free (a -> deps[--a -> ndeps]);
    }

    free (a -> deps);

    a -> pend  = NULL;
    a -> npend = a -> pendcap = 0;
    a -> deps  = NULL;
    a -> depcap = 0;

    a -> tab   = NULL;
    a -> words = NULL;
    a -> fix   = NULL;
    a -> rel   = NULL;
    a -> nrel  = a -> relcap = 0;
    a -> rec   = NULL;
    a -> first = a -> last = NULL;
}

void  asm_reset (asm_t *a)
{
    asm_t  saved = *a;

    asm_free (a);
    asm_init (a);

    a -> has_base    = saved.has_base;
    a -> base        = saved.base;
    a -> verbose     = saved.verbose;
    a -> step        = saved.step;
    a -> listing     = saved.listing;
    a -> interactive = saved.interactive;
    a -> standard    = saved.standard;
    a -> no_warn     = saved.no_warn;
    a -> incdirs     = saved.incdirs;
    a -> nincdirs    = saved.nincdirs;
    a -> out         = saved.out;
    a -> dbg         = saved.dbg;
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Symbol table: chained hash (FNV-1a), growing at a load factor of 1
//
static uint32_t  sym_hash (const char *s, size_t n)
{
    uint32_t  h = 2166136261u;
    size_t    i;

    for (i = 0; i < n; i++)
    {
        h ^= (unsigned char) s[i];
        h *= 16777619u;
    }

    return (h);
}

static void  sym_grow (asm_t *a)
{
    size_t   newcap = a -> tabcap * 2;
    sym_t  **newtab = xrealloc (NULL, newcap * sizeof (sym_t *));
    sym_t   *s;

    memset (newtab, 0, newcap * sizeof (sym_t *));

    for (s = a -> first; s != NULL; s = s -> order)
    {
        size_t  slot = sym_hash (s -> name, strlen (s -> name)) & (newcap - 1);

        s -> next      = newtab[slot];
        newtab[slot]   = s;
    }

    free (a -> tab);
    a -> tab    = newtab;
    a -> tabcap = newcap;
}

sym_t  *asm_sym_intern (asm_t *a, const char *name, size_t n)
{
    size_t  slot = sym_hash (name, n) & (a -> tabcap - 1);
    sym_t  *s;

    for (s = a -> tab[slot]; s != NULL; s = s -> next)
    {
        if (strlen (s -> name) == n && memcmp (s -> name, name, n) == 0)
        {
            return (s);
        }
    }

    if (a -> nsyms >= a -> tabcap)
    {
        sym_grow (a);
        slot = sym_hash (name, n) & (a -> tabcap - 1);
    }

    s              = xrealloc (NULL, sizeof (*s));
    s -> name      = xstrndup (name, n);
    s -> addr      = 0;
    s -> defined   = 0;
    s -> global    = 0;
    s -> external  = 0;
    s -> datasym   = 0;
    s -> gline     = 0;
    s -> gcol      = 0;
    s -> kind      = 'D';
    s -> pending   = 0;
    s -> gfile     = NULL;
    s -> order     = NULL;
    s -> next      = a -> tab[slot];
    a -> tab[slot] = s;

    if (a -> last != NULL)
    {
        a -> last -> order = s;
    }
    else
    {
        a -> first = s;
    }

    a -> last   = s;
    a -> nsyms  = a -> nsyms + 1;

    return (s);
}

static uint32_t  here (asm_t *a)
{
    return (a -> base + (uint32_t) a -> nwords);
}

void  asm_label_define (asm_t *a, sym_t *s, const loc_t *loc)
{
    // the lexer hands over "_name" (without the colon), already interned
    a -> line_active = 1;

    if (s -> defined)
    {
        asm_error (a, loc, "duplicate label '%s'", s -> name);
        return;
    }

    if (s -> external)
    {
        asm_error (a, loc, "label '%s' is declared extern: it is defined in another object", s -> name);
        return;
    }

    s -> addr    = here (a);
    s -> defined = 1;

    // code or data? the next thing assembled tells (see settle_labels)
    if (a -> npend == a -> pendcap)
    {
        a -> pendcap = a -> pendcap ? a -> pendcap * 2 : 64;
        a -> pend    = xrealloc (a -> pend, a -> pendcap * sizeof (sym_t *));
    }

    s -> pending = 1;
    a -> pend[a -> npend++] = s;

    if (a -> verbose)
    {
        fprintf (a -> dbg, "[label] %s at 0x%08X\n", s -> name, s -> addr);
    }

    if (a -> interactive && a -> listing)
    {
        fprintf (a -> out, "label:       %s = 0x%08X\n", s -> name, s -> addr);
    }
}

// the labels defined since the last statement are followed by code ('F') or data ('D')
static void  settle_labels (asm_t *a, char kind)
{
    while (a -> npend > 0)
    {
        sym_t  *s = a -> pend[--a -> npend];

        s -> kind    = kind;
        s -> pending = 0;
    }
}

void  asm_print_labels (asm_t *a, FILE *fp)
{
    const sym_t  *s;

    for (s = a -> first; s != NULL; s = s -> order)
    {
        if (s -> defined)
        {
            fprintf (fp, "0x%08X  %s\n", s -> addr, s -> name);
        }
        else
        {
            fprintf (fp, "<undefined>   %s\n", s -> name);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Output words and fixups
//
static void  ensure_words (asm_t *a, size_t extra)
{
    if (a -> nwords + extra > a -> cap)
    {
        size_t  newcap = a -> cap ? a -> cap : 1024;

        while (newcap < a -> nwords + extra)
        {
            newcap *= 2;
        }

        a -> words = xrealloc (a -> words, newcap * sizeof (uint32_t));
        a -> cap   = newcap;
    }
}

static size_t  emit_word (asm_t *a, uint32_t w)
{
    ensure_words (a, 1);
    a -> words[a -> nwords] = w;

    return (a -> nwords++);
}

// emit a word holding an immediate: a plain value, or a label (now or via a fixup)
static size_t  emit_imm (asm_t *a, const loc_t *loc, imm_t imm)
{
    size_t  index;

    if ((imm.kind == IMM_SYM || imm.reloc) && !a -> no_rel)
    {
        if (a -> nrel == a -> relcap)
        {
            a -> relcap = a -> relcap ? a -> relcap * 2 : 256;
            a -> rel    = xrealloc (a -> rel, a -> relcap * sizeof (rel_t));
        }

        a -> rel[a -> nrel].word   = (uint32_t) a -> nwords;
        a -> rel[a -> nrel].sym    = imm.kind == IMM_SYM ? imm.sym : NULL;
        a -> rel[a -> nrel].addend = imm.kind == IMM_SYM ? imm.addend : imm.bits;
        a -> nrel++;
    }

    if (imm.kind == IMM_SYM)
    {
        index = emit_word (a, imm.sym -> defined ? imm.sym -> addr + imm.addend : 0);

        if (!imm.sym -> defined)
        {
            if (a -> nfix == a -> fixcap)
            {
                a -> fixcap = a -> fixcap ? a -> fixcap * 2 : 256;
                a -> fix    = xrealloc (a -> fix, a -> fixcap * sizeof (fixup_t));
            }

            a -> fix[a -> nfix].word = (uint32_t) index;
            a -> fix[a -> nfix].sym  = imm.sym;
            a -> fix[a -> nfix].addend = imm.addend;
            {
                const char  *file;
                int          line;
                int          expanded;

                // keep the real position: the line map is gone by the time labels are checked
                asm_resolve (a, loc -> first_line, &file, &line, &expanded);

                a -> fix[a -> nfix].file = file;
                a -> fix[a -> nfix].loc  = *loc;
                a -> fix[a -> nfix].loc.first_line   = line;
                a -> fix[a -> nfix].loc.first_column = expanded ? 0 : loc -> first_column;
            }
            a -> nfix                = a -> nfix + 1;
        }
    }
    else
    {
        index = emit_word (a, imm.bits);
    }

    return (index);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Numeric helpers used by the grammar
//
num_t  num_negate (asm_t *a, const loc_t *loc, num_t n)
{
    if (n.is_float)
    {
        n.bits = n.bits ^ 0x80000000u;
    }
    else if (n.bits > 0x80000000u)
    {
        asm_error (a, loc, "number out of range for a negative 32-bit value");
    }
    else
    {
        n.bits = 0u - n.bits;
    }

    return (n);
}

imm_t  imm_from_sym (sym_t *s)
{
    imm_t  v = NOIMM;

    v.kind = IMM_SYM;
    v.sym  = s;

    return (v);
}

imm_t  imm_label_offset (asm_t *a, const loc_t *loc, sym_t *s, num_t off)
{
    imm_t  v = imm_from_sym (s);

    if (a -> standard)
    {
        asm_error (a, loc, "'label +/- integer' is not part of the official assembler's language");
    }
    else if (off.is_float)
    {
        asm_error (a, loc, "the offset added to a label must be an integer, not a float");
    }
    else
    {
        v.addend = off.bits;
    }

    return (v);
}

imm_t  imm_any (num_t n)
{
    imm_t  v = NOIMM;

    v.kind     = IMM_BITS;
    v.bits     = n.bits;
    v.is_float = n.is_float;

    return (v);
}

imm_t  imm_integer (asm_t *a, const loc_t *loc, num_t n)
{
    if (n.is_float)
    {
        asm_error (a, loc, "an integer is required here, not a float");
        n.bits     = 0;
        n.is_float = 0;
    }

    return (imm_any (n));
}

imm_t  imm_float (num_t n)
{
    if (!n.is_float)
    {
        // integer literals are converted to float values, as in the official assembler
        float  f = (float) (int32_t) n.bits;

        memcpy (&n.bits, &f, sizeof (f));
        n.is_float = 1;
    }

    return (imm_any (n));
}

imm_t  imm_address (asm_t *a, const loc_t *loc, imm_t v)
{
    if (v.kind == IMM_BITS && v.is_float)
    {
        asm_error (a, loc, "an address must be an integer or a label, not a float");
        v.bits     = 0;
        v.is_float = 0;
    }

    return (v);
}

uint32_t  port_from_num (asm_t *a, const loc_t *loc, num_t n)
{
    if (n.is_float)
    {
        asm_error (a, loc, "an I/O port must be a port name or an integer, not a float");
        return (0);
    }

    if (n.bits > PORT_MASK)
    {
        asm_warn (a, loc, "port number 0x%X does not fit in 14 bits; truncated", n.bits);
    }
    else if (!port_is_known (n.bits))
    {
        asm_warn (a, loc, "0x%03X is not a known I/O port", n.bits);
    }

    return (n.bits & PORT_MASK);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Listings (the human-readable output) and the verbose trace
//
static const char  *pending_label (const asm_t *a, size_t word)
{
    size_t  i;

    for (i = a -> stmt_fix; i < a -> nfix; i++)
    {
        if (a -> fix[i].word == word)
        {
            return (a -> fix[i].sym -> name);
        }
    }

    return (NULL);
}

static void  list_statement (asm_t *a, int is_data)
{
    size_t       i;
    const char  *name;

    if (!a -> listing)
    {
        return;
    }

    if (a -> has_base)
    {
        fprintf (a -> out, "OFFSET:      0x%08X\n", a -> base + (uint32_t) a -> stmt_start);
    }

    for (i = a -> stmt_start; i < a -> nwords; i++)
    {
        const char  *tag = is_data ? "data:       " :
                           (i == a -> stmt_start) ? "instruction:" : "immediate:  ";

        name = pending_label (a, i);

        if (name != NULL)
        {
            fprintf (a -> out, "%s %s\n", tag, name);
        }
        else
        {
            fprintf (a -> out, "%s 0x%08X\n", tag, a -> words[i]);
        }
    }

    fputs ("-----------------------\n", a -> out);
}

static void  trace_instruction (asm_t *a, unsigned opcode, unsigned r1, unsigned r2,
                                unsigned mode, unsigned port, int has_imm)
{
    uint32_t     w     = a -> words[a -> stmt_start];
    const char  *name  = pending_label (a, a -> stmt_start + 1);
    int          color = isatty (fileno (a -> dbg));
    int          bit;

    fprintf (a -> dbg, "======================================================\n");

    if (a -> has_base)
    {
        fprintf (a -> dbg, "OFFSET:        0x%08X\n", a -> base + (uint32_t) a -> stmt_start);
    }

    fprintf (a -> dbg, "mnemonic:      %s (opcode %u)\n", opcode_name (opcode), opcode);
    fprintf (a -> dbg, "imm_flag:      %d\n", has_imm);
    fprintf (a -> dbg, "register1:     %u\n", r1);
    fprintf (a -> dbg, "register2:     %u\n", r2);
    fprintf (a -> dbg, "addr mode:     %u\n", mode);
    fprintf (a -> dbg, "port:          0x%03X\n", port & PORT_MASK);
    fprintf (a -> dbg, "instruction:   ");

    // opcode | imm | reg1 | reg2 | mode | port, each field in its own color
    {
        static const int   width[6] = { 6, 1, 4, 4, 3, 14 };
        static const char *paint[6] = { "31", "33", "32", "36", "34", "35" };
        int                field    = 0;
        int                left     = width[0];

        for (bit = 31; bit >= 0; bit--)
        {
            if (color && left == width[field])
            {
                fprintf (a -> dbg, "\033[%sm", paint[field]);
            }

            fputc (((w >> bit) & 1) ? '1' : '0', a -> dbg);
            left--;

            if (left == 0)
            {
                if (color)
                {
                    fputs ("\033[0m", a -> dbg);
                }

                if (bit > 0)
                {
                    fputc (' ', a -> dbg);
                    field++;
                    left = width[field];
                }
            }
        }

        fputc ('\n', a -> dbg);
    }

    fprintf (a -> dbg, "INSTRUCTION:   0x%08X\n", w);

    if (has_imm)
    {
        if (name != NULL)
        {
            fprintf (a -> dbg, "IMMEDIATE:     %s (forward reference)\n", name);
        }
        else
        {
            fprintf (a -> dbg, "IMMEDIATE:     0x%08X\n", a -> words[a -> stmt_start + 1]);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Instruction encoding
//
void  asm_inst (asm_t *a, const loc_t *loc, unsigned opcode, unsigned r1, unsigned r2,
                unsigned mode, unsigned port, imm_t imm)
{
    int       has_imm = (imm.kind != IMM_NONE);
    uint32_t  word;

    a -> line_active = 1;

    if (a -> stmt_bad)
    {
        return;
    }

    settle_labels (a, 'F');

    word = ((uint32_t) (opcode & 0x3F) << 26)
         | ((uint32_t) (has_imm ? 1 : 0) << 25)
         | ((uint32_t) (r1 & 0xF)   << 21)
         | ((uint32_t) (r2 & 0xF)   << 17)
         | ((uint32_t) (mode & 0x7) << 14)
         | ((uint32_t) (port & PORT_MASK));

    a -> stmt_start = a -> nwords;
    a -> stmt_fix   = a -> nfix;

    // remember where this instruction came from (debug file)
    if (!a -> no_rec && a -> nrec == a -> reccap)
    {
        a -> reccap = a -> reccap ? a -> reccap * 2 : 256;
        a -> rec    = xrealloc (a -> rec, a -> reccap * sizeof (dbgrec_t));
    }
    if (!a -> no_rec)
    {
        const char  *file;
        int          line;
        int          expanded;

        asm_resolve (a, loc -> first_line, &file, &line, &expanded);

        a -> rec[a -> nrec].word = (uint32_t) a -> nwords;
        a -> rec[a -> nrec].file = file;
        a -> rec[a -> nrec].line = line;
        a -> nrec++;
    }

    emit_word (a, word);

    if (has_imm)
    {
        emit_imm (a, loc, imm);
    }

    if (a -> verbose)
    {
        trace_instruction (a, opcode, r1, r2, mode, port, has_imm);
    }

    list_statement (a, 0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Internal registers. IP, IR and IV cannot be named by the CPU, but their effects can be
// had indirectly. Every form below assembles to exactly one real instruction and uses no
// hidden scratch register.  A is the address of the expansion's first word.
//
//     MOV Rn, IP        MOV Rn, A+2          IP = address of the next instruction
//     MOV Rn, IR        MOV Rn, <that word>  IR = the instruction being executed
//     MOV Rn, IV        (rejected: IV would be the immediate that is being defined)
//     MOV IP, Rn|addr   JMP Rn|addr
//     MOV IR, Rn        MOV [A+2], Rn        overwrite the NEXT instruction word
//     MOV IV, Rn        MOV [A+3], Rn        overwrite the NEXT instruction's immediate
//
#define  OP_MOV  19u
#define  OP_JMP  ((unsigned) kw_lookup ("JMP", 3) -> value)

static imm_t  imm_bits (uint32_t v)
{
    imm_t  i = NOIMM;

    i.kind  = IMM_BITS;
    i.bits  = v;
    return i;
}

// a value computed from the instruction's own address: it moves with the code
static imm_t  imm_reloc (uint32_t v)
{
    imm_t  i = imm_bits (v);

    i.reloc = 1;
    return i;
}

void  asm_mov_sysreg_read (asm_t *a, const loc_t *loc, unsigned rn, unsigned sys)
{
    if (sys == SYS_IV)
    {
        asm_error (a, loc, "cannot read IV: it would be the immediate of this very instruction");
        return;
    }

    if (sys == SYS_IP)
    {
        if (!a -> has_base)
        {
            asm_warn (a, loc, "IP is relative to address 0 here; use --bios or --cart for real addresses");
        }
        asm_inst (a, loc, OP_MOV, rn, 0, 0, 0, imm_reloc (here (a) + 2));
        return;
    }

    // IR: the encoding of this MOV Rn, imm itself
    asm_inst (a, loc, OP_MOV, rn, 0, 0, 0,
              imm_bits ((OP_MOV << 26) | (1u << 25) | ((rn & 0xF) << 21)));
}

void  asm_mov_sysreg_write (asm_t *a, const loc_t *loc, unsigned sys, unsigned rn)
{
    if (sys == SYS_IP)
    {
        asm_inst (a, loc, OP_JMP, rn, 0, 0, 0, NOIMM);
        return;
    }

    if (a -> has_base && a -> base >= BIOS_BASE)
    {
        asm_warn (a, loc, "%s is patched at run time: this code must execute from RAM, "
                          "cartridge ROM is read-only", sys == SYS_IR ? "IR" : "IV");
    }

    asm_inst (a, loc, OP_MOV, 0, rn, 5, 0, imm_reloc (here (a) + (sys == SYS_IR ? 2 : 3)));
}

void  asm_mov_ip_value (asm_t *a, const loc_t *loc, imm_t target)
{
    asm_inst (a, loc, OP_JMP, 0, 0, 0, 0, imm_address (a, loc, target));
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Data directives: integer, float, pointer, string, zstring, datafile
//
void  asm_data_begin (asm_t *a)
{
    settle_labels (a, 'D');

    a -> line_active = 1;
    a -> stmt_start  = a -> nwords;
    a -> stmt_fix    = a -> nfix;
}

void  asm_data_word (asm_t *a, const loc_t *loc, imm_t v)
{
    if (!a -> stmt_bad)
    {
        emit_imm (a, loc, v);
    }
}

void  asm_data_string (asm_t *a, str_t s)
{
    size_t  i;

    // each character takes a whole word; the string is zero-terminated
    ensure_words (a, s.n + 1);

    for (i = 0; i < s.n; i++)
    {
        emit_word (a, (uint32_t) (unsigned char) s.p[i]);
    }

    emit_word (a, 0);
    free      (s.p);
}

// byte-packed string: four characters per word (first character in the low byte),
// followed by a zero byte; the final word is zero-padded
void  asm_data_zstring (asm_t *a, str_t s)
{
    size_t    i, nwords = (s.n + 1 + 3) / 4;
    uint32_t  w = 0;

    ensure_words (a, nwords);

    for (i = 0; i < nwords * 4; i++)
    {
        uint32_t  c = (i < s.n) ? (unsigned char) s.p[i] : 0;
        w |= c << (8 * (i % 4));
        if (i % 4 == 3)
        {
            emit_word (a, w);
            w = 0;
        }
    }

    free (s.p);
}

void  asm_add_dep (asm_t *a, const char *path)
{
    if (a -> ndeps == a -> depcap)
    {
        a -> depcap = a -> depcap ? a -> depcap * 2 : 16;
        a -> deps   = xrealloc (a -> deps, a -> depcap * sizeof (char *));
    }

    a -> deps[a -> ndeps++] = xstrndup (path, strlen (path));
}

// is item one of the ", "-separated entries of list?
static int  list_has (const char *list, const char *item)
{
    size_t  n = strlen (item);

    while (*list)
    {
        const char  *end = strstr (list, ", ");
        size_t       len = end ? (size_t) (end - list) : strlen (list);

        if (len == n && memcmp (list, item, n) == 0)
        {
            return (1);
        }

        list += len + (end ? 2 : 0);
    }

    return (0);
}

// try one candidate; remember where it was looked for (for the error message) and, when
// it opens, which file it was
static FILE  *try_data_file (const char *dir, size_t dirlen, const char *path, char **tried,
                             char **found)
{
    size_t  plen = strlen (path);
    char   *full = xrealloc (NULL, dirlen + 1 + plen + 1);
    FILE   *fp;

    memcpy (full, dir, dirlen);

    if (dirlen > 0 && dir[dirlen - 1] != '/')
    {
        full[dirlen++] = '/';
    }

    strcpy (full + dirlen, path);

    // the places tried, as "out/, ../data/, ." (a place is listed once)
    {
        const char  *place  = dirlen > 0 ? dir : ".";
        size_t       plc    = dirlen > 0 ? dirlen : 1;
        char        *label  = xrealloc (NULL, plc + 2);
        size_t       have   = *tried ? strlen (*tried) : 0;

        memcpy (label, place, plc);

        if (dirlen > 0 && label[plc - 1] != '/')
        {
            label[plc++] = '/';
        }

        label[plc] = 0;

        if (*tried == NULL || !list_has (*tried, label))
        {
            *tried = xrealloc (*tried, have + plc + 3);
            strcpy (*tried + have, have ? ", " : "");
            strcat (*tried, label);
        }

        free (label);
    }

    fp = fopen (full, "rb");

    if (fp != NULL)
    {
        *found = full;
    }
    else
    {
        free (full);
    }

    return (fp);
}

// open a data file. The first place that has it wins:
//   1. an absolute path
//   2. the directory of the file holding the 'datafile' line
//   3. each -I directory, in order (not under --standard)
//   4. the current directory
// *tried gets the list of places looked in (for the error message)
static FILE  *open_data_file (asm_t *a, const loc_t *loc, const char *path, char **tried,
                              char **found)
{
    const char  *cur;
    int          line;
    int          expanded;
    const char  *slash;
    FILE        *fp = NULL;
    int          i;

    *tried = NULL;
    *found = NULL;

    if (path[0] == '/')
    {
        fp = fopen (path, "rb");

        if (fp != NULL)
        {
            *found = xstrndup (path, strlen (path));
        }

        return (fp);
    }

    // the data file is looked up next to the file the "datafile" line really came from
    asm_resolve (a, loc -> first_line, &cur, &line, &expanded);
    slash = strrchr (cur, '/');

    if (slash != NULL)
    {
        fp = try_data_file (cur, (size_t) (slash - cur) + 1, path, tried, found);
    }

    for (i = 0; fp == NULL && i < a -> nincdirs && !a -> standard; i++)
    {
        fp = try_data_file (a -> incdirs[i], strlen (a -> incdirs[i]), path, tried, found);
    }

    if (fp == NULL)
    {
        fp = try_data_file ("", 0, path, tried, found);
    }

    return (fp);
}

void  asm_data_file (asm_t *a, const loc_t *loc, str_t path)
{
    FILE          *fp;
    unsigned char  chunk[4096];
    unsigned char  carry[4];
    size_t         carried = 0;
    size_t         got;
    char          *tried, *found;

    if (path.n == 0 || strlen (path.p) != path.n)
    {
        asm_error (a, loc, "invalid data file path");
        free (path.p);
        return;
    }

    fp = open_data_file (a, loc, path.p, &tried, &found);

    if (fp == NULL)
    {
        if (tried != NULL)
        {
            asm_error (a, loc, "cannot open data file \"%s\" (looked in %s)", path.p, tried);
        }
        else
        {
            asm_error (a, loc, "cannot open data file \"%s\"", path.p);
        }

        free (tried);
        free (path.p);
        return;
    }

    asm_add_dep (a, found);
    free (found);
    free (tried);

    // the file content becomes consecutive little-endian words
    while ((got = fread (chunk, 1, sizeof (chunk), fp)) > 0)
    {
        size_t  i;

        ensure_words (a, got / 4 + 1);

        for (i = 0; i < got; i++)
        {
            carry[carried++] = chunk[i];

            if (carried == 4)
            {
                emit_word (a, (uint32_t) carry[0]
                            | ((uint32_t) carry[1] <<  8)
                            | ((uint32_t) carry[2] << 16)
                            | ((uint32_t) carry[3] << 24));
                carried = 0;
            }
        }
    }

    fclose (fp);

    if (carried != 0)
    {
        asm_error (a, loc, "data file \"%s\" size must be a multiple of 4 bytes", path.p);
    }

    free (path.p);
}

void  asm_data_end (asm_t *a)
{
    if (!a -> stmt_bad)
    {
        list_statement (a, 1);
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// End of line: roll back statements that failed, pause in step mode, reset flags
//
static void  step_pause (asm_t *a)
{
    int  c;

    if (a -> verbose)
    {
        fputs ("\033[H\033[2J", a -> dbg);
    }

    fputs ("[step] press Enter to continue ", stderr);
    fflush (stderr);

    do
    {
        c = getchar ();
    }
    while (c != '\n' && c != EOF);

    if (c == EOF)
    {
        a -> step = 0;
    }
}

void  asm_end_line (asm_t *a)
{
    if (a -> stmt_bad)
    {
        // forget whatever a failed statement had already put in the buffers
        a -> nwords = a -> stmt_start;
        a -> nfix   = a -> stmt_fix;

        while (a -> nrec > 0 && a -> rec[a -> nrec - 1].word >= a -> stmt_start)
        {
            a -> nrec--;
        }
    }
    else if (a -> step && a -> line_active)
    {
        step_pause (a);
    }

    a -> lex_error   = 0;
    a -> stmt_bad    = 0;
    a -> line_active = 0;
    a -> stmt_start  = a -> nwords;
    a -> stmt_fix    = a -> nfix;
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Parsing entry point: assembles a block of text (a whole file, or one REPL line)
//
static int  parse_common_buf (asm_t *a, const char *name, char *buf, size_t n, char *owned,
                              int first_line, const srcloc_t *map, size_t nmap);

static int  parse_common (asm_t *a, const char *name, const char *text, size_t len,
                          int first_line, const srcloc_t *map, size_t nmap)
{
    char     *buf;
    size_t    n = len;

    // skip a UTF-8 byte order mark
    if (n >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB
               && (unsigned char) text[2] == 0xBF)
    {
        text += 3;
        n    -= 3;
    }

    // the scanner wants the text, a final newline and two terminating NUL bytes
    buf = xrealloc (NULL, n + 4);
    memcpy (buf, text, n);
    return (parse_common_buf (a, name, buf, n, buf, first_line, map, nmap));
}

// text the caller gave away (room for len + 4): scanned in place, no copy (on v32nix a copy of a linked program's text does not fit)
int  asm_parse_map_owned (asm_t *a, const char *name, char *text, size_t len, const srcloc_t *map, size_t nmap)
{
    char  *buf = text;

    if (len >= 3 && (unsigned char) buf[0] == 0xEF && (unsigned char) buf[1] == 0xBB && (unsigned char) buf[2] == 0xBF)
    {
        memmove (buf, buf + 3, len - 3);
        len -= 3;
    }
    return (parse_common_buf (a, name, buf, len, text, 1, map, nmap));
}

static int  parse_common_buf (asm_t *a, const char *name, char *buf, size_t n, char *owned,
                              int first_line, const srcloc_t *map, size_t nmap)
{
    unsigned  before = a -> errors;
    void     *scanner;

    if (n == 0 || buf[n - 1] != '\n')
    {
        buf[n++] = '\n';
    }

    buf[n]     = 0;
    buf[n + 1] = 0;

    a -> cur_file    = name;
    a -> lex_line    = first_line;
    a -> lex_col     = 1;
    a -> lex_error   = 0;
    a -> stmt_bad    = 0;
    a -> line_active = 0;
    a -> stmt_start  = a -> nwords;
    a -> stmt_fix    = a -> nfix;

    scanner = v32_lex_new (a);

    if (scanner == NULL)
    {
        free (owned);
        asm_error (a, NULL, "unable to create the scanner");
        return (1);
    }

    v32_lex_buffer (scanner, buf, n + 2);

    a -> map  = map;
    a -> nmap = nmap;

    if (yyparse (a, scanner) == 2)
    {
        asm_error (a, NULL, "parser ran out of memory");
    }

    a -> map  = NULL;
    a -> nmap = 0;

    v32_lex_free (scanner);
    free         (owned);

    return ((int) (a -> errors - before));
}

// plain text whose lines are its own (REPL lines, files assembled without preprocessing)
int  asm_parse (asm_t *a, const char *name, const char *text, size_t len, int first_line)
{
    return (parse_common (a, name, text, len, first_line, NULL, 0));
}

// preprocessor output: line N of the text is described by map[N-1]
int  asm_parse_map (asm_t *a, const char *name, const char *text, size_t len,
                    const srcloc_t *map, size_t nmap)
{
    return (parse_common (a, name, text, len, 1, map, nmap));
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Second pass: patch forward references, complain about labels never defined
//
int  asm_finish (asm_t *a)
{
    size_t  i;

    for (i = 0; i < a -> nfix; i++)
    {
        fixup_t  *f = &a -> fix[i];

        if (f -> sym -> defined)
        {
            a -> words[f -> word] = f -> sym -> addr + f -> addend;
        }
        else if (f -> sym -> external && a -> object)
        {
            a -> words[f -> word] = f -> addend;        // the linker adds the symbol's address
        }
        else
        {
            a -> cur_file = f -> file;
            asm_error (a, &f -> loc, "undefined label '%s'", f -> sym -> name);
        }
    }

    a -> nfix = 0;

    for (sym_t *s = a -> first; s != NULL; s = s -> order)
    {
        if (s -> global && !s -> defined)
        {
            loc_t  where = { s -> gline, s -> gcol, s -> gline, s -> gcol };

            a -> cur_file = s -> gfile;
            asm_error (a, &where, "global label '%s' is never defined", s -> name);
        }
    }

    return ((int) a -> errors);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// VBIN output: "V32-VBIN", the program size in words, then the words (little endian)
//
int  asm_write_vbin (asm_t *a, FILE *fp)
{
    size_t         total = 12 + a -> nwords * 4;
    unsigned char *buf   = xrealloc (NULL, total);
    size_t         i;
    int            ok;

    memcpy (buf, "V32-VBIN", 8);

    for (i = 0; i < 4; i++)
    {
        buf[8 + i] = (unsigned char) (((uint32_t) a -> nwords >> (8 * i)) & 0xFF);
    }

    for (i = 0; i < a -> nwords; i++)
    {
        uint32_t  w = a -> words[i];

        buf[12 + i * 4 + 0] = (unsigned char) (w         & 0xFF);
        buf[12 + i * 4 + 1] = (unsigned char) ((w >>  8) & 0xFF);
        buf[12 + i * 4 + 2] = (unsigned char) ((w >> 16) & 0xFF);
        buf[12 + i * 4 + 3] = (unsigned char) ((w >> 24) & 0xFF);
    }

    ok = (fwrite (buf, 1, total, fp) == total);
    free (buf);

    return (ok ? 0 : -1);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Debug file (-g): the same CSV the official assembler writes with "-g <mode>"
//
//     ADDRESS,file,line[,label]
//
// one line per instruction (data is not listed). ADDRESS is 0x%08X.
//   program: the instruction's memory address in words (0x20000000+n for a cartridge),
//            byte-for-byte what the official "-g program" prints
//   vbin:    byte offset in the .vbin file (0x0C header + 4 per word)
//   v32:     byte offset in the final .v32 cartridge file (0x8C header + 4 per word)
// The official assembler's vbin/v32 modes add that header to the *absolute* address times
// 4, which wraps around in 32 bits (e.g. 0x8000000C); here they give the real file offsets.
// The label column holds the alphabetically first label defined at that instruction.
//
typedef struct
{
    uint32_t     addr;
    const sym_t *sym;
}
labaddr_t;

static int  labaddr_cmp (const void *x, const void *y)
{
    const labaddr_t  *p = x, *q = y;

    if (p -> addr != q -> addr)
    {
        return (p -> addr < q -> addr ? -1 : 1);
    }

    return (strcmp (p -> sym -> name, q -> sym -> name));
}

static const sym_t  *label_at (const labaddr_t *v, size_t n, uint32_t addr)
{
    size_t  lo = 0, hi = n;

    while (lo < hi)
    {
        size_t  mid = (lo + hi) / 2;

        if (v[mid].addr < addr)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }

    return ((lo < n && v[lo].addr == addr) ? v[lo].sym : NULL);
}

static void  put_path (FILE *fp, const char *path)
{
    // like the official tool: backslashes become '/', "//" collapses, a leading "./" goes
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

int  asm_write_debug (asm_t *a, FILE *fp, dbgmode_t mode)
{
    uint32_t    base_off = (mode == DBG_V32) ? 0x8C : (mode == DBG_VBIN ? 0x0C : 0);
    uint32_t    unit     = (mode == DBG_PROGRAM) ? 1 : 4;
    labaddr_t  *v        = NULL;
    size_t      n = 0, i;
    sym_t      *s;

    for (s = a -> first; s != NULL; s = s -> order)
    {
        n += s -> defined ? 1 : 0;
    }

    v = xrealloc (NULL, (n + 1) * sizeof (labaddr_t));

    for (n = 0, s = a -> first; s != NULL; s = s -> order)
    {
        if (s -> defined)
        {
            v[n].addr = s -> addr;
            v[n].sym  = s;
            n++;
        }
    }

    qsort (v, n, sizeof (labaddr_t), labaddr_cmp);

    for (i = 0; i < a -> nrec; i++)
    {
        const dbgrec_t  *r   = &a -> rec[i];
        const sym_t     *lab = label_at (v, n, a -> base + r -> word);

        // program mode prints the address the CPU will see (this is what the official
        // assembler really prints); the file modes print the byte offset in the file
        fprintf (fp, "0x%08X,", mode == DBG_PROGRAM ? a -> base + r -> word
                                                    : base_off + r -> word * unit);
        put_path (fp, r -> file);
        fprintf (fp, ",%d", r -> line);

        if (lab != NULL)
        {
            fprintf (fp, ",%s", lab -> name);
        }

        fputc ('\n', fp);
    }

    free (v);

    return (ferror (fp) ? -1 : 0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Relocation list (-r): every word that holds an address, as "index,address" lines.
//
//     index    word index in the program (code and data), 0 is the first word
//     address  the value the word holds in this assembly (a label's address plus
//              any offset, or the IP-relative value), 0x%08X
//     kind     only when the source declares a datasym: C (code address: moves with the
//              load address) or D (data address: moves with the data base)
//
// The words listed are exactly those that were resolved from a label (or computed from
// the instruction's own address), never numbers that merely look like addresses; a loader
// that moves the program adds the difference between the new and the assembled base.
//
static int  rel_cmp (const void *x, const void *y)
{
    uint32_t  p = ((const rel_t *) x) -> word, q = ((const rel_t *) y) -> word;

    return (p < q ? -1 : (p > q ? 1 : 0));
}

int  asm_write_relocs (asm_t *a, FILE *fp)
{
    size_t  i;

    qsort (a -> rel, a -> nrel, sizeof (rel_t), rel_cmp);

    for (i = 0; i < a -> nrel; i++)
    {
        if (i > 0 && a -> rel[i].word == a -> rel[i - 1].word)
        {
            continue;
        }

        if (a -> rel[i].word < a -> nwords)
        {
            if (a -> has_datasym)
            {
                fprintf (fp, "%u,0x%08X,%c\n", a -> rel[i].word, a -> words[a -> rel[i].word],
                         (a -> rel[i].sym != NULL && a -> rel[i].sym -> datasym) ? 'D' : 'C');
            }
            else
            {
                fprintf (fp, "%u,0x%08X\n", a -> rel[i].word, a -> words[a -> rel[i].word]);
            }
        }
    }

    return (ferror (fp) ? -1 : 0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// The compiler's debug file (--map): v32cc -g writes
//
//     asmfile,asmline,cfile,cline[,function]
//
// It is loaded once, before assembling, and used three ways: -g merges it (each instruction of
// the assembler's own debug records is looked up by its (file,line) and the output names the C
// file and line, with the function as the label; instructions with no entry are left out), -C
// stores the C file and line in the object instead of the assembly ones, and every error or
// warning gets a note naming the C line the assembly line came from (the exact entry, or the
// nearest earlier one). Names are matched exactly, else by base name (see map_find_by_base).
//
typedef struct
{
    char  *afile;
    int    aline;
    char  *cfile;
    int    cline;
    char  *func;
}
mapent_t;

typedef struct
{
    mapent_t  *m;
    size_t     nm;
    size_t    *bidx;                    // indexes of m sorted by (base name, line)
    char      *name;                    // the map file's name, for messages
}
cmap_t;

static cmap_t  *the_cmap;

static int  mapent_cmp (const void *x, const void *y)
{
    const mapent_t  *p = x, *q = y;
    int              c = strcmp (p -> afile, q -> afile);

    if (c != 0)
    {
        return (c);
    }

    return (p -> aline < q -> aline ? -1 : (p -> aline > q -> aline ? 1 : 0));
}

static char  *norm_path (const char *path)
{
    char        *out = xrealloc (NULL, strlen (path) + 1), *q = out;
    const char  *p   = path;
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
        *q++       = c;
    }

    *q = 0;

    return (out);
}

static const char  *base_of (const char *path)
{
    const char  *slash = strrchr (path, '/');

    return (slash ? slash + 1 : path);
}

// the entries being indexed by baseidx_cmp (qsort has no context argument)
static const mapent_t  *map_sort_base;

static int  base_key_cmp (const char *fa, int la, const char *fb, int lb)
{
    int  c = strcmp (base_of (fa), base_of (fb));

    if (c != 0)
    {
        return (c);
    }

    return (la < lb ? -1 : (la > lb ? 1 : 0));
}

static int  baseidx_cmp (const void *x, const void *y)
{
    const mapent_t  *p = &map_sort_base[*(const size_t *) x], *q = &map_sort_base[*(const size_t *) y];

    return (base_key_cmp (p -> afile, p -> aline, q -> afile, q -> aline));
}

// how many trailing path components two normalized paths share
static int  shared_tail (const char *p, const char *q)
{
    const char  *pe = p + strlen (p), *qe = q + strlen (q);
    int          n  = 0;

    for (;;)
    {
        const char  *ps = pe, *qs = qe;

        while (ps > p && ps[-1] != '/')  { ps--; }
        while (qs > q && qs[-1] != '/')  { qs--; }

        if ((size_t) (pe - ps) != (size_t) (qe - qs) || memcmp (ps, qs, (size_t) (pe - ps)) != 0
            || pe == ps)
        {
            return (n);
        }

        n++;

        if (ps == p || qs == q)
        {
            return (n);
        }

        pe = ps - 1;
        qe = qs - 1;
    }
}

// the map entry for (file, line) when only the file's base name has to agree; among
// several candidates the one whose directories agree the most wins
static const mapent_t  *map_find_by_base (const cmap_t *c, const char *file, int line)
{
    const mapent_t  *m = c -> m;
    size_t           lo = 0, hi = c -> nm, k;
    int              best = -1;
    size_t           pick = 0;

    while (lo < hi)
    {
        size_t  mid = (lo + hi) / 2;

        if (base_key_cmp (m[c -> bidx[mid]].afile, m[c -> bidx[mid]].aline, file, line) < 0)  { lo = mid + 1; }
        else                                                                                   { hi = mid; }
    }

    for (k = lo; k < c -> nm && base_key_cmp (m[c -> bidx[k]].afile, m[c -> bidx[k]].aline, file, line) == 0; k++)
    {
        int  t = shared_tail (m[c -> bidx[k]].afile, file);

        if (t > best)
        {
            best = t;
            pick = c -> bidx[k];
        }
    }

    return (best >= 0 ? &m[pick] : NULL);
}

// the entry for exactly this assembly line, if there is one
static const mapent_t  *cmap_find (const cmap_t *c, const char *normfile, int line)
{
    mapent_t         key;
    const mapent_t  *hit;

    key.afile = (char *) normfile;
    key.aline = line;
    hit       = bsearch (&key, c -> m, c -> nm, sizeof (mapent_t), mapent_cmp);

    return (hit != NULL ? hit : map_find_by_base (c, normfile, line));
}

// the last entry at or before this line of the same file (by name, else by base name)
static const mapent_t  *cmap_before (const cmap_t *c, const char *normfile, int line)
{
    size_t  lo = 0, hi = c -> nm;

    while (lo < hi)                     // first entry greater than (file, line)
    {
        size_t  mid = (lo + hi) / 2;

        if (strcmp (c -> m[mid].afile, normfile) < 0
            || (strcmp (c -> m[mid].afile, normfile) == 0 && c -> m[mid].aline <= line))  { lo = mid + 1; }
        else                                                                              { hi = mid; }
    }

    if (lo > 0 && strcmp (c -> m[lo - 1].afile, normfile) == 0)
    {
        return (&c -> m[lo - 1]);
    }

    lo = 0;
    hi = c -> nm;

    while (lo < hi)                     // same through the base-name index
    {
        size_t  mid = (lo + hi) / 2;

        if (base_key_cmp (c -> m[c -> bidx[mid]].afile, c -> m[c -> bidx[mid]].aline, normfile, line) <= 0)  { lo = mid + 1; }
        else                                                                                                  { hi = mid; }
    }

    if (lo > 0 && strcmp (base_of (c -> m[c -> bidx[lo - 1]].afile), base_of (normfile)) == 0)
    {
        return (&c -> m[c -> bidx[lo - 1]]);
    }

    return (NULL);
}

int  asm_cmap_loaded (void)
{
    return (the_cmap != NULL);
}

void  asm_cmap_free (void)
{
    size_t  i;

    if (the_cmap == NULL)
    {
        return;
    }

    for (i = 0; i < the_cmap -> nm; i++)
    {
        free (the_cmap -> m[i].afile);
        free (the_cmap -> m[i].cfile);
        free (the_cmap -> m[i].func);
    }

    free (the_cmap -> m);
    free (the_cmap -> bidx);
    free (the_cmap -> name);
    free (the_cmap);
    the_cmap = NULL;
}

// read the map; returns the number of malformed lines (reported), or -1 if nothing could be read
int  asm_cmap_load (FILE *cmap, const char *cmap_name)
{
    cmap_t  *c = xrealloc (NULL, sizeof (*c));
    size_t   capm = 0, i;
    char     line[4096];
    int      bad = 0;

    memset (c, 0, sizeof (*c));
    c -> name = xstrndup (cmap_name, strlen (cmap_name));

    while (fgets (line, sizeof (line), cmap) != NULL)
    {
        char  *f[5] = { 0 }, *p = line;
        int    nf = 0;
        char  *end;
        int    aline;

        line[strcspn (line, "\r\n")] = 0;

        if (line[0] == 0 || line[0] == '#')
        {
            continue;
        }

        f[nf++] = p;

        while (nf < 5 && (p = strchr (p, ',')) != NULL)
        {
            *p++    = 0;
            f[nf++] = p;
        }

        if (nf < 4)
        {
            fprintf (stderr, "v32as: %s: malformed line (expected asmfile,asmline,cfile,cline[,function])\n",
                     cmap_name);
            bad++;
            continue;
        }

        aline = (int) strtol (f[1], &end, 10);

        if (end == f[1])
        {
            continue;                   // a header line
        }

        if (c -> nm == capm)
        {
            capm = capm ? capm * 2 : 256;
            c -> m = xrealloc (c -> m, capm * sizeof (mapent_t));
        }

        c -> m[c -> nm].aline = aline;
        c -> m[c -> nm].afile = norm_path (f[0]);
        c -> m[c -> nm].cfile = norm_path (f[2]);
        c -> m[c -> nm].cline = (int) strtol (f[3], NULL, 10);
        c -> m[c -> nm].func  = nf > 4 && f[4][0] ? xstrndup (f[4], strlen (f[4])) : NULL;
        c -> nm++;
    }

    qsort (c -> m, c -> nm, sizeof (mapent_t), mapent_cmp);

    // a second index by (base name, line), for assembly files the map names differently
    // ("main.asm" in the map, "out/kernel/main.asm" on the command line)
    c -> bidx = xrealloc (NULL, (c -> nm + 1) * sizeof (size_t));

    for (i = 0; i < c -> nm; i++)
    {
        c -> bidx[i] = i;
    }

    map_sort_base = c -> m;
    qsort (c -> bidx, c -> nm, sizeof (size_t), baseidx_cmp);

    asm_cmap_free ();
    the_cmap = c;

    return (bad);
}

// after a diagnostic about assembly line (file, line): where in the C source it came from
void  asm_cmap_note (const char *file, int line)
{
    char            *nf;
    const mapent_t  *hit;

    if (the_cmap == NULL || file == NULL || line < 1)
    {
        return;
    }

    nf  = norm_path (file);
    hit = cmap_find (the_cmap, nf, line);

    if (hit != NULL)
    {
        fprintf (stderr, "%s:%d: note: the assembly line above was generated from here%s%s\n",
                 hit -> cfile, hit -> cline, hit -> func != NULL ? ", in function " : "",
                 hit -> func != NULL ? hit -> func : "");
    }
    else if ((hit = cmap_before (the_cmap, nf, line)) != NULL)
    {
        fprintf (stderr, "%s:%d: note: the assembly line above has no source line of its own; "
                         "it follows the code generated from here%s%s\n",
                 hit -> cfile, hit -> cline, hit -> func != NULL ? ", in function " : "",
                 hit -> func != NULL ? hit -> func : "");
    }

    free (nf);
}

int  asm_write_debug_map (asm_t *a, FILE *fp, dbgmode_t mode)
{
    const cmap_t  *c = the_cmap;
    uint32_t       base_off = (mode == DBG_V32) ? 0x8C : (mode == DBG_VBIN ? 0x0C : 0);
    uint32_t       unit     = (mode == DBG_PROGRAM) ? 1 : 4;
    size_t         matched = 0, i;

    for (i = 0; i < a -> nrec; i++)
    {
        const dbgrec_t  *r = &a -> rec[i];
        char            *file = norm_path (r -> file);
        const mapent_t  *hit = cmap_find (c, file, r -> line);

        if (hit != NULL)
        {
            matched++;

            fprintf (fp, "0x%08X,%s,%d", mode == DBG_PROGRAM ? a -> base + r -> word
                                                             : base_off + r -> word * unit,
                     hit -> cfile, hit -> cline);

            if (hit -> func != NULL)
            {
                fprintf (fp, ",%s", hit -> func);
            }

            fputc ('\n', fp);
        }

        free (file);
    }

    // some misses are normal (hand-written lines have no C line); all of them is not
    if (a -> nrec > 0 && matched == 0 && !a -> no_warn)
    {
        const char  *last = NULL;
        int          shown = 0;

        fprintf (stderr, "v32as: warning: --map %s: no entry matches an instruction of %s "
                         "(names in the map:", c -> name, a -> rec[0].file);

        for (i = 0; i < c -> nm && shown < 4; i++)
        {
            if (last == NULL || strcmp (last, c -> m[i].afile) != 0)
            {
                fprintf (stderr, "%s %s", shown ? "," : "", c -> m[i].afile);
                last = c -> m[i].afile;
                shown++;
            }
        }

        fprintf (stderr, "%s)\n", c -> nm == 0 ? " none" : (shown >= 4 ? ", ..." : ""));
    }

    return (ferror (fp) ? -1 : 0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Symbol table (--symbols): "address,name,kind", sorted by address
//
//     kind  F  a label followed by code        D  a label followed by data (or nothing)
//           V  a compiler variable (a %define of a global_* number)
//
typedef struct
{
    const char  *name;
    uint32_t     addr;
    char         kind;
}
symrow_t;

static int  symrow_cmp (const void *x, const void *y)
{
    const symrow_t  *p = x, *q = y;

    if (p -> addr != q -> addr)
    {
        return (p -> addr < q -> addr ? -1 : 1);
    }

    return (strcmp (p -> name, q -> name));
}

int  asm_write_symbols (asm_t *a, FILE *fp, int all,
                        const char *(*local) (void *ud, const char *name), void *local_ud,
                        const symline_t *extra, size_t nextra)
{
    const sym_t  *s;
    symrow_t     *v = xrealloc (NULL, (a -> nsyms + nextra + a -> nrel + 1) * sizeof (symrow_t));
    char        **own = NULL;                   // names made here ("datasym+N"), freed below
    size_t        n = 0, i, nown = 0;

    for (s = a -> first; s != NULL; s = s -> order)
    {
        const char  *ln;

        if (!s -> defined || s -> external || s -> datasym)     // a datasym is a base, not an address
        {
            continue;
        }

        ln = local ? local (local_ud, s -> name) : NULL;

        if (ln != NULL && !all)
        {
            continue;
        }

        v[n].name = ln != NULL ? ln : s -> name;
        v[n].addr = s -> addr;
        v[n].kind = s -> kind;
        n++;
    }

    for (i = 0; all && i < nextra; i++)
    {
        v[n].name = extra[i].name;
        v[n].addr = extra[i].addr;
        v[n].kind = extra[i].kind;
        n++;
    }

    // --symbols-all: the data variables the program refers to as "DATASYM+N" (what v32cc --pic-data
    // writes for a global): one line per distinct N, kind V, the address column holding the offset N
    for (i = 0; all && i < a -> nrel; i++)
    {
        const sym_t  *d = a -> rel[i].sym;
        size_t        j;
        char         *nm;

        if (d == NULL || !d -> datasym)
        {
            continue;
        }

        nm = xrealloc (NULL, strlen (d -> name) + 12);
        sprintf (nm, "%s+%u", d -> name, (unsigned) a -> rel[i].addend);

        for (j = 0; j < nown && strcmp (own[j], nm) != 0; j++)      // the same variable again: once is enough
        {
        }

        if (j < nown)
        {
            free (nm);
            continue;
        }

        own = xrealloc (own, (nown + 1) * sizeof (char *));
        own[nown++] = nm;
        v[n].name   = nm;
        v[n].addr   = a -> rel[i].addend;
        v[n].kind   = 'V';
        n++;
    }

    qsort (v, n, sizeof (symrow_t), symrow_cmp);

    for (i = 0; i < n; i++)
    {
        fprintf (fp, "0x%08X,%s,%c\n", v[i].addr, v[i].name, v[i].kind);
    }

    for (i = 0; i < nown; i++)
    {
        free (own[i]);
    }

    free (own);
    free (v);

    return (ferror (fp) ? -1 : 0);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// 'global' and 'extern'
//
void  asm_sym_declare (asm_t *a, const loc_t *loc, sym_t *s, int external)
{
    a -> line_active = 1;

    if (external && s -> defined)
    {
        asm_error (a, loc, "label '%s' is defined here, it cannot be extern", s -> name);
    }
    else if (external && s -> global)
    {
        asm_error (a, loc, "label '%s' cannot be both global and extern", s -> name);
    }
    else if (!external && s -> external)
    {
        asm_error (a, loc, "label '%s' cannot be both global and extern", s -> name);
    }
    else if (external)
    {
        s -> external = 1;
    }
    else if (!s -> global)
    {
        const char  *file;
        int          line, expanded;

        asm_resolve (a, loc -> first_line, &file, &line, &expanded);

        s -> global = 1;
        s -> gfile  = file;
        s -> gline  = line;
        s -> gcol   = expanded ? 0 : loc -> first_column;
    }
}

// datasym NAME: NAME is the base of the program's data, with the value 0. A reference such as "NAME + 17" is a word
// that holds the number 17 and is listed by -r as a data relocation (kind D): a loader that places the data at B adds B.
void  asm_sym_datasym (asm_t *a, const loc_t *loc, sym_t *s)
{
    a -> line_active = 1;

    if (s -> defined && !s -> datasym)
    {
        asm_error (a, loc, "label '%s' is already defined, it cannot be a datasym", s -> name);
    }
    else if (s -> global || s -> external)
    {
        asm_error (a, loc, "label '%s' cannot be a datasym and global or extern", s -> name);
    }
    else
    {
        s -> datasym = 1;
        s -> defined = 1;
        s -> addr    = 0;
        a -> has_datasym = 1;
    }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Relocatable object (-C): see docs/OBJECT.md. All numbers are 32-bit little endian.
//
typedef struct
{
    unsigned char  *p;
    size_t          n;
    size_t          cap;
}
buf_t;

static void  buf_put (buf_t *b, const void *data, size_t n)
{
    if (b -> n + n > b -> cap)
    {
        b -> cap = (b -> n + n) * 2 + 256;
        b -> p   = xrealloc (b -> p, b -> cap);
    }

    memcpy (b -> p + b -> n, data, n);
    b -> n += n;
}

static void  buf_u32 (buf_t *b, uint32_t v)
{
    unsigned char  c[4] = { (unsigned char) v, (unsigned char) (v >> 8),
                            (unsigned char) (v >> 16), (unsigned char) (v >> 24) };

    buf_put (b, c, 4);
}

// string table: returns the offset of a new copy of s
static uint32_t  str_add (buf_t *t, const char *s)
{
    uint32_t  off = (uint32_t) t -> n;

    buf_put (t, s, strlen (s) + 1);

    return (off);
}

int  asm_write_object (asm_t *a, FILE *fp)
{
    buf_t         out = { 0 }, str = { 0 }, body = { 0 };
    const char  **files = NULL;
    uint32_t     *fileoff = NULL;
    size_t        nfiles = 0, nsyms = 0, i, j;
    uint32_t      nrels = 0, ndbg = 0;
    sym_t        *s;
    int           ok;
    int           v2 = a -> has_datasym || asm_cmap_loaded ();      // flag 16, 4-word debug records

    for (s = a -> first; s != NULL; s = s -> order)
    {
        nsyms++;
    }

    // symbol index = position in the definition/reference order
    sym_t  **tab = xrealloc (NULL, (nsyms + 1) * sizeof (sym_t *));

    for (i = 0, s = a -> first; s != NULL; s = s -> order)
    {
        tab[i++] = s;
    }

    for (i = 0; i < a -> nwords; i++)
    {
        buf_u32 (&body, a -> words[i]);
    }

    for (i = 0; i < nsyms; i++)
    {
        uint32_t  flags = (tab[i] -> defined ? 1 : 0) | (tab[i] -> global ? 2 : 0)
                        | (tab[i] -> external ? 4 : 0)
                        | (tab[i] -> defined && tab[i] -> kind == 'F' ? 8 : 0)
                        | (tab[i] -> datasym ? 16 : 0);

        buf_u32 (&body, str_add (&str, tab[i] -> name));
        buf_u32 (&body, tab[i] -> defined ? tab[i] -> addr : 0);
        buf_u32 (&body, flags);
    }

    // relocations: one per word that holds an address, in word order
    qsort (a -> rel, a -> nrel, sizeof (rel_t), rel_cmp);

    for (i = 0; i < a -> nrel; i++)
    {
        uint32_t  idx = 0xFFFFFFFFu;

        if ((i > 0 && a -> rel[i].word == a -> rel[i - 1].word) || a -> rel[i].word >= a -> nwords)
        {
            continue;
        }

        if (a -> rel[i].sym != NULL)
        {
            for (j = 0; j < nsyms; j++)
            {
                if (tab[j] == a -> rel[i].sym)
                {
                    idx = (uint32_t) j;
                    break;
                }
            }
        }

        buf_u32 (&body, a -> rel[i].word);
        buf_u32 (&body, idx);
        buf_u32 (&body, a -> rel[i].addend);
        nrels++;
    }

    // debug records; file names are stored once each. With --map the C file and line replace the
    // assembly ones (instructions the map does not know are left out, as in -g --map) and the
    // function name travels along as a fourth word
    for (i = 0; i < a -> nrec; i++)
    {
        const char      *file = a -> rec[i].file;
        int              line = a -> rec[i].line;
        const mapent_t  *hit = NULL;
        uint32_t         func = 0xFFFFFFFFu;

        if (asm_cmap_loaded ())
        {
            char  *nf = norm_path (a -> rec[i].file);

            hit = cmap_find (the_cmap, nf, a -> rec[i].line);
            free (nf);

            if (hit == NULL)
            {
                continue;
            }

            file = hit -> cfile;
            line = hit -> cline;
        }

        for (j = 0; j < nfiles && strcmp (files[j], file) != 0; j++)
        {
        }

        if (j == nfiles)
        {
            files   = xrealloc (files, (nfiles + 1) * sizeof (char *));
            fileoff = xrealloc (fileoff, (nfiles + 1) * sizeof (uint32_t));
            files[nfiles]   = file;
            fileoff[nfiles] = str_add (&str, file);
            nfiles++;
        }

        if (hit != NULL && hit -> func != NULL)
        {
            func = str_add (&str, hit -> func);
        }

        buf_u32 (&body, a -> rec[i].word);
        buf_u32 (&body, fileoff[j]);
        buf_u32 (&body, (uint32_t) line);

        if (v2)
        {
            buf_u32 (&body, func);
        }

        ndbg++;
    }

    buf_put (&out, "V32-VOBJ", 8);
    buf_u32 (&out, v2 ? 2 : 1);                         // format version
    buf_u32 (&out, (uint32_t) a -> nwords);
    buf_u32 (&out, (uint32_t) nsyms);
    buf_u32 (&out, nrels);
    buf_u32 (&out, ndbg);
    buf_u32 (&out, (uint32_t) str.n);
    buf_put (&out, body.p, body.n);
    buf_put (&out, str.p, str.n);

    ok = (fwrite (out.p, 1, out.n, fp) == out.n);

    free (out.p);
    free (str.p);
    free (body.p);
    free (files);
    free (fileoff);
    free (tab);

    return (ok ? 0 : -1);
}
