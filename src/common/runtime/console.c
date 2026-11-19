#define _GNU_SOURCE

#include <common/runtime/console.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/runtime/thread.h>

typedef struct {
  int master;
  int slave;
  int stop_pipe[2];
  pthread_t thread;
  bool started;
  bool stdin_open;
  bool stdin_raw;
  bool loopback;
  struct termios stdin_termios;
} Console;

static Console console = {
  .master = -1,
  .slave = -1,
  .stop_pipe = { -1, -1 },
};

static void console_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void console_write_all(int fd, const uint8_t *buf, size_t len) {
  size_t offset = 0;

  while (offset < len) {
    ssize_t written = write(fd, buf + offset, len - offset);

    if (written < 0) {
      if (errno == EINTR) continue;
      break;
    }

    offset += (size_t)written;
  }
}

static void *console_thread_main(void *arg) {
  (void)arg;

  thread_ctx_init(THREAD_ROLE_CONSOLE, "console");

  for (;;) {
    struct pollfd fds[2] = {
      { .fd = console.master, .events = POLLIN },
      { .fd = console.stop_pipe[0], .events = POLLIN },
    };
    int ret = poll(fds, 2, -1);

    if (ret < 0) {
      if (errno == EINTR) continue;
      LOG_ERR("Console poll failed: %s\n", strerror(errno));
      break;
    }

    if ((fds[1].revents & POLLIN) != 0) break;
    if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;

    uint8_t buf[256];
    ssize_t count = read(console.master, buf, sizeof(buf));

    if (count <= 0) continue;

    if (console.loopback) console_write_all(console.master, buf, (size_t)count);
  }

  return NULL;
}

bool console_start(void) {
  char name[128];
  struct termios termios;

  if (console.started) return true;

  if (openpty(&console.master, &console.slave, name, NULL, NULL) != 0) {
    LOG_ERR("Failed to open console pty: %s\n", strerror(errno));
    return false;
  }

  if (tcgetattr(console.slave, &termios) == 0) {
    cfmakeraw(&termios);
    (void)tcsetattr(console.slave, TCSANOW, &termios);
  }

  console_set_nonblocking(console.master);

  if (pipe(console.stop_pipe) != 0) {
    LOG_ERR("Failed to create console stop pipe: %s\n", strerror(errno));
    close(console.master);
    close(console.slave);
    console.master = -1;
    console.slave = -1;
    return false;
  }

  console_set_nonblocking(console.stop_pipe[1]);

  console.stdin_open = true;
  console.loopback = true;
  console.stdin_raw = false;

  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &console.stdin_termios) == 0) {
    termios = console.stdin_termios;
    cfmakeraw(&termios);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &termios) == 0) console.stdin_raw = true;
  }

  if (pthread_create(&console.thread, NULL, console_thread_main, NULL) != 0) {
    LOG_ERR("Failed to create console thread\n");
    close(console.master);
    close(console.slave);
    close(console.stop_pipe[0]);
    close(console.stop_pipe[1]);
    console.master = -1;
    console.slave = -1;
    console.stop_pipe[0] = -1;
    console.stop_pipe[1] = -1;
    if (console.stdin_raw) (void)tcsetattr(STDIN_FILENO, TCSANOW, &console.stdin_termios);
    console.stdin_raw = false;
    return false;
  }

  console.started = true;

  printf("Console PTS: %s\n", name);
  fflush(stdout);

  return true;
}

void console_stop(void) {
  ssize_t written;

  if (!console.started) return;

  written = write(console.stop_pipe[1], "x", 1);
  (void)written;
  pthread_join(console.thread, NULL);

  if (console.stdin_raw) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &console.stdin_termios);
    console.stdin_raw = false;
  }

  close(console.master);
  close(console.slave);
  close(console.stop_pipe[0]);
  close(console.stop_pipe[1]);

  console.master = -1;
  console.slave = -1;
  console.stop_pipe[0] = -1;
  console.stop_pipe[1] = -1;
  console.started = false;
  console.stdin_open = false;
  console.loopback = false;
}

bool console_active(void) {
  return console.started;
}

bool console_stdin_open(void) {
  return console.stdin_open;
}

int console_output_fd(void) {
  return console.started ? console.slave : -1;
}

void console_pump_input(void) {
  uint8_t buf[256];
  ssize_t count = read(STDIN_FILENO, buf, sizeof(buf));

  if (count == 0) {
    console.stdin_open = false;
    return;
  }

  if (count < 0) {
    if (errno == EINTR || errno == EAGAIN) return;
    console.stdin_open = false;
    return;
  }

  console_write_all(console.slave, buf, (size_t)count);
}

void console_pump_output(void) {
  uint8_t buf[256];
  ssize_t count = read(console.slave, buf, sizeof(buf));

  if (count > 0) console_write_all(STDOUT_FILENO, buf, (size_t)count);
}
