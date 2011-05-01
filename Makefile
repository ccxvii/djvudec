# GNU Makefile

build ?= debug

OUT := build/$(build)

CFLAGS += -Wall -g

default: all

ifeq "$(verbose)" ""
QUIET_CC = @ echo ' ' ' ' CC $@ ;
QUIET_LINK = @ echo ' ' ' ' LINK $@ ;
QUIET_MKDIR = @ echo ' ' ' ' MKDIR $@ ;
endif

CC_CMD = $(QUIET_CC) $(CC) $(CFLAGS) -o $@ -c $<
LINK_CMD = $(QUIET_LINK) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
MKDIR_CMD = $(QUIET_MKDIR) mkdir -p $@

$(OUT) :
	$(MKDIR_CMD)

$(OUT)/%.o : %.c djvudec.h | $(OUT)
	$(CC_CMD)

AOUT := $(OUT)/a.out
SRC := dv_doc.c dv_iw44.c dv_jb2.c dv_bzz.c dv_zp.c

$(AOUT) : $(addprefix $(OUT)/, $(SRC:%.c=%.o))
	$(LINK_CMD)

all: $(AOUT)

clean:
	rm $(OUT)/*

nuke:
	rm -rf build

