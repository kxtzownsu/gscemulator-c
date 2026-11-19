#ifndef COMMON_RUNTIME_CONSOLE_H
#define COMMON_RUNTIME_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CONSOLE_NONE = 0,
  CONSOLE_STDIO,
  CONSOLE_PTY,
} ConsoleMode;

bool console_start(ConsoleMode mode);
void console_stop(void);
bool console_active(void);
ConsoleMode console_mode(void);
bool console_stdin_open(void);
int console_stdio_fd(void);
int console_uart_read(uint8_t *buf, size_t len);
int console_uart_write(const uint8_t *buf, size_t len);
bool console_wait_enter(const char *message);
void console_pump_input(void);
void console_pump_output(void);

#endif /* COMMON_RUNTIME_CONSOLE_H */
