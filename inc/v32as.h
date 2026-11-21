//
// v32as.h: shared types and prototypes for v32as, the C implementation of the
// Vircon32 assembler (flex for lexing, bison for parsing)
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef V32AS_H
#define V32AS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "pp.h"
#include "heapstat.h"

#define  VERSION            "20261121-dev"

#define  BIOS_BASE          0x10000000u
#define  CART_BASE          0x20000000u
#define  PORT_MASK          0x3FFFu          // the port field of an instruction is 14 bits

////////////////////////////////////////////////////////////////////////////////////////
//
// Source locations (compatible with bison's default location members)
//
typedef struct
{
    int  first_line;
    int  first_column;
    int  last_line;
    int  last_column;
}
loc_t;

////////////////////////////////////////////////////////////////////////////////////////
//
// Instruction operand classes: each one corresponds to a family of grammar rules in
// parser.y. The table in tables.c assigns one class to each of the 64 opcodes.
//
typedef enum
{
    OC_NONE,        // HLT WAIT RET MOVS SETS
    OC_R,           // one register
    OC_RI,          // register, register|integer
    OC_RF,          // register, register|float (integer literals are converted)
    OC_RR,          // register, register (ATAN2, POW)
    OC_JMP,         // JMP CALL: register|address
    OC_CJMP,        // JT JF:    register, register|address
    OC_MOV,         // the eight MOV addressing modes
    OC_LEA,         // LEA register, [register (+offset)]
    OC_IN,          // IN register, port
    OC_OUT          // OUT port, register|value
}
opclass_t;

typedef enum
{
    KW_OPCODE,
    KW_DIRECTIVE,
    KW_REGISTER,
    KW_PORT,
    KW_PORTVALUE,
    KW_CONSTANT,
    KW_SYSREG                       // IP IR IV: internal registers (MOV only)
}
kw_kind_t;

typedef struct
{
    const char *name;
    kw_kind_t   kind;
    int         cls;                // opclass_t for opcodes; directive id for directives
    uint32_t    value;              // opcode number, register index, port, constant
}
kw_t;

typedef enum
{
    DIR_INTEGER,
    DIR_FLOAT,
    DIR_STRING,                     // one character per word, zero-terminated
    DIR_ZSTRING,                    // byte-packed (4 per word, little-endian), zero-terminated
    DIR_POINTER,
    DIR_DATAFILE,
    DIR_GLOBAL,
    DIR_EXTERN,
    DIR_DATASYM
}
directive_t;

////////////////////////////////////////////////////////////////////////////////////////
//
// Values produced by the lexer and used by the grammar
//
typedef struct
{
    uint32_t  bits;                 // the 32-bit pattern (IEEE-754 for floats)
    int       is_float;
}
num_t;

typedef struct sym
{
    char         *name;
    uint32_t      addr;             // absolute address (valid once defined)
    int           defined;
    int           global;           // 'global': visible to the linker
    int           external;         // 'extern': defined in another object
    int           datasym;          // 'datasym': the base of the program's data (value 0); references are data relocations
    const char   *gfile;            // where 'global' declared it (for diagnostics)
    int           gline;
    int           gcol;
    char          kind;             // what follows the label: 'F' code, 'D' data (for --symbols)
    int           pending;          // label defined, kind not yet known
    struct sym   *next;             // hash chain
    struct sym   *order;            // definition/reference order list
}
sym_t;

typedef struct
{
    enum { IMM_NONE, IMM_BITS, IMM_SYM }  kind;
    uint32_t  bits;
    sym_t    *sym;
    int       is_float;
    uint32_t  addend;               // IMM_SYM: constant added to the label's address
    int       reloc;                // IMM_BITS: the value is an address derived from the location
}
imm_t;

#define  NOIMM  ((imm_t) { IMM_NONE, 0, NULL, 0, 0, 0 })

typedef struct
{
    char    *p;
    size_t   n;
}
str_t;

////////////////////////////////////////////////////////////////////////////////////////
//
// Assembler context
//
typedef struct
{
    uint32_t     word;              // index into the output word array
    sym_t       *sym;
    uint32_t     addend;
    const char  *file;
    loc_t        loc;
}
fixup_t;

