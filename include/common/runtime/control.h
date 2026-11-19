#ifndef COMMON_RUNTIME_CONTROL_H
#define COMMON_RUNTIME_CONTROL_H

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>

#include <common/logger.h>
#include <common/rc.h>

typedef struct {
  _Atomic bool shutting_down;
  _Atomic bool emu_done;
  _Atomic bool reboot_requested;
  _Atomic int pending_rc;
  char pending_msg[256];
  pthread_mutex_t fault_lock;
  int wake_fd[2];
  pthread_t emu_thread;
  bool emu_thread_started;
  volatile sig_atomic_t signal_received;
} Runtime;

void runtime_init(void);
LOG_PRINTF_FORMAT(2, 3)
void runtime_fault(Rc rc, const char *fmt, ...);
bool runtime_is_shutting_down(void);
void runtime_request_reboot(void);
bool runtime_reboot_requested(void);
void runtime_start_emulation(void);
void runtime_wake(void);
void runtime_run(void);

#endif /* COMMON_RUNTIME_CONTROL_H */
