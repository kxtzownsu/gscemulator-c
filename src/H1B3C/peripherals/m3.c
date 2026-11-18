#include <H1B3C/peripherals/m3.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define M3_CPUID_VALUE 0x410FC331

DEFINE_BASIC_REGISTER(demcr)
DEFINE_BASIC_REGISTER(dwt_ctrl)
DEFINE_BASIC_REGISTER(itcmcr)

static uint64_t M3_Read(Component *self, uint64_t offset, unsigned size) {
  switch (offset) {
    case REG_M3_DEMCR_OFFSET:
      LOG_VBS("M3: read DEMCR (size=%u) -> 0x%08x\n", size, demcr);
      return read_demcr(size);
    case REG_M3_DWT_CTRL_OFFSET:
      LOG_VBS("M3: read DWT_CTRL (size=%u) -> 0x%08x\n", size, dwt_ctrl);
      return read_dwt_ctrl(size);
    case REG_M3_CPUID_OFFSET:
      LOG_VBS("M3: read CPUID (size=%u) -> 0x%08x\n", size, M3_CPUID_VALUE);
      return M3_CPUID_VALUE;
    case REG_M3_ITCMCR_OFFSET:
      LOG_VBS("M3: read ITCMCR (size=%u) -> 0x%08x\n", size, itcmcr);
      return read_itcmcr(size);
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void M3_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  switch (offset) {
    case REG_M3_DEMCR_OFFSET:
      write_demcr(size, (uint32_t)value);
      LOG_VBS("M3: wrote DEMCR (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_M3_DWT_CTRL_OFFSET:
      write_dwt_ctrl(size, (uint32_t)value);
      LOG_VBS("M3: wrote DWT_CTRL (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_M3_ITCMCR_OFFSET:
      /* Hardware always reads back 7 after a write. */
      write_itcmcr(size, 7);
      LOG_VBS("M3: wrote ITCMCR (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *M3_Initialize(void) {
  demcr = 0;
  dwt_ctrl = 0;
  itcmcr = 0;
  return component_register("M3", NULL, M3_Read, M3_Write);
}
