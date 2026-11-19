#include <H1B3C/peripherals/xo.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(clk_jtr_sync_contents)

static const BasicRegister xo_registers[] = {
  BASIC_REGISTER_ENTRY(REG_XO_CLK_JTR_SYNC_CONTENTS_OFFSET, clk_jtr_sync_contents),
};

static uint64_t XO_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(xo_registers, ARRAY_LEN(xo_registers), offset);

  if (reg != NULL) {
    LOG_VBS("XO: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void XO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(xo_registers, ARRAY_LEN(xo_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("XO: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *XO_Initialize(void) {
  clk_jtr_sync_contents = 0;

  return component_register("XO", NULL, XO_Read, XO_Write);
}