// one record per assembled instruction, for the -g debug file
typedef struct
{
    uint32_t     word;              // index of the instruction's first word in the output
    const char  *file;
    int          line;
    const char  *mfile;             // ;@src: the source file/line of the marker this instruction is the first
    int          mline;             //   one after (mfile NULL: the instruction starts no marker; "-" and 0: the
    const char  *mfunc;             //   marker was ";@src -"); mfunc: the function label in effect, or NULL
}
dbgrec_t;

// The ";@..." comment lines of V32DBG, in source order (the scanner hands them over; they are used when the
// instruction that follows them is assembled)
typedef enum { EV_SRC, EV_NONE, EV_VAR, EV_SCOPE_OPEN, EV_SCOPE_CLOSE, EV_FN_END, EV_HOME } evkind_t;

typedef struct
{
    int          pline;             // line of the assembler's input text (the preprocessor's output)
    evkind_t     kind;
    const char  *file;              // EV_SRC: FILE:LINE; EV_NONE (";@src -", or a malformed one): "-" and 0
    int          line;
    const char  *name;              // EV_VAR: NAME (EV_FN_END: the function name, without "__function_"; EV_HOME: the new home TO)
    const char  *loc;               // EV_VAR: loc= (or "-"), type= (or NULL), the other words joined by '+' (or NULL)
    const char  *type;
    const char  *flags;
}
srcev_t;

// ";@var" and ";@scope" resolved to addresses (word indexes of the assembled program), for FILE.vars
typedef struct
{
    int          is_scope;
    uint32_t     start, end;        // [start, end) in words; end 0xFFFFFFFF while still open
    const char  *name;              // vars only
    const char  *loc, *type, *flags;
    int          depth;             // 0: the function's own scope, 1: a block in it, ...
    const char  *func;              // the function it is in, or NULL
    uint32_t     scope_id;
    uint32_t     seq;
    int          skip;              // empty range: not written
}
varrec_t;

// the open functions and ";@scope {" blocks, innermost last
typedef struct
{
    int          is_func;
    uint32_t     id, start;
    const char  *name;              // functions
}
scopeitem_t;

typedef enum { DBG_PROGRAM, DBG_VBIN, DBG_V32 } dbgmode_t;

typedef struct
{
    uint32_t     word;              // index of the word in the output
    sym_t       *sym;               // the label it refers to (NULL: relative to the load address)
    uint32_t     addend;            // label offset; for sym == NULL the value itself
}
rel_t;

