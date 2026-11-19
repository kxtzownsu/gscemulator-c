#ifndef COMMON_RUNTIME_CONSOLE_H
#define COMMON_RUNTIME_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool console_start(void);
void console_stop(void);
bool console_active(void);
bool console_stdin_open(void);
int console_stdio_fd(void);
int console_uart_read(uint8_t *buf, size_t len);
int console_uart_write(const uint8_t *buf, size_t len);
void console_pump_input(void);
void console_pump_output(void);

#endif /* COMMON_RUNTIME_CONSOLE_H */
