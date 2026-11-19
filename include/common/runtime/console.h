#ifndef COMMON_RUNTIME_CONSOLE_H
#define COMMON_RUNTIME_CONSOLE_H

#include <stdbool.h>

bool console_start(void);
void console_stop(void);
bool console_active(void);
bool console_stdin_open(void);
int console_output_fd(void);
void console_pump_input(void);
void console_pump_output(void);

#endif /* COMMON_RUNTIME_CONSOLE_H */
