#include <H1B3C/peripherals/rbox.h>

#include <stdint.h>
#include <string.h>

#include <common/logger.h>

static uint32_t rbox_regs[REG_RBOX_COUNT];

static int rbox_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_RBOX_OFFSET + (uint64_t)REG_RBOX_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)((offset - REG_RBOX_OFFSET) / sizeof(uint32_t));
}

static uint64_t RBOX_Read(Component *self, uint64_t offset, unsigned size) {
  int index = rbox_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "read", offset);
    return 0;
  }

  LOG_VBS("RBOX: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset, size,
          rbox_regs[index]);
  return rbox_regs[index];
}

static void RBOX_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index = rbox_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "write", offset);
    return;
  }

  if (offset == REG_RBOX_INT_STATE_OFFSET) {
    rbox_regs[index] &= ~(uint32_t)value;
  } else {
    rbox_regs[index] = (uint32_t)value;
  }

  LOG_VBS("RBOX: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset, size,
          rbox_regs[index]);
}

Component *RBOX_Initialize(void) {
  memset(rbox_regs, 0, sizeof(rbox_regs));

  return component_register("RBOX", NULL, RBOX_Read, RBOX_Write);
}
