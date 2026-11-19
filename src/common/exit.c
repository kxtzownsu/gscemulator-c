#include <assert.h>
#include <stdlib.h>

#include <common/common.h>
#include <common/peripherals/component.h>
#include <common/runtime/console.h>
#include <common/runtime/thread.h>

ARGCHECK_NORETURN void emu_exit(Rc rc) {
  void (*end_emulation)(void) = ctx.chipemu.end_emulation;
  ArgCheck *ac = ctx.ac;

  assert(thread_is_control());

  component_stop_all();
  console_stop();

  ctx.chipemu.end_emulation = NULL;
  ctx.ac = NULL;

  if (end_emulation != NULL) end_emulation();
  if (ac != NULL) argcheck_destroy(ac);

  exit(rc);
}
