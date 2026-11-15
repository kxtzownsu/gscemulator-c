#include <stdlib.h>

#include <common/common.h>

ARGCHECK_NORETURN void emu_exit(Rc rc) {
  void (*end_emulation)(void) = ctx.chipemu.end_emulation;
  ArgCheck *ac = ctx.ac;

  ctx.chipemu.end_emulation = NULL;
  ctx.ac = NULL;

  if (end_emulation != NULL) end_emulation();
  if (ac != NULL) argcheck_destroy(ac);

  exit(rc);
}
