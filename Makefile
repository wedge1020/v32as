# v32as: the C implementation of a Vircon32 assembler (flex + bison)
#
#   make            build bin/v32as and bin/v32asld (the linker)
#   make debug      build with -g and no optimization
#   make test       run the regression tests (tests/run_tests.sh)
#   make install    install v32as, v32asld, the macro libraries, the man pages and the documents
#                   (default: ~/bin, ~/man and ~/share/doc/v32as; PREFIX=/usr/local gives the usual
#                   layout; DESTDIR, PREFIX, BINDIR, MANDIR, LIBDIR and DOCDIR are honoured;
#                   DEST_LANG=en|es|fr picks the language of the man1 pages and documents, default en)
#   make uninstall  remove what install put there
#   make sysinstall make install PREFIX=/usr/local (or the PREFIX you give; DESTDIR and DEST_LANG work too)
#   make sysuninstall  the reverse
#   make mancheck   lint the man pages (plain sh and awk)
#   make version    read the version from the VERSION line of inc/v32as.h (the single source) and stamp it
#                   everywhere it appears; with VERSION=YYYYMMDD-status (V= also works) write that one first (inc/v32as.h,
#                   src/ld.c, the man pages in every language): make version VERSION=20261105-dev
#                   [VDATE=2026-11-05, the date of the man pages; default today]
#   make archive    zip the sources (without build products) to ../v32as-project-YYYYMMDD.zip,
#                   YYYYMMDD being the date part of the VERSION
#
# Layout:  src/ sources   inc/ headers   docs/ design notes   tests/ regression tests
#          build/ generated files and objects (made on demand)   bin/ the finished tool
#
# Requires: a C compiler, flex and bison. GNU readline is used for the interactive
# mode when its development files are found (make READLINE=0 to opt out).

CC       ?= gcc
FLEX     ?= flex
BISON    ?= bison
CFLAGS   ?= -O2
override CFLAGS += -Wall -Wextra -std=gnu11 -Wno-unused-parameter
LIBS     = -lm
PROJ     = v32as
SRC      = src
INC      = inc
BUILD    = build
override CFLAGS += -I$(INC) -I$(BUILD)
OBJ      = $(addprefix $(BUILD)/,parser.tab.o lexer.o asm.o tables.o heapstat.o pp.o ppexpr.o buildscript.o main.o)

READLINE ?= auto
ifeq ($(READLINE),auto)
  HAVE_RL := $(shell echo '\#include <readline/readline.h>' | $(CC) -E - >/dev/null 2>&1 && echo 1)
else
  HAVE_RL := $(filter 1,$(READLINE))
endif
ifeq ($(HAVE_RL),1)
  override CFLAGS += -DHAVE_READLINE
  override LIBS += -lreadline
endif

