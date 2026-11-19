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
  int ready_pipe[2];
  pthread_t thread;
  char name[128];
  bool started;
  bool stdin_open;
  bool stdin_raw;
  ConsoleMode mode;
  bool pending_cr;
  struct termios stdin_termios;
} Console;

static Console console = {
  .master = -1,
  .slave = -1,
  .stop_pipe = { -1, -1 },
  .ready_pipe = { -1, -1 },
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

static void console_write_byte(int fd, uint8_t byte) {
  ssize_t written = write(fd, &byte, 1);

  (void)written;
}

static void *console_thread_main(void *arg) {
  struct termios termios;
  uint8_t ready = 0;

  (void)arg;

  thread_ctx_init(THREAD_ROLE_CONSOLE, "console");

  if (openpty(&console.master, &console.slave, console.name, NULL, NULL) != 0) {
    LOG_ERR("Failed to open console pty: %s\n", strerror(errno));
    console_write_byte(console.ready_pipe[1], ready);
    return NULL;
  }

  if (tcgetattr(console.slave, &termios) == 0) {
    cfmakeraw(&termios);
    (void)tcsetattr(console.slave, TCSANOW, &termios);
  }

  console_set_nonblocking(console.master);
  console_set_nonblocking(console.slave);

  ready = 1;
  console_write_byte(console.ready_pipe[1], ready);

  for (;;) {
    struct pollfd fds = { .fd = console.stop_pipe[0], .events = POLLIN };
    int ret = poll(&fds, 1, -1);

    if (ret < 0) {
      if (errno == EINTR) continue;
      LOG_ERR("Console poll failed: %s\n", strerror(errno));
      break;
    }

    if ((fds.revents & POLLIN) != 0) break;
  }

  return NULL;
}

bool console_start(ConsoleMode mode) {
  uint8_t ready = 0;
  ssize_t got;

  if (mode == CONSOLE_NONE) return true;

  if (console.started) return true;

  if (pipe(console.stop_pipe) != 0) {
    LOG_ERR("Failed to create console stop pipe: %s\n", strerror(errno));
    return false;
  }

  if (pipe(console.ready_pipe) != 0) {
    LOG_ERR("Failed to create console ready pipe: %s\n", strerror(errno));
    close(console.stop_pipe[0]);
    close(console.stop_pipe[1]);
    console.stop_pipe[0] = -1;
    console.stop_pipe[1] = -1;
    return false;
  }

  console_set_nonblocking(console.stop_pipe[1]);

  console.mode = mode;
  console.stdin_open = mode == CONSOLE_STDIO;
  console.stdin_raw = false;
  console.pending_cr = false;

  if (mode == CONSOLE_STDIO && isatty(STDIN_FILENO) &&
      tcgetattr(STDIN_FILENO, &console.stdin_termios) == 0) {
    struct termios termios = console.stdin_termios;

    cfmakeraw(&termios);
    /* Keep Ctrl-C as SIGINT and preserve normal output/newline processing. */
    termios.c_lflag |= ISIG;
    termios.c_oflag = console.stdin_termios.c_oflag;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &termios) == 0) console.stdin_raw = true;
  }

  if (pthread_create(&console.thread, NULL, console_thread_main, NULL) != 0) {
    LOG_ERR("Failed to create console thread\n");
    close(console.stop_pipe[0]);
    close(console.stop_pipe[1]);
    close(console.ready_pipe[0]);
    close(console.ready_pipe[1]);
    console.stop_pipe[0] = -1;
    console.stop_pipe[1] = -1;
    console.ready_pipe[0] = -1;
    console.ready_pipe[1] = -1;
    if (console.stdin_raw) (void)tcsetattr(STDIN_FILENO, TCSANOW, &console.stdin_termios);
    console.stdin_raw = false;
    return false;
  }

  do {
    got = read(console.ready_pipe[0], &ready, 1);
  } while (got < 0 && errno == EINTR);

  if (got != 1 || ready != 1) {
    console_write_byte(console.stop_pipe[1], 0);
    pthread_join(console.thread, NULL);
    close(console.stop_pipe[0]);
    close(console.stop_pipe[1]);
    close(console.ready_pipe[0]);
    close(console.ready_pipe[1]);
    if (console.master >= 0) close(console.master);
    if (console.slave >= 0) close(console.slave);
    console.master = -1;
    console.slave = -1;
    console.stop_pipe[0] = -1;
    console.stop_pipe[1] = -1;
    console.ready_pipe[0] = -1;
    console.ready_pipe[1] = -1;
    if (console.stdin_raw) (void)tcsetattr(STDIN_FILENO, TCSANOW, &console.stdin_termios);
    console.stdin_raw = false;
    return false;
  }

  console.started = true;

  if (mode == CONSOLE_PTY) {
    printf("Console PTS (pty-only): %s\n", console.name);
  } else {
    printf("Console PTS: %s\n", console.name);
  }
  fflush(stdout);

  return true;
}

