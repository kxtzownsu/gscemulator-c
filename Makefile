project_name := gscemu
project_name_upper := GSCEMU

GOAL := $(firstword $(MAKECMDGOALS))

include scripts/make/toolchain.mk
include scripts/make/variables.mk
include scripts/make/setup.mk
include scripts/make/firmware.mk
include scripts/make/unicorn.mk
include scripts/make/version.mk
include scripts/make/flags.mk
include scripts/make/build.mk
include scripts/make/fuzz.mk
include scripts/make/clean.mk

.DEFAULT_GOAL := all

.DELETE_ON_ERROR:

.PHONY: all clean fullclean run fuzz

-include $(DEPS)
