#include <common/runtime/thread.h>

#include <stddef.h>

_Thread_local ThreadCtx tctx;

void thread_ctx_init(ThreadRole role, const char *name) {
  tctx.role = role;
  tctx.name = name;
  tctx.worker = NULL;
  tctx.component = NULL;
  tctx.mmio_pc = 0;
  tctx.mmio_pc_valid = false;
}

bool thread_is_control(void) {
  return tctx.role == THREAD_ROLE_CONTROL || tctx.role == THREAD_ROLE_UNKNOWN;
}
