#ifndef COMMON_MEMORY_FAULT_H
#define COMMON_MEMORY_FAULT_H

#include <stdbool.h>

void install_memory_fault_hook(void);
bool memory_fault_occurred(void);

#endif /* COMMON_MEMORY_FAULT_H */
