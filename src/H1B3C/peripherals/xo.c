#include <H1B3C/peripherals/xo.h>

#include <stdint.h>
#include <string.h>

#include <common/logger.h>

static uint32_t xo_regs[REG_XO_COUNT];

static int xo_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_XO_OFFSET + (uint64_t)REG_XO_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)((offset - REG_XO_OFFSET) / sizeof(uint32_t));
}

static uint64_t XO_Read(Component *self, uint64_t offset, unsigned size) {
  int index = xo_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "read", offset);
    return 0;
  }

  LOG_VBS("XO: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset, size,
          xo_regs[index]);
  return xo_regs[index];
}

static void XO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index = xo_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "write", offset);
    return;
  }

  xo_regs[index] = (uint32_t)value;
  LOG_VBS("XO: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset, size,
          xo_regs[index]);
}

Component *XO_Initialize(void) {
  memset(xo_regs, 0, sizeof(xo_regs));

  return component_register("XO", NULL, XO_Read, XO_Write);
}
