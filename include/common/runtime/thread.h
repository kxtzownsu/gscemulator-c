#ifndef COMMON_RUNTIME_THREAD_H
#define COMMON_RUNTIME_THREAD_H

#include <stdbool.h>
#include <stdint.h>

struct Component;
struct Worker;

typedef enum {
  THREAD_ROLE_UNKNOWN = 0,
  THREAD_ROLE_CONTROL,
  THREAD_ROLE_EMULATION,
  THREAD_ROLE_CONSOLE,
  THREAD_ROLE_WORKER,
} ThreadRole;

typedef struct {
  ThreadRole role;
  const char *name;
  struct Worker *worker;
  struct Component *component;
  uint32_t mmio_pc;
  bool mmio_pc_valid;
} ThreadCtx;

extern _Thread_local ThreadCtx tctx;

void thread_ctx_init(ThreadRole role, const char *name);
bool thread_is_control(void);

#endif /* COMMON_RUNTIME_THREAD_H */
