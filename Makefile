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

$(OUT)/%.o : %.c mudjvu.h | $(OUT)
	$(CC_CMD)

AOUT := $(OUT)/a.out
BOUT := $(OUT)/b.out
ASRC := dv_doc.c dv_bzz.c dv_jb2.c dv_zp.c
BSRC := dv_doc.c dv_bzz.c dv_jb2.c dv_zp_old.c

$(AOUT) : $(addprefix $(OUT)/, $(ASRC:%.c=%.o))
	$(LINK_CMD)
$(BOUT) : $(addprefix $(OUT)/, $(BSRC:%.c=%.o))
	$(LINK_CMD)

all: $(AOUT) $(BOUT)

clean:
	rm $(OUT)/*

nuke:
	rm -rf build

