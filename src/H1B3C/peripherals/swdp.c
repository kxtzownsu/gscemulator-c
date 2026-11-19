#include <H1B3C/peripherals/swdp.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(p4_last_sync)
DEFINE_BASIC_REGISTER(build_date)
DEFINE_BASIC_REGISTER(build_time)

static const BasicRegister swdp_registers[] = {
  BASIC_REGISTER_ENTRY(REG_SWDP_P4_LAST_SYNC_OFFSET, p4_last_sync),
  BASIC_REGISTER_ENTRY(REG_SWDP_BUILD_DATE_OFFSET, build_date),
  BASIC_REGISTER_ENTRY(REG_SWDP_BUILD_TIME_OFFSET, build_time),
};

static uint64_t SWDP_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(swdp_registers, ARRAY_LEN(swdp_registers), offset);

  if (reg != NULL) {
    LOG_VBS("SWDP: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void SWDP_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(swdp_registers, ARRAY_LEN(swdp_registers), offset);

  (void)size;
  (void)value;

  if (reg != NULL) {
    LOG_VBS("SWDP: ignored write to %s\n", reg->name);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *SWDP_Initialize(void) {
  p4_last_sync = 0;
  build_date = 0;
  build_time = 0;

  return component_register("SWDP", NULL, SWDP_Read, SWDP_Write);
}
