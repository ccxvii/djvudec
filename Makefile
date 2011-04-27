# GNU Makefile

build ?= debug

OUT := build/$(build)

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

APP := $(OUT)/a.out
SRC := $(wildcard *.c)

$(APP) : $(addprefix $(OUT)/, $(SRC:%.c=%.o))
	$(LINK_CMD)

all: $(APP)

