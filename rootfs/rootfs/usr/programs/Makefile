ROOT := $(abspath ../../../..)
LIBC_DIR := $(ROOT)/rootfs/rootfs/usr/libc
NEWLIB_SOURCE := $(ROOT)/newlib-cygwin
NEWLIB_BUILD ?= $(ROOT)/build/newlib-i686
NEWLIB_LIB := $(NEWLIB_BUILD)/i686-elf/newlib/libc.a
NEWLIB_LIBM := $(NEWLIB_BUILD)/i686-elf/newlib/libm.a
NEWLIB_HEADERS := $(NEWLIB_BUILD)/i686-elf/newlib/targ-include
USER_BUILD := $(ROOT)/build/userspace

CC := i686-elf-gcc
PROGRAM ?= test.c
OUTPUT ?= $(basename $(notdir $(PROGRAM))).elf
PROGRAM_OBJ := $(USER_BUILD)/$(basename $(notdir $(PROGRAM))).o
CRT_OBJ := $(USER_BUILD)/crt0.o
SYSCALL_OBJ := $(USER_BUILD)/syscall.o

USER_CFLAGS := -std=gnu99 -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra
USER_CPPFLAGS := -isystem $(NEWLIB_HEADERS) -isystem $(NEWLIB_SOURCE)/newlib/libc/include -I$(LIBC_DIR)/include

.PHONY: all newlib

all: $(OUTPUT)

newlib:
	@if [ ! -f "$(NEWLIB_LIB)" ] || [ ! -f "$(NEWLIB_LIBM)" ]; then \
		mkdir -p "$(NEWLIB_BUILD)" && \
		cd "$(NEWLIB_BUILD)" && "$(NEWLIB_SOURCE)/configure" --target=i686-elf --prefix="$(NEWLIB_BUILD)/install" --disable-nls && \
		$(MAKE) -C "$(NEWLIB_BUILD)" all-target-newlib; \
	fi

$(PROGRAM_OBJ): $(PROGRAM) | newlib
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(USER_CPPFLAGS) -c $< -o $@

$(CRT_OBJ): $(LIBC_DIR)/crt/crt0.S | newlib
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(USER_CPPFLAGS) -c $< -o $@

$(SYSCALL_OBJ): $(LIBC_DIR)/syscall.c | newlib
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(USER_CPPFLAGS) -c $< -o $@

$(OUTPUT): $(PROGRAM_OBJ) $(CRT_OBJ) $(SYSCALL_OBJ) $(LIBC_DIR)/user.ld | newlib
	$(CC) -nostdlib -static -no-pie -Wl,-T,$(LIBC_DIR)/user.ld -o $@ \
		$(CRT_OBJ) $(PROGRAM_OBJ) $(SYSCALL_OBJ) \
		-L"$(NEWLIB_BUILD)/i686-elf/newlib" -lc -lm -lgcc