typedef struct asm_ctx
{
    // configuration
    int          has_base;          // BIOS or CART mode: addresses are meaningful
    uint32_t     base;
    int          verbose;
    int          step;
    int          listing;           // print the human-readable listing
    int          interactive;       // true inside the REPL
    FILE        *out;               // listing stream
    FILE        *dbg;               // verbose (debug) stream

    // output
    uint32_t    *words;
    size_t       nwords;
    size_t       cap;

    // symbols and forward references
    sym_t      **tab;
    size_t       tabcap;
    size_t       nsyms;
    sym_t       *first;
    sym_t       *last;
    fixup_t     *fix;
    size_t       nfix;
    size_t       fixcap;

    // words holding an address (label references, IP-relative values), for -r and objects
    rel_t       *rel;
    size_t       nrel;
    size_t       relcap;
    int          has_datasym;       // a 'datasym' was declared: -r lists the kind of each relocation

    // instruction -> source line records (for -g)
    dbgrec_t    *rec;
    size_t       nrec;
    size_t       reccap;

    // V32DBG ";@..." comment lines (see asm_annotation), consumed in order as the instructions after them are assembled
    srcev_t     *ev;
    size_t       nev;
    size_t       evcap;
    size_t       ev_next;           // events before this index have been applied
    size_t       srcmk_seen;        // ;@src lines met (well formed or ";@src -"): non-zero switches -g to marker mode
    int          pend_has;          // a ;@src applies to the next instruction
    const char  *pend_file;
    int          pend_line;
    char       **mkstr;             // strings of the events (kept for the life of the assembly)
    size_t       nmkstr, mkstrcap;
    scopeitem_t *stk;               // open functions and blocks
    size_t       nstk, stkcap;
    uint32_t     next_id;
    varrec_t    *vars;              // ;@var / ;@scope as address ranges
    size_t       nvars, varcap;
    size_t      *open;              // indexes into vars of the ones still open
    size_t       nopen, opencap;
    int          ev_done;
    int          src_markers;       // read ;@ lines (off under --standard)
    int          warn_markers;      // -Wmarkers: warn about a malformed ;@src, ;@var or ;@scope
    const char  *lang;              // ;@lang X (the first one): the producer's language, or NULL
    const char *(*define_of) (void *ud, const char *name);      // a %define'd name's replacement text (NULL: none); for ;@var loc=
    void        *define_ud;

    // state
    const char  *cur_file;
    unsigned     errors;
    unsigned     warnings;
    int          lex_error;         // lexer already reported a problem on this line
    int          line_active;       // the current line produced a label or statement
    int          stmt_bad;          // an error was reported inside the current statement
    size_t       stmt_start;        // first word of the statement being assembled
    size_t       stmt_fix;          // fixup count at statement start
    int          lex_line;          // lexer bookkeeping
    int          lex_col;

    // preprocessor line map for the text being parsed (NULL: lines are the source's own)
    const srcloc_t  *map;
    size_t           nmap;

    // where 'datafile' looks after the source file's own directory (-I, in order)
    const char **incdirs;
    int          nincdirs;

    // files read through 'datafile' (for -MD)
    char       **deps;
    size_t       ndeps;
    size_t       depcap;

    // labels waiting to learn whether code or data follows them (for --symbols)
    sym_t      **pend;
    size_t       npend;
    size_t       pendcap;

    int          object;            // -C: write a relocatable object (v32ob), base 0
    int          no_rel;            // do not record the address words (rel): no -r, -C or --symbols-all wants them (memory on v32nix)
    int          no_rec;            // do not record instruction lines (rec): no -g or -C wants them
    int          standard;          // strict official-assembler language (--standard)
    int          no_warn;           // -w
}
asm_t;

////////////////////////////////////////////////////////////////////////////////////////
//
// tables.c: keyword lookup (opcodes, directives, registers, ports, constants)
//
const kw_t  *kw_lookup      (const char *s, size_t n);      // case-insensitive
const char  *opcode_name    (unsigned opcode);
int          port_is_known  (unsigned port);

////////////////////////////////////////////////////////////////////////////////////////
//
// asm.c: the assembler core
//
void    asm_init            (asm_t *a);
void    asm_free            (asm_t *a);
void    asm_reset           (asm_t *a);
int     asm_parse           (asm_t *a, const char *name, const char *text, size_t len,
                             int first_line);
int     asm_parse_map       (asm_t *a, const char *name, const char *text, size_t len,
                             const srcloc_t *map, size_t nmap);
void    asm_resolve         (const asm_t *a, int line, const char **file, int *src_line,
                             int *expanded);
int     asm_finish          (asm_t *a);
int     asm_write_vbin      (asm_t *a, FILE *fp);
int     asm_parse_map_owned (asm_t *a, const char *name, char *text, size_t len, const srcloc_t *map, size_t nmap);  // takes text (room for len + 4)
int     asm_write_debug     (asm_t *a, FILE *fp, dbgmode_t mode);
int     asm_write_object    (asm_t *a, FILE *fp);
void    asm_sym_declare     (asm_t *a, const loc_t *loc, sym_t *s, int external);
void    asm_sym_datasym     (asm_t *a, const loc_t *loc, sym_t *s);
int     asm_write_relocs    (asm_t *a, FILE *fp);
int     asm_write_debug_map (asm_t *a, FILE *fp, dbgmode_t mode);   // needs asm_cmap_load first
int     asm_cmap_load       (FILE *cmap, const char *cmap_name);    // --map: returns the malformed lines
void    asm_cmap_free       (void);
int     asm_cmap_loaded     (void);
void    asm_annotation      (asm_t *a, const loc_t *loc, const char *text, size_t n);   // a ;@src, ;@var, ;@scope or ;@fn comment line
size_t  asm_vars_finish     (asm_t *a);                             // close what is open; the number of FILE.vars records
int     asm_write_vars      (asm_t *a, FILE *fp, dbgmode_t mode);   // FILE.vars: var and scope ranges (after asm_vars_finish)
const varrec_t  *asm_var_records (asm_t *a, size_t *n);
const char  *asm_marker_lang (const asm_t *a, int for_debug);      // the "# lang X" language to record (NULL: none)
const char  *asm_var_loc    (asm_t *a, const varrec_t *r, int final, dbgmode_t mode, char *buf, size_t n);   // the loc= text to write             // the records, sorted, empty ranges left out (for the object writer)
void    asm_cmap_note       (const char *file, int line);           // note: the C line of an assembly line
void    asm_print_labels    (asm_t *a, FILE *fp);
void    asm_add_dep         (asm_t *a, const char *path);

