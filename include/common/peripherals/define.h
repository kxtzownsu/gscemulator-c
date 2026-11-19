#ifndef COMMON_PERIPHERALS_DEFINE_H
#define COMMON_PERIPHERALS_DEFINE_H

#include <stddef.h>
#include <stdint.h>

#define DEFINE_BASIC_REGISTER(name) static uint32_t name = 0;

typedef struct {
  uint32_t offset;
  const char *name;
  uint32_t *value;
} BasicRegister;

#define BASIC_REGISTER_ENTRY(offset, name) { (offset), #name, &(name) }
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

static inline const BasicRegister *basic_register_find(const BasicRegister *registers, size_t count,
                                                       uint64_t offset) {
  size_t i;

  for (i = 0; i < count; i++) {
    if (registers[i].offset == offset) return &registers[i];
  }

  return NULL;
}

#endif /* COMMON_PERIPHERALS_DEFINE_H */
