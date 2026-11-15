#ifndef COMMON_PERIPHERALS_COMPONENT_H
#define COMMON_PERIPHERALS_COMPONENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#define COMPONENT_MAX 32

typedef struct Component Component;
typedef uint64_t (*ComponentRead)(Component *self, uint64_t offset, unsigned size);
typedef void (*ComponentWrite)(Component *self, uint64_t offset, unsigned size, uint64_t value);

struct Component {
  const char *name;
  void *state;
  ComponentRead read;
  ComponentWrite write;
};

typedef struct {
  Component *component;
  uint32_t base;
  uint32_t size;
} MmioRegion;

Component *component_register(const char *name, void *state, ComponentRead read, ComponentWrite write);
Component *component_find(const char *name);
bool component_map_mmio(uc_engine *uc, const MmioRegion *regions, size_t count);

#endif /* COMMON_PERIPHERALS_COMPONENT_H */