HDRS     = $(wildcard $(INC)/*.h)

all: bin/$(PROJ) bin/v32asld

debug: override CFLAGS += -g -O0 -DDEBUG
debug: all

bin/$(PROJ): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

bin/v32asld: $(SRC)/ld.c
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

# generated sources
# One rule per output: a rule with two targets is two independent rules to make, and
# under -j bison would run twice at once and corrupt parser.tab.c. The header comes
# with the source file (bison -d writes both), hence the recipe only freshens its time stamp.
$(BUILD)/parser.tab.c: $(SRC)/parser.y | $(BUILD)
	$(BISON) -Wall -d -o $(BUILD)/parser.tab.c $(SRC)/parser.y

$(BUILD)/parser.tab.h: $(BUILD)/parser.tab.c
	@[ -f $@ ] || { rm -f $<; $(MAKE) --no-print-directory $<; }
	@touch $@

$(BUILD)/lexer.c: $(SRC)/lexer.l $(BUILD)/parser.tab.h | $(BUILD)
	$(FLEX) -o $@ $(SRC)/lexer.l

# generated files get their own warning exemptions
$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -c -o $@ $<

$(BUILD)/lexer.o: $(BUILD)/lexer.c $(BUILD)/parser.tab.h $(HDRS)
	$(CC) $(CFLAGS) -Wno-sign-compare -Wno-unused-function -c -o $@ $<

# hand-written sources
# where `make install` puts the macro libraries: searched by %include after the -I directories (see add_default_include_dirs)
$(BUILD)/main.o: override CFLAGS += -DV32AS_LIBDIR='"$(LIBDIR)"'

$(BUILD)/%.o: $(SRC)/%.c $(BUILD)/parser.tab.h $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	@sh tests/run_tests.sh bin/$(PROJ) bin/v32asld
	@sh tests/exe_relative.sh bin/$(PROJ)
	@sh tests/rom_tests.sh bin/$(PROJ)

# Where install puts things.  Without PREFIX the tools go to ~/bin and the man pages to ~/man
# (what man-db finds for a ~/bin in PATH); with PREFIX=/usr/local (or /usr, /opt/v32as ...) the
# usual PREFIX/bin and PREFIX/share/man.  Each part can be set on its own; DESTDIR is for packaging.
ifdef PREFIX
  BINDIR ?= $(PREFIX)/bin
  MANDIR ?= $(PREFIX)/share/man
  LIBDIR ?= $(PREFIX)/share/v32as/lib
else
  BINDIR ?= $(HOME)/bin
  MANDIR ?= $(HOME)/man
  LIBDIR ?= $(HOME)/share/v32as/lib
endif
MANPAGES = $(wildcard man/man1/*.1 man/man3/*.3 man/man7/*.7)
LIBFILES = $(wildcard lib/*.inc)

# DEST_LANG (en, es or fr; default en) is the language of the installed man1 pages (man/es/man1,
# man/fr/man1: installed under the normal names, so `man v32as` shows them; a page with no
# translation is installed in English; man3 and man7 are English only) and of the documents.
DEST_LANG ?= en
ifeq ($(filter $(DEST_LANG),en es fr),)
$(error DEST_LANG must be en, es or fr (got '$(DEST_LANG)'))
endif
# The documents: the English ones, and the translation of each (README.es.md, docs/USAGE.fr.md ...)
# that exists. DOCDIR is where they go (README.md and CHANGES.md in it, the others in DOCDIR/docs).
DOCFILES = README.md CHANGES.md LICENSE $(filter-out %.es.md %.fr.md,$(sort $(wildcard docs/*.md)))
TRDOCS   = $(sort $(wildcard README.es.md README.fr.md docs/*.es.md docs/*.fr.md))
ifdef PREFIX
  DOCDIR ?= $(PREFIX)/share/doc/v32as
else
  DOCDIR ?= $(HOME)/share/doc/v32as
endif
# the pages man1 installs under the English names; the English ones that the language has no page for
MAN1EN   = $(wildcard man/man1/*.1)
ARCHIVE  ?= ../v32as-project-$(ARCHDATE).zip

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(DOCDIR)/docs
	install -m 755 bin/$(PROJ) bin/v32asld $(DESTDIR)$(BINDIR)/
	install -m 644 $(LIBFILES) $(DESTDIR)$(LIBDIR)/
	@for f in $(MANPAGES); do \
	    d=$(DESTDIR)$(MANDIR)/$$(basename $$(dirname $$f)); \
	    src=$$f; \
	    case "$$f" in man/man1/*) \
	        if [ "$(DEST_LANG)" != en ] && [ -f "man/$(DEST_LANG)/man1/$$(basename $$f)" ]; then \
	            src="man/$(DEST_LANG)/man1/$$(basename $$f)"; fi ;; \
	    esac; \
	    install -d $$d && install -m 644 $$src $$d/$$(basename $$f) || exit 1; \
	done
	@set -e; for f in $(DOCFILES); do \
	    src=$$f; \
	    if [ "$(DEST_LANG)" != en ]; then \
	        tr=$${f%.md}.$(DEST_LANG).md; \
	        if [ -f "$$tr" ]; then src=$$tr; fi; \
	    fi; \
	    [ -f "$$src" ] || continue; \
	    sed 's/\.$(DEST_LANG)\.md/.md/g' "$$src" > "$(DESTDIR)$(DOCDIR)/$$f"; \
	    chmod 644 "$(DESTDIR)$(DOCDIR)/$$f"; \
	done
	@echo "installed: $(DESTDIR)$(BINDIR)/$(PROJ), $(DESTDIR)$(BINDIR)/v32asld, man pages ($(DEST_LANG)) in $(DESTDIR)$(MANDIR), macro libraries in $(DESTDIR)$(LIBDIR), documents ($(DEST_LANG)) in $(DESTDIR)$(DOCDIR)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROJ) $(DESTDIR)$(BINDIR)/v32asld
	rm -f $(addprefix $(DESTDIR)$(LIBDIR)/,$(notdir $(LIBFILES)))
	@for f in $(MANPAGES); do \
	    rm -f $(DESTDIR)$(MANDIR)/$$(basename $$(dirname $$f))/$$(basename $$f); \
	done
	rm -f $(addprefix $(DESTDIR)$(DOCDIR)/,$(DOCFILES))
	-@rmdir $(DESTDIR)$(DOCDIR)/docs $(DESTDIR)$(DOCDIR) 2>/dev/null; true

# System-wide: PREFIX (default /usr/local) and DESTDIR are honoured.
SYSPREFIX = $(if $(PREFIX),$(PREFIX),/usr/local)

sysinstall: all
	$(MAKE) install PREFIX=$(SYSPREFIX)

sysuninstall:
	$(MAKE) uninstall PREFIX=$(SYSPREFIX)

# A simple lint of the man pages: structure and ASCII only (no groff needed)
mancheck:
	@sh tests/mancheck.sh $(MANPAGES)

clean:
	@rm -rvf $(BUILD) bin/$(PROJ) bin/v32asld .*.sw[op] *~ core *.zip *.patch ../v32as-project.zip ../v32as-project-[0-9]*.zip

archive: clean
	@zip -r $(ARCHIVE) . -x '.git/*' 'build/*' 'bin/*' '*.zip' '*.patch'

# Version: the VERSION line of inc/v32as.h is the single source.  `make version` reads it from there
# and stamps it everywhere it is repeated; `make version VERSION=YYYYMMDD-status` (or the old spelling
# V=...) first rewrites it in inc/v32as.h.  Either way it goes into src/ld.c and the .TH line of every man page: English and the translations in man/es and
# man/fr (their dates in the language's own form).  VDATE=YYYY-MM-DD is the date of the pages
# (default: today).  No `sed -i` (BSD sed takes the script as the backup suffix).
VERSION := $(shell sed -n 's/^\#define[[:space:]]*VERSION[[:space:]]*"\(.*\)".*/\1/p' $(INC)/v32as.h | head -n 1)
ifeq ($(origin V),command line)
  VERSION := $(V)
endif
# always stamp: with no VERSION= the header's own version is written back (a no-op there)
STAMP = yes
# The date part (YYYYMMDD) of VERSION names the project archive.
ARCHDATE := $(shell echo '$(VERSION)' | sed 's/^\([0-9]\{8\}\).*/\1/')
VDATE ?= $(shell date +%Y-%m-%d)
VD_Y  := $(word 1,$(subst -, ,$(VDATE)))
VD_M  := $(shell echo '$(word 2,$(subst -, ,$(VDATE)))' | sed 's/^0//')
VD_D  := $(shell echo '$(word 3,$(subst -, ,$(VDATE)))' | sed 's/^0//')
VDATE_ES := $(VD_D) de $(word $(VD_M),enero febrero marzo abril mayo junio julio agosto septiembre octubre noviembre diciembre) de $(VD_Y)
VDATE_FR := $(if $(filter 1,$(VD_D)),1er,$(VD_D)) $(word $(VD_M),janvier février mars avril mai juin juillet août septembre octobre novembre décembre) $(VD_Y)
ALLMAN   = $(MANPAGES) $(wildcard man/es/man1/*.1 man/fr/man1/*.1)
version:
	@v='$(VERSION)'; \
	if [ -z "$$v" ]; then echo "make version: no version (inc/v32as.h needs a '#define  VERSION  \"...\"' line)" >&2; exit 1; fi; \
	case "$$v" in *[!A-Za-z0-9._+-]*) echo "make version: '$$v' is not a valid version (letters, digits and . _ + - only)" >&2; exit 1 ;; esac
	@if [ -n "$(STAMP)" ]; then \
	    for f in inc/v32as.h $(SRC)/ld.c; do \
	        sed 's/^\(#define  *VERSION  *\)"[^"]*"/\1"$(VERSION)"/' $$f > $$f.tmp && mv $$f.tmp $$f; \
	    done; \
	    for f in $(ALLMAN); do \
	        case "$$f" in man/es/*) d='$(VDATE_ES)' ;; man/fr/*) d='$(VDATE_FR)' ;; *) d='$(VDATE)' ;; esac; \
	        sed "s/^\(\.TH [^\"]*\)\"[^\"]*\" \"\(v32as[a-z]*\) [^\"]*\"/\1\"$$d\" \"\2 $(VERSION)\"/" $$f > $$f.tmp && mv $$f.tmp $$f; \
	    done; \
	fi
	@echo "v32as $(VERSION)"
	@grep -n '^#define  *VERSION' inc/v32as.h $(SRC)/ld.c; for f in $(ALLMAN); do grep -n '^\.TH' $$f; done

# the official assembler, compared: needs a checkout of the official ComputerSoftware repository,
# found next to this one (../ComputerSoftware or ../ComputerSoftware.git) or named with STOCK=DIR.
# Builds its assemble and compile with $(CXX) into build/realcheck/stock; COMBO=1 adds ~52,000
# one-line programs (every opcode with every pair of operand forms; JOBS=N workers, default 4).
STOCK ?= $(firstword $(wildcard ../ComputerSoftware ../ComputerSoftware.git))
realcheck: all
	@if [ -z "$(STOCK)" ]; then \
	    echo "realcheck: no ComputerSoftware checkout next to this one; use: make realcheck STOCK=DIR" >&2; exit 2; fi
	@sh tests/realcheck.sh $(if $(COMBO),--combo) "$(STOCK)" bin/$(PROJ)

.PHONY: all debug test realcheck install uninstall sysinstall sysuninstall mancheck clean archive version
