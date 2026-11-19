#ifndef CTX_H
#define CTX_H

#include <stdbool.h>
#include <unicorn/unicorn.h>

#include <libargs.h>
#include <common/emulator.h>
#include <common/essential-memory.h>
#include <common/peripherals/component.h>

#define MAX_FILE_PATH 512

typedef struct {
  char bootrom[MAX_FILE_PATH];
  bool useBundledROM;
  char infobnk[MAX_FILE_PATH];
  char progfw[MAX_FILE_PATH];
  bool useBundledFW;
} Firmware;

typedef struct {
  ArgCheck *ac;
  EmulatorChips chip;
  Emulator chipemu;
  uc_engine *uc;
  const EssentialMemoryEntry *essential_memory;
  Firmware firmware;
  Component components[COMPONENT_MAX];
  size_t component_count;
  char flash_dir[MAX_FILE_PATH];
  bool persist_flash;
  bool verbose;
} Ctx;

extern Ctx ctx;

void ctx_init(void);

#endif /* CTX_H */
