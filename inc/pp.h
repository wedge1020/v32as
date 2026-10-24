//
// pp.h: the v32as preprocessor (text in, text + line map out)
//
// The preprocessor runs in front of the assembler proper. It consumes source text,
// executes the %directives, expands macros, and produces plain assembly text plus a map
// from every output line back to the (file, line) it came from, so that diagnostics, the
// -g debug file and "datafile" lookups keep pointing at what the programmer wrote.
//
// Phase 1 implements exact parity with the stock Vircon32 assembler's preprocessor:
//
//     %define NAME tokens...     %undef NAME
//     %ifdef NAME   %ifndef NAME   %else   %endif
//     %include "file"            %error "msg"   %warning "msg"
//
// (directive names are case-insensitive here; macro names are not)
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef V32AS_PP_H
#define V32AS_PP_H

#include <stddef.h>
#include <stdio.h>

// Where one line of preprocessor output came from
typedef struct
{
    const char  *file;              // interned: valid for the life of the process
    int          line;
    int          expanded;          // a macro was expanded on this line: columns are unreliable
}
srcloc_t;

typedef struct pp  pp_t;

pp_t        *pp_new         (void);
void         pp_free        (pp_t *p);

// -D NAME / -D NAME=VALUE (a bare NAME is defined as 1, like a C compiler), -U NAME
int          pp_define_cmdline (pp_t *p, const char *spec);
void         pp_undefine    (pp_t *p, const char *name);

// Preprocess a whole file's text. Output accumulates until pp_output_clear().
// Returns the number of errors found in this call.
unsigned     pp_process     (pp_t *p, const char *name, const char *text, size_t len);

// Line-at-a-time use (interactive mode): one stream stays open, so %ifdef blocks and
// definitions persist from line to line.
void         pp_stream_begin (pp_t *p, const char *name);
unsigned     pp_stream_line  (pp_t *p, const char *line, size_t len, int lineno);
int          pp_pending      (const pp_t *p);       // inside %macro / %rep / %if: more input is expected

// Results
const char     *pp_output   (const pp_t *p, size_t *len);
const srcloc_t *pp_linemap  (const pp_t *p, size_t *nlines);
void            pp_output_clear (pp_t *p);
unsigned        pp_errors   (const pp_t *p);

// Write the preprocessed text (as -E does). With markers, a cpp-style '# N "file"'
// line is written wherever the source position is not simply the next line.
void         pp_write_expanded (const pp_t *p, FILE *fp, int markers);

const char  *pp_intern      (const char *s);

// Configuration
void         pp_set_standard    (pp_t *p, int on);     // exact stock-assembler behaviour, no extensions
void         pp_set_warnings    (pp_t *p, int on);
void         pp_set_target      (pp_t *p, const char *kind, unsigned base);   // "BIOS" "CART" "ORIGIN" "NONE"
void         pp_add_include_dir (pp_t *p, const char *dir);
void         pp_set_redefine_warning (pp_t *p, int on);     // -Wredefine (default on, never under --standard)

// Files read by %include so far, in order (for -MD)
const char  *const  *pp_deps (const pp_t *p, size_t *n);

// Every object-like macro as (name, replacement text); for the compiler's variables
void         pp_each_define (pp_t *p, void (*cb) (void *ud, const char *name, const char *body), void *ud);

// For a label the assembler sees (e.g. "_draw__again") that came from a file-local ".again"
// under "_draw:", its spelling "_draw.again"; NULL for any other label
const char  *pp_local_name (pp_t *p, const char *mangled);

#endif
