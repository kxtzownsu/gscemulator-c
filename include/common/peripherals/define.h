#ifndef COMMON_PERIPHERALS_DEFINE_H
#define COMMON_PERIPHERALS_DEFINE_H

#include <stdint.h>

#define DEFINE_BASIC_REGISTER(name) \
  static uint32_t name = 0; \
  static uint32_t read_##name(uint32_t size) { \
    (void)size; \
    return name; \
  } \
  static void write_##name(uint32_t size, uint32_t value) { \
    (void)size; \
    name = value; \
  }

#endif