// --symbols: one "address,name[,kind]" line per label, sorted by address. 'local' maps the
// assembler-level name of a file-local label to its "function.local" spelling (NULL: the
// name is not a local label); 'extra' are further lines (the compiler's variables).
typedef struct
{
    const char  *name;
    uint32_t     addr;
    char         kind;
}
symline_t;

int     asm_write_symbols   (asm_t *a, FILE *fp, int all,
                             const char *(*local) (void *ud, const char *name), void *local_ud,
                             const symline_t *extra, size_t nextra);

// FILE.vbin.sym (-g): kind,name,address,words,cfile,cline with final addresses ('extra': V lines, kind '1' = one word)
int     asm_write_vsym      (asm_t *a, FILE *fp, const char *(*local) (void *ud, const char *name), void *local_ud,
                             const symline_t *extra, size_t nextra);

void    asm_error           (asm_t *a, const loc_t *loc, const char *fmt, ...)
                            __attribute__ ((format (printf, 3, 4)));
void    asm_warn            (asm_t *a, const loc_t *loc, const char *fmt, ...)
                            __attribute__ ((format (printf, 3, 4)));

sym_t  *asm_sym_intern      (asm_t *a, const char *name, size_t n);
void    asm_label_define    (asm_t *a, sym_t *s, const loc_t *loc);

// numeric helpers used by the grammar
num_t   num_negate          (asm_t *a, const loc_t *loc, num_t n);
imm_t   imm_from_sym        (sym_t *s);
imm_t   imm_label_offset    (asm_t *a, const loc_t *loc, sym_t *s, num_t off);   // label +/- integer
imm_t   imm_any             (num_t n);                      // bits exactly as written
imm_t   imm_integer         (asm_t *a, const loc_t *loc, num_t n);
imm_t   imm_float           (num_t n);                      // integers get converted
imm_t   imm_address         (asm_t *a, const loc_t *loc, imm_t v);   // int or label
uint32_t port_from_num      (asm_t *a, const loc_t *loc, num_t n);

// emitters used by the grammar
void    asm_inst            (asm_t *a, const loc_t *loc, unsigned opcode,
                             unsigned r1, unsigned r2, unsigned mode, unsigned port,
                             imm_t imm);
// internal registers (IP, IR, IV), reached through MOV expansions; see asm.c
enum { SYS_IP = 0, SYS_IR = 1, SYS_IV = 2 };
void    asm_mov_sysreg_read  (asm_t *a, const loc_t *loc, unsigned rn, unsigned sys);
void    asm_mov_sysreg_write (asm_t *a, const loc_t *loc, unsigned sys, unsigned rn);
void    asm_mov_ip_value     (asm_t *a, const loc_t *loc, imm_t target);
void    asm_data_begin      (asm_t *a);
void    asm_data_word       (asm_t *a, const loc_t *loc, imm_t v);
void    asm_data_string     (asm_t *a, str_t s);
void    asm_data_zstring    (asm_t *a, str_t s);
void    asm_data_file       (asm_t *a, const loc_t *loc, str_t path);
void    asm_data_end        (asm_t *a);
void    asm_end_line        (asm_t *a);

////////////////////////////////////////////////////////////////////////////////////////
//
// lexer.l: scanner management (the generated scanner header is kept private)
//
void   *v32_lex_new         (asm_t *a);
void    v32_lex_free        (void *scanner);
void    v32_lex_buffer      (void *scanner, char *buf, size_t size);   // buf: text\n\0\0

#endif
