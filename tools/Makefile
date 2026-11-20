CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
CPPFLAGS ?= -I../include -Icommon

COMMON_SRCS = common/shaft.c ../src/common/sha256.c
COMMON_HDRS = common/shaft.h

PROGS = rescue/rescue spiflash/spiflash

.PHONY: all clean

all: $(PROGS)

rescue/rescue: rescue/rescue.c $(COMMON_SRCS) $(COMMON_HDRS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

spiflash/spiflash: spiflash/spiflash.c $(COMMON_SRCS) $(COMMON_HDRS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

clean:
	rm -f $(PROGS)
