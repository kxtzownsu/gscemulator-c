HOST_ARCH := $(shell uname -m)
ARCH ?= $(HOST_ARCH)

ifneq ($(USE_ALL_CORES),1)
ifeq ($(shell nproc),1)
HOST_CORES := $(shell nproc)
else
HOST_CORES := $(shell expr `nproc` / 2)
endif
else
HOST_CORES := $(shell nproc)
endif

ifeq ($(VERBOSE),1)
Q :=
else
Q := @
endif

BASH ?= $(shell command -v bash)
SHELL := $(BASH)

CC ?= $(shell command -v gcc)
GIT ?= $(shell command -v git)
PKG_CONFIG ?= $(shell command -v pkg-config)
FUZZ_CC ?= $(shell command -v clang)
CMAKE ?= $(shell command -v cmake)

MKDIR ?= $(shell command -v mkdir)
COPY ?= $(shell command -v cp)
MOVE ?= $(shell command -v mv)
RM ?= $(shell command -v rm)
