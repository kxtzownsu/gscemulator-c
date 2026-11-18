## dirs ##
PROJECT_DIR := $(abspath .)
BUILD_ROOT := $(abspath $(PROJECT_DIR)/build)
BUILD_DIR := $(BUILD_ROOT)/$(ARCH)
OBJ_DIR := $(BUILD_DIR)/obj
THIRD_PARTY_DIR := $(PROJECT_DIR)/third_party
UNICORN_DIR := $(THIRD_PARTY_DIR)/unicorn
UNICORN_BUILD_DIR := $(BUILD_DIR)/unicorn
UNICORN_STATIC_LIB := $(UNICORN_BUILD_DIR)/libunicorn.a
UNICORN_STATIC_HDRS := $(UNICORN_DIR)/include
INCLUDE_DIR := $(PROJECT_DIR)/include
SRC_DIR := $(PROJECT_DIR)/src
TARGET := $(BUILD_DIR)/gscemu

## files ##
SRCS := $(filter-out $(SRC_DIR)/H1B3C/firmware/%,$(shell find $(SRC_DIR) -name '*.c'))
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
BUILD_CONFIG := $(BUILD_DIR)/.build-config
VERSION_HEADER := $(INCLUDE_DIR)/common/version.h
BUNDLED_FIRMWARE_HEADER := $(INCLUDE_DIR)/common/bundled-firmware.h
CHIPS := h1b3c

## fuzz ##
FUZZ_TIME ?= 60
FUZZ_ARGS ?=
FUZZ_BIN := $(BUILD_DIR)/fuzz_libargs
FUZZ_SRC := $(PROJECT_DIR)/tests/fuzz_libargs.c
FUZZ_DICT := $(PROJECT_DIR)/tests/fuzz.dict
FUZZ_CORPUS := $(BUILD_DIR)/fuzz-corpus
FUZZ_CFLAGS := -std=c11 -g -O1 -Wall -Wextra -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
