
OBJ_DIR = objs

CFLAGS = -pipe  -O -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g

CC := cc
LD := $(CC)
OBJDUMP := objdump
NM := nm

APP_NAME := cpx_https

INC = \
	linux\

SRC_FILES := \
		main.c



OBJ_FILES := $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC_FILES))

INC_CFLAGS := $(addprefix -I, $(INC))

CFLAGS += $(INC_CFLAGS)


all: build


$(OBJ_DIR)/%.o: %.c
	@echo + cc $<
	@mkdir -p $(@D)
	$(V)$(CC) -c $(CFLAGS) -o $@ $<


build: $(OBJ_DIR)/$(APP_NAME)


$(OBJ_DIR)/$(APP_NAME): $(OBJ_FILES)
	$(V)$(LD) -o $@ $^
	$(V)$(OBJDUMP) -S $@ > $@.asm
	$(V)$(NM) -n $@ > $@.sym



clean:
	rm -rf $(OBJ_DIR)