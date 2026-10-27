# v32as: the C implementation of a Vircon32 assembler (flex + bison)
#
#   make            build bin/v32as and bin/v32asld (the linker)
#   make debug      build with -g and no optimization
#   make test       run the regression tests (tests/run_tests.sh)
#   make install    copy the tool into ~/bin
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
OBJ      = $(addprefix $(BUILD)/,parser.tab.o lexer.o asm.o tables.o pp.o ppexpr.o main.o)

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
$(BUILD)/parser.tab.c $(BUILD)/parser.tab.h: $(SRC)/parser.y | $(BUILD)
	$(BISON) -Wall -d -o $(BUILD)/parser.tab.c $(SRC)/parser.y

$(BUILD)/lexer.c: $(SRC)/lexer.l $(BUILD)/parser.tab.h | $(BUILD)
	$(FLEX) -o $@ $(SRC)/lexer.l

# generated files get their own warning exemptions
$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -c -o $@ $<

$(BUILD)/lexer.o: $(BUILD)/lexer.c $(BUILD)/parser.tab.h $(HDRS)
	$(CC) $(CFLAGS) -Wno-sign-compare -Wno-unused-function -c -o $@ $<

# hand-written sources
$(BUILD)/%.o: $(SRC)/%.c $(BUILD)/parser.tab.h $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	@sh tests/run_tests.sh bin/$(PROJ) bin/v32asld

install: all
	@mkdir -p /home/$(USER)/bin
	@cp -av bin/$(PROJ) bin/v32asld /home/$(USER)/bin/

sysinstall: all
	@cp -av bin/$(PROJ) bin/v32asld /usr/local/bin/

clean:
	@rm -rvf $(BUILD) bin/$(PROJ) bin/v32asld .*.sw[op] *~ core ../v32as-project.zip *.zip *.patch

archive: clean
	@zip -r ../v32as-project.zip . -x '.git/*' 'build/*' 'bin/*'

.PHONY: all debug test install clean archive
