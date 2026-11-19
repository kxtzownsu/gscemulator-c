PROGRAM_VERSION := 0.1.0

GIT_COMMIT := $(shell $(GIT) rev-parse --short HEAD 2>/dev/null)
GIT_DIRTY := $(if $(shell $(GIT) status --porcelain 2>/dev/null),-dirty,)
PROGRAM_VERSION_STRING := $(PROGRAM_VERSION)$(if $(GIT_COMMIT),-$(GIT_COMMIT))$(GIT_DIRTY)

TOOLCHAIN := $(shell $(CC) --version 2>/dev/null | head -n 1)
ifeq ($(strip $(TOOLCHAIN)),)
TOOLCHAIN := unknown
endif

UNICORN_VERSION := $(shell $(PKG_CONFIG) --modversion unicorn 2>/dev/null)
ifeq ($(STATIC),1)
UNICORN_COMMIT := $(shell $(GIT) -C $(UNICORN_DIR) rev-parse --short HEAD 2>/dev/null)
ifneq ($(wildcard $(UNICORN_BUILD_DIR)/unicorn.pc),)
UNICORN_VERSION := $(shell sed -n 's/^Version: //p' $(UNICORN_BUILD_DIR)/unicorn.pc 2>/dev/null)
endif
endif
ifeq ($(strip $(UNICORN_VERSION)),)
UNICORN_VERSION := unknown
endif
UNICORN_STRING := unicorn $(UNICORN_VERSION)$(if $(UNICORN_COMMIT), ($(UNICORN_COMMIT)),)

ifeq ($(BUNDLED_FIRMWARE),1)
BUNDLED_FIRMWARE_TYPES := $(foreach chip,$(BUNDLED_CHIPS),$(shell printf '%s' '$(chip)' | tr '[:upper:]' '[:lower:]'):rom:fw)
endif
ifeq ($(strip $(BUNDLED_FIRMWARE_TYPES)),)
BUNDLED_FIRMWARE_TYPES := none
endif

BUILD_CONFIG_STRING := STATIC=$(STATIC) BUNDLED_FIRMWARE=$(BUNDLED_FIRMWARE) BUNDLED_CHIPS=$(BUNDLED_CHIPS) CC=$(CC) CHIPS=$(CHIPS) PROGRAM_VERSION=$(PROGRAM_VERSION) SINGLE_THREAD=$(SINGLE_THREAD) TSAN=$(TSAN)
VERSION_DEPS := $(SRCS) $(shell find $(SRC_DIR) $(INCLUDE_DIR) -name '*.h' ! -name 'version.h')

$(BUILD_CONFIG): FORCE
	@$(MKDIR) -p $(dir $@)
	@if [ "$$(cat $@ 2>/dev/null)" != '$(BUILD_CONFIG_STRING)' ]; then \
		printf '%s\n' '$(BUILD_CONFIG_STRING)' > $@; \
	fi

$(VERSION_HEADER): $(BUILD_CONFIG) $(VERSION_DEPS)
	@echo "  VERSION   $@"
	$(Q)$(BASH) $(PROJECT_DIR)/scripts/generate_version_header.sh \
		--name "$(project_name)" \
		--version "$(PROGRAM_VERSION_STRING)" \
		--toolchain "$(TOOLCHAIN)" \
		--unicorn "$(UNICORN_STRING)" \
		--chips "$(CHIPS)" \
		--bundled "$(BUNDLED_FIRMWARE_TYPES)" \
		> $@

.PHONY: FORCE

FORCE:

$(OBJ_DIR)/common/version.o: $(VERSION_HEADER)
