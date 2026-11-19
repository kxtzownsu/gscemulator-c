#include <H1B3C/peripherals/m3.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define M3_CPUID_VALUE 0x410FC331

DEFINE_BASIC_REGISTER(demcr)
DEFINE_BASIC_REGISTER(dwt_ctrl)
DEFINE_BASIC_REGISTER(vtor)

static uint32_t itcmcr;

static const BasicRegister m3_registers[] = {
  BASIC_REGISTER_ENTRY(REG_M3_DEMCR_OFFSET, demcr),
  BASIC_REGISTER_ENTRY(REG_M3_DWT_CTRL_OFFSET, dwt_ctrl),
  BASIC_REGISTER_ENTRY(REG_M3_VTOR_OFFSET, vtor),
};

static uint64_t M3_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);

  switch (offset) {
    case REG_M3_DWT_CYCCNT_OFFSET:
      LOG_VBS("M3: read DWT_CYCCNT (size=%u) -> 0\n", size);
      return 0;
    case REG_M3_CPUID_OFFSET:
      LOG_VBS("M3: read CPUID (size=%u) -> 0x%08x\n", size, M3_CPUID_VALUE);
      return M3_CPUID_VALUE;
    case REG_M3_ITCMCR_OFFSET:
      LOG_VBS("M3: read ITCMCR (size=%u) -> 0x%08x\n", size, itcmcr);
      return itcmcr;
    default:
      break;
  }

  if (reg != NULL) {
    LOG_VBS("M3: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void M3_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);

  switch (offset) {
    case REG_M3_ITCMCR_OFFSET:
      /* Hardware always reads back 7 after a write. */
      itcmcr = 7;
      LOG_VBS("M3: wrote ITCMCR (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      break;
  }

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("M3: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *M3_Initialize(void) {
  demcr = 0;
  dwt_ctrl = 0;
  itcmcr = 0;
  vtor = 0;
  return component_register("M3", NULL, M3_Read, M3_Write);
}