void console_stop(void) {
  ssize_t written;

  if (!console.started) return;

  written = write(console.stop_pipe[1], "x", 1);
  (void)written;
  pthread_join(console.thread, NULL);

  if (console.pending_cr) {
    console_write_all(STDOUT_FILENO, (const uint8_t *)"\r", 1);
    console.pending_cr = false;
  }

  if (console.stdin_raw) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &console.stdin_termios);
    console.stdin_raw = false;
  }

  close(console.master);
  close(console.slave);
  close(console.stop_pipe[0]);
  close(console.stop_pipe[1]);
  close(console.ready_pipe[0]);
  close(console.ready_pipe[1]);

  console.master = -1;
  console.slave = -1;
  console.stop_pipe[0] = -1;
  console.stop_pipe[1] = -1;
  console.ready_pipe[0] = -1;
  console.ready_pipe[1] = -1;
  console.started = false;
  console.stdin_open = false;
  console.mode = CONSOLE_NONE;
}

bool console_active(void) {
  return console.started && console.mode != CONSOLE_NONE;
}

ConsoleMode console_mode(void) {
  return console.mode;
}

bool console_stdin_open(void) {
  return console.started && console.mode == CONSOLE_STDIO && console.stdin_open;
}

int console_stdio_fd(void) {
  return console.started && console.mode == CONSOLE_STDIO ? console.slave : -1;
}

int console_uart_read(uint8_t *buf, size_t len) {
  ssize_t count;

  if (!console.started || console.master < 0) return 0;

  count = read(console.master, buf, len);
  if (count <= 0) return 0;

  return (int)count;
}

int console_uart_write(const uint8_t *buf, size_t len) {
  ssize_t count;

  if (!console.started || console.master < 0) return 0;

  count = write(console.master, buf, len);
  if (count <= 0) return 0;

  return (int)count;
}

bool console_wait_enter(const char *message) {
  char byte;

  if (message != NULL) {
    printf("%s\n", message);
    fflush(stdout);
  }

  for (;;) {
    ssize_t count = read(STDIN_FILENO, &byte, 1);

    if (count == 1) {
      if (byte == '\n' || byte == '\r') return true;
      continue;
    }

    if (count == 0) return false;
    if (errno == EINTR) continue;
    return false;
  }
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
  uint8_t out[sizeof(buf) + 1];
  size_t out_len = 0;
  ssize_t count = read(console.slave, buf, sizeof(buf));
  ssize_t i;

  if (count <= 0) return;

  for (i = 0; i < count; i++) {
    uint8_t byte = buf[i];

    if (console.pending_cr) {
      console.pending_cr = false;
      if (byte == '\n') {
        out[out_len++] = '\n';
        continue;
      }
      out[out_len++] = '\r';
    }

    if (byte == '\r') {
      console.pending_cr = true;
      continue;
    }

    out[out_len++] = byte;
  }

  if (out_len > 0) console_write_all(STDOUT_FILENO, out, out_len);
}
