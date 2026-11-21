# v32as: the C implementation of a Vircon32 assembler (flex + bison)
#
#   make            build bin/v32as and bin/v32asld (the linker)
#   make debug      build with -g and no optimization
#   make test       run the regression tests (tests/run_tests.sh)
#   make install    install v32as, v32asld, the macro libraries and the man pages
#                   (default: ~/bin and ~/man; PREFIX=/usr/local gives the usual layout;
#                   DESTDIR, PREFIX, BINDIR, MANDIR and LIBDIR are honoured)
#   make uninstall  remove what install put there
#   make sysinstall make install PREFIX=/usr/local (or the PREFIX you give; DESTDIR works too)
#   make sysuninstall  the reverse
#   make mancheck   lint the man pages (plain sh and awk)
#   make version    stamp the version everywhere it appears (inc/v32as.h, src/ld.c, the man pages):
#                   make version V=20261105-dev [VDATE=2026-11-05]   (default: today's date + -dev)
#   make archive    zip the sources (without build products) to ../v32as-project.zip
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
	$(CC) -o $@ $(OBJ) $(LIBS)

bin/v32asld: $(SRC)/ld.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $<

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
ARCHIVE  ?= ../v32as-project.zip

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)
	install -m 755 bin/$(PROJ) bin/v32asld $(DESTDIR)$(BINDIR)/
	install -m 644 $(LIBFILES) $(DESTDIR)$(LIBDIR)/
	@for f in $(MANPAGES); do \
	    d=$(DESTDIR)$(MANDIR)/$$(basename $$(dirname $$f)); \
	    install -d $$d && install -m 644 $$f $$d/ || exit 1; \
	done
	@echo "installed: $(DESTDIR)$(BINDIR)/$(PROJ), $(DESTDIR)$(BINDIR)/v32asld, man pages in $(DESTDIR)$(MANDIR), macro libraries in $(DESTDIR)$(LIBDIR)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROJ) $(DESTDIR)$(BINDIR)/v32asld
	rm -f $(addprefix $(DESTDIR)$(LIBDIR)/,$(notdir $(LIBFILES)))
	@for f in $(MANPAGES); do \
	    rm -f $(DESTDIR)$(MANDIR)/$$(basename $$(dirname $$f))/$$(basename $$f); \
	done

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
	@rm -rvf $(BUILD) bin/$(PROJ) bin/v32asld .*.sw[op] *~ core ../v32as-project.zip *.zip *.patch

archive: clean
	@zip -r $(ARCHIVE) . -x '.git/*' 'build/*' 'bin/*'

# Stamp a version: the VERSION macros of v32as and v32asld and the .TH line of every man page.
V     ?= $(shell date +%Y%m%d)-dev
VDATE ?= $(shell date +%Y-%m-%d)
# (sed -i.bak and then rm: the one spelling of in-place editing that GNU and BSD/macOS sed both accept)
version:
	@sed -i.bak 's/^\(#define  VERSION  *\)"[^"]*"/\1"$(V)"/' inc/v32as.h $(SRC)/ld.c
	@sed -i.bak '1s/^\(\.TH [^"]*\)"[^"]*" "\(v32as[a-z]*\) [^"]*"/\1"$(VDATE)" "\2 $(V)"/' $(MANPAGES)
	@rm -f inc/v32as.h.bak $(SRC)/ld.c.bak $(addsuffix .bak,$(MANPAGES))
	@grep -n '^#define  VERSION' inc/v32as.h $(SRC)/ld.c; for f in $(MANPAGES); do sed -n 1p $$f; done

.PHONY: all debug test install uninstall sysinstall sysuninstall mancheck clean archive version
