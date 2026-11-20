STATIC ?= 1

ifeq ($(STATIC),1)
UNICORN_CFLAGS := -I$(UNICORN_STATIC_HDRS)
UNICORN_LIBS := $(UNICORN_STATIC_LIB) -lpthread -lm
else
ifneq ($(PKG_CONFIG),)
UNICORN_CFLAGS := $(shell $(PKG_CONFIG) --cflags unicorn)
UNICORN_LIBS := $(shell $(PKG_CONFIG) --libs unicorn)
endif
endif

CFLAGS ?= -O2 -g $(UNICORN_CFLAGS)
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Werror=format-security -Werror=format-nonliteral -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pthread
CPPFLAGS += -I$(INCLUDE_DIR)
LDFLAGS ?=
LDFLAGS += -fno-pie -Wl,-z,relro,-z,now
LDLIBS ?= $(UNICORN_LIBS)
LDLIBS += -pthread -lutil

SINGLE_THREAD ?= 0
TSAN ?= 0

ifeq ($(SINGLE_THREAD),1)
CPPFLAGS += -DSINGLE_THREAD
endif

ifeq ($(TSAN),1)
CFLAGS += -fsanitize=thread -fno-omit-frame-pointer
LDFLAGS += -fsanitize=thread
endif

ifeq ($(STATIC),1)
CPPFLAGS += -DUNICORN_SAFE_HOOKS
LDFLAGS += -static
endif
