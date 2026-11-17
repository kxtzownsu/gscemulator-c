CC ?= gcc
BASH ?= bash
PKG_CONFIG ?= pkg-config 
CFLAGS ?= -O2 -g $(shell $(PKG_CONFIG) --cflags unicorn)
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Werror=format-security -Werror=format-nonliteral -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
CPPFLAGS ?=
LDFLAGS ?=
LDFLAGS += -fno-pie -Wl,-z,relro,-z,now
LDLIBS ?= $(shell $(PKG_CONFIG) --libs unicorn)

ARCH := $(shell uname -m)
PROJECT_DIR := $(abspath .)
BUILD_DIR := $(abspath $(PROJECT_DIR)/build/$(ARCH))
OBJ_DIR := $(BUILD_DIR)/obj
TARGET := $(BUILD_DIR)/gscemu
BUNDLED_FIRMWARE ?= 1

FUZZ_CC ?= clang
FUZZ_TIME ?= 60
FUZZ_ARGS ?=
FUZZ_BIN := $(BUILD_DIR)/fuzz_libargs
FUZZ_SRC := tests/fuzz_libargs.c
FUZZ_DICT := tests/fuzz.dict
FUZZ_CORPUS := $(BUILD_DIR)/fuzz-corpus
FUZZ_CFLAGS := -std=c11 -g -O1 -Wall -Wextra -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer

SRCS := $(filter-out src/H1B3C/firmware/%,$(shell find src -name '*.c'))
OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

ifeq ($(BUNDLED_FIRMWARE),1)

ifdef BUNDLED_CHIP
BUNDLED_CHIPS := $(BUNDLED_CHIP)
endif
BUNDLED_CHIPS ?= H1B3C

BUNDLED_H1B3C_BOOTROM_BIN ?= $(BUNDLED_BOOTROM_BIN)
BUNDLED_H1B3C_FIRMWARE_BIN ?= $(BUNDLED_FIRMWARE_BIN)

BUNDLED_BOOTROM_FOR = $(if $(BUNDLED_$(1)_BOOTROM_BIN),$(BUNDLED_$(1)_BOOTROM_BIN),$(PROJECT_DIR)/src/$(1)/firmware/bootrom.bin)
BUNDLED_FIRMWARE_FOR = $(if $(BUNDLED_$(1)_FIRMWARE_BIN),$(BUNDLED_$(1)_FIRMWARE_BIN),$(PROJECT_DIR)/src/$(1)/firmware/firmware.bin)

BUNDLED_ARGS := $(foreach chip,$(BUNDLED_CHIPS),--chip $(chip) --bootrom $(call BUNDLED_BOOTROM_FOR,$(chip)) --firmware $(call BUNDLED_FIRMWARE_FOR,$(chip)))
BUNDLED_INPUTS := $(foreach chip,$(BUNDLED_CHIPS),$(call BUNDLED_BOOTROM_FOR,$(chip)) $(call BUNDLED_FIRMWARE_FOR,$(chip)))
BUNDLED_FIRMWARE_HEADER := $(PROJECT_DIR)/include/common/bundled-firmware.h

CPPFLAGS += -DBUNDLED_FIRMWARE
CFLAGS += -DBUNDLED_FIRMWARE

endif

CPPFLAGS += -Iinclude

.DEFAULT_GOAL := all

.DELETE_ON_ERROR:

.PHONY: all clean run fuzz

all: $(BUNDLED_FIRMWARE_HEADER) $(TARGET)

$(BUNDLED_FIRMWARE_HEADER): $(BUNDLED_INPUTS)
	$(BASH) $(PROJECT_DIR)/scripts/generate_firmware_header.sh $(BUNDLED_ARGS) > $@

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

fuzz: $(FUZZ_BIN)
	@mkdir -p $(FUZZ_CORPUS)
	$(FUZZ_BIN) -max_total_time=$(FUZZ_TIME) -print_final_stats=1 -dict=$(FUZZ_DICT) $(FUZZ_CORPUS) tests/corpus $(FUZZ_ARGS)

$(FUZZ_BIN): $(FUZZ_SRC) $(FUZZ_DICT) src/libargs.c include/libargs.h
	@mkdir -p $(dir $@)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -Iinclude $(FUZZ_SRC) src/libargs.c -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
