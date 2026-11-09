//
// buildscript.h: the ROM definition (XML for packrom) and the Make.sh / Make.bat build scripts of v32as
//
// v32as itself only assembles.  When the program says what the cartridge is made of (%title, %version, %texture,
// %sound; see pp.h) it can also write the definition packrom reads (--rom-xml) and the scripts that rebuild the
// whole cartridge from the sources: optimize with v32opt (when it is installed), assemble, convert the images and
// sounds with png2vircon / wav2vircon, pack with packrom (--build-scripts).  Modelled on v32cc's build scripts.
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef V32AS_BUILDSCRIPT_H
#define V32AS_BUILDSCRIPT_H

#include "pp.h"

// An argument list for the scripts: the text, and whether it is a file name (then it is written relative to the
// directory of the scripts, since a script is run from there)
typedef struct
{
    char  **text;
    int    *path;
    int     n;
    int     cap;
}
bs_args_t;

void  bs_args_add  (bs_args_t *a, const char *text, int is_path);
void  bs_args_free (bs_args_t *a);

// The environment variable a build script sets: a v32as started from inside a build never rewrites the scripts
#define BS_ENV_GUARD  "V32AS_BUILD_SCRIPT"

// Write the ROM definition for packrom to XML_PATH.  VBIN_PATH is the binary the definition names; the file names
// in it are relative to XML_PATH's directory.  optimized: the twin for the v32opt build ("<title> [optimized]").
// fallback_title: the title when the source has no %title (the program's name).  Returns 0, or -1 with the
// message written to STDERR.
int   bs_write_xml (const pp_t *pp, const char *xml_path, const char *vbin_path, int bios, int optimized,
                    const char *fallback_title);

// What the scripts are built from
typedef struct
{
    const char *tool;               // v32as, as it is to be called by the scripts
    int         bios;               // BIOS (-b) instead of a cartridge
    const char *output;             // the VBIN
    const char *xml;                // the ROM definition: --rom-xml, or NULL for OUTPUT with .xml
    const char *xml_opt;            // its twin for the optimized build, or NULL for NAMEOpt.xml
    const char *dir;                // --build-script-dir, or NULL: next to the first input (its parent for src/ + obj/)
    const char *formats;            // comma list of sh, bat
    const bs_args_t *flags;         // the options the assembler is called with
    const bs_args_t *inputs;        // the sources
    const pp_t *pp;                 // the %texture / %sound lists
}
bs_job_t;

// The file names of a job (as the caller gave them, not made relative to anything): the ROM definition, and the
// optimized twin of each file.  has_opt is 0 when there is no twin: more than one source (v32opt takes one), or a
// name that would overwrite a source.
typedef struct
{
    char  *xml;
    char  *xml_opt;
    char  *vbin_opt;
    char  *asm_opt;
    char  *asm_pre;                 // the source after the preprocessor: what v32opt reads
    int    has_opt;
}
bs_names_t;

void  bs_names      (const bs_job_t *job, bs_names_t *n);
void  bs_names_free (bs_names_t *n);

// Write Make.sh and/or Make.bat.  A file that was not written by v32as is never touched; one that would not
// change is left alone; one that is replaced is replaced in one step, so a copy being run keeps working.
// Returns 0 (also when a file was kept), or -1 on a message about a script that could not be written.
int   bs_emit_scripts (const bs_job_t *job);

// Is FORMATS a valid list ("sh", "bat", "sh,bat")?  Returns the offending word, or NULL.
const char  *bs_bad_format (const char *formats);

#endif
