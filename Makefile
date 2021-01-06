CC := gcc
SRCD := src
BLDD := build
BIND := bin
INCD := include
TESTD := tests

CFLAGS := -O2 -Wall -Werror
LDFLAGS :=
EXEC := smash

INC := -I $(INCD)

SRC_FILES := $(shell find $(SRCD) -type f -name *.c)
OBJ_FILES := $(patsubst $(SRCD)/%,$(BLDD)/%,$(SRC_FILES:.c=.o))
HDR_FILES := $(shell find $(INCD) -type f -name *.h)
TST_FILES := $(shell find $(TSTD) -type f -name test*.sh)

.PHONY: clean all setup

all: setup $(BIND)/$(EXEC)

smash: all

debug: CFLAGS += -DDEBUG -g
debug: all

ec: CFLAGS += -DEXTRA_CREDIT
ec: all

dec: CFLAGS += -DDEBUG -DEXTRA_CREDIT -g
dec: all

setup: $(BIND) $(BLDD)
$(BIND):
	mkdir -p $(BIND)
$(BLDD):
	mkdir -p $(BLDD)

$(BIND)/$(EXEC): $(OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BLDD)/%.o: $(SRCD)/%.c $(HDR_FILES)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<


clean:
	rm -rf $(BLDD) $(BIND)

tests: $(EXEC)
	@for x in tests/test*.sh; do $(BIND)/$(EXEC) $$x; done

ectests: ec $(EXEC)
	@for x in tests/ectest*.sh; do $(BIND)/$(EXEC) $$x; done

doc:
	pandoc README.md -V geometry:margin=1in -V 'mainfont:Times New Roman' -V fontsize=11pt -o design.pdf
