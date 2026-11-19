#include <common/runtime/control.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <common/common.h>
#include <common/peripherals/component.h>
#include <common/runtime/console.h>
#include <common/runtime/thread.h>

static Runtime runtime;

static void runtime_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void runtime_write_wake_byte(char byte) {
  ssize_t written = write(runtime.wake_fd[1], &byte, 1);
  (void)written;
}

#ifndef SINGLE_THREAD
static void runtime_signal_handler(int signum) {
  int saved_errno = errno;

  runtime.signal_received = signum;
  runtime_write_wake_byte('s');

  errno = saved_errno;
}
#endif

static void runtime_drain_wake(void) {
  char buf[64];

  while (read(runtime.wake_fd[0], buf, sizeof(buf)) > 0) {
  }
}

void runtime_wake(void) {
  runtime_write_wake_byte('w');
}

void runtime_init(void) {
#ifndef SINGLE_THREAD
  struct sigaction action;
#endif
  int fds[2];

  thread_ctx_init(THREAD_ROLE_CONTROL, "control");

  atomic_store(&runtime.shutting_down, false);
  atomic_store(&runtime.emu_done, false);
  atomic_store(&runtime.pending_rc, RC_SUCCESS);
  runtime.pending_msg[0] = '\0';
  runtime.emu_thread_started = false;
  runtime.signal_received = 0;
  pthread_mutex_init(&runtime.fault_lock, NULL);

  if (pipe(fds) != 0) {
    LOG_ERR("Failed to create runtime wake pipe: %s\n", strerror(errno));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  runtime.wake_fd[0] = fds[0];
  runtime.wake_fd[1] = fds[1];
  runtime_set_nonblocking(runtime.wake_fd[0]);
  runtime_set_nonblocking(runtime.wake_fd[1]);

#ifndef SINGLE_THREAD
  memset(&action, 0, sizeof(action));
  action.sa_handler = runtime_signal_handler;
  sigemptyset(&action.sa_mask);

  if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) {
    LOG_ERR("Failed to install signal handlers: %s\n", strerror(errno));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
#endif

  signal(SIGPIPE, SIG_IGN);
}

void runtime_fault(Rc rc, const char *fmt, ...) {
  va_list args;

  pthread_mutex_lock(&runtime.fault_lock);

  if (atomic_load(&runtime.pending_rc) == RC_SUCCESS) {
    atomic_store(&runtime.pending_rc, rc);

    if (fmt != NULL) {
      va_start(args, fmt);
      vsnprintf(runtime.pending_msg, sizeof(runtime.pending_msg), fmt, args);
      va_end(args);
    }
  }

  pthread_mutex_unlock(&runtime.fault_lock);

  atomic_store(&runtime.shutting_down, true);

  if (ctx.uc != NULL) (void)uc_emu_stop(ctx.uc);

  runtime_wake();
}

bool runtime_is_shutting_down(void) {
  return atomic_load(&runtime.shutting_down);
}

#ifndef SINGLE_THREAD
static void *runtime_emu_thread(void *arg) {
  (void)arg;

  thread_ctx_init(THREAD_ROLE_EMULATION, "emu");
  ctx.chipemu.start_emulation();

  atomic_store(&runtime.emu_done, true);
  runtime_wake();

  return NULL;
}
#endif

void runtime_start_emulation(void) {
#ifdef SINGLE_THREAD
  ctx.chipemu.start_emulation();
  atomic_store(&runtime.emu_done, true);
#else
  if (pthread_create(&runtime.emu_thread, NULL, runtime_emu_thread, NULL) != 0) {
    LOG_ERR("Failed to create emulation thread\n");
    runtime_fault(RC_CHIPEMU_FAILED_INIT, "failed to create emulation thread");
    atomic_store(&runtime.emu_done, true);
    return;
  }

  runtime.emu_thread_started = true;
#endif
}

static void runtime_shutdown(void) {
  Rc rc;

  atomic_store(&runtime.shutting_down, true);

  if (ctx.uc != NULL) (void)uc_emu_stop(ctx.uc);

  if (runtime.emu_thread_started && !pthread_equal(pthread_self(), runtime.emu_thread)) {
    pthread_join(runtime.emu_thread, NULL);
    runtime.emu_thread_started = false;
  }

  component_stop_all();
  console_stop();

  if (runtime.signal_received != 0) {
    pthread_mutex_lock(&runtime.fault_lock);
    if (atomic_load(&runtime.pending_rc) == RC_SUCCESS) {
      atomic_store(&runtime.pending_rc, RC_RUNTIME_INTERRUPTED);
    }
    pthread_mutex_unlock(&runtime.fault_lock);
  }

  rc = (Rc)atomic_load(&runtime.pending_rc);
  emu_exit(rc);
}

void runtime_run(void) {
  for (;;) {
    struct pollfd fds[3];
    nfds_t count = 0;
    int wake_index;
    int input_index = -1;
    int output_index = -1;
    int ret;

    if (runtime.signal_received != 0) break;
    if (atomic_load(&runtime.emu_done)) break;

    fds[count].fd = runtime.wake_fd[0];
    fds[count].events = POLLIN;
    fds[count].revents = 0;
    wake_index = (int)count++;

    if (console_active()) {
      if (console_stdin_open()) {
        fds[count].fd = STDIN_FILENO;
        fds[count].events = POLLIN;
        fds[count].revents = 0;
        input_index = (int)count++;
      }

      if (console_stdio_fd() >= 0) {
        fds[count].fd = console_stdio_fd();
        fds[count].events = POLLIN;
        fds[count].revents = 0;
        output_index = (int)count++;
      }
    }

    ret = poll(fds, count, -1);

    if (ret < 0) {
      if (errno == EINTR) {
        if (runtime.signal_received != 0) break;
        continue;
      }

      LOG_ERR("Runtime poll failed: %s\n", strerror(errno));
      runtime_fault(RC_CHIPEMU_FAILED_INIT, "runtime poll failed: %s", strerror(errno));
      break;
    }

    if (fds[wake_index].revents & POLLIN) runtime_drain_wake();
    if (runtime.signal_received != 0) break;

    if (input_index >= 0 && fds[input_index].revents != 0) console_pump_input();
    if (output_index >= 0 && (fds[output_index].revents & POLLIN) != 0) console_pump_output();

    if (atomic_load(&runtime.emu_done)) break;
  }

  runtime_shutdown();
}
