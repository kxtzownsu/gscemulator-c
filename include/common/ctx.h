#ifndef CTX_H
#define CTX_H

#include <stdbool.h>
#include <unicorn/unicorn.h>

#include <libargs.h>
#include <common/emulator.h>

#define MAX_FILE_PATH 512

typedef struct {
  char bootrom[MAX_FILE_PATH];
  char infobnk[MAX_FILE_PATH];
  char progfw[MAX_FILE_PATH];
} Firmware;

typedef struct {
  ArgCheck *ac;
  EmulatorChips chip;
  Emulator chipemu;
  uc_engine *uc;
  Firmware firmware;
  bool verbose;
} Ctx;

extern Ctx ctx;

void ctx_init(void);

#endif /* CTX_H */
