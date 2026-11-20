#include <H1B3C/peripherals/swdp.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(trickbox_halt)
DEFINE_BASIC_REGISTER(trickbox_uart)
DEFINE_BASIC_REGISTER(trickbox_error)
DEFINE_BASIC_REGISTER(trickbox_fatal)
DEFINE_BASIC_REGISTER(scratch_reg0)
DEFINE_BASIC_REGISTER(scratch_reg1)
DEFINE_BASIC_REGISTER(scratch_reg2)
DEFINE_BASIC_REGISTER(scratch_reg3)
DEFINE_BASIC_REGISTER(appsvtor)
DEFINE_BASIC_REGISTER(xml_md5sum)
DEFINE_BASIC_REGISTER(header_md5sum)
DEFINE_BASIC_REGISTER(p4_last_sync)
DEFINE_BASIC_REGISTER(build_date)
DEFINE_BASIC_REGISTER(build_time)
DEFINE_BASIC_REGISTER(test_port_disable)

static const BasicRegister swdp_registers[] = {
  BASIC_REGISTER_ENTRY(REG_SWDP_TRICKBOX_HALT_OFFSET, trickbox_halt),
  BASIC_REGISTER_ENTRY(REG_SWDP_TRICKBOX_UART_OFFSET, trickbox_uart),
  BASIC_REGISTER_ENTRY(REG_SWDP_TRICKBOX_ERROR_OFFSET, trickbox_error),
  BASIC_REGISTER_ENTRY(REG_SWDP_TRICKBOX_FATAL_OFFSET, trickbox_fatal),
  BASIC_REGISTER_ENTRY(REG_SWDP_SCRATCH_REG0_OFFSET, scratch_reg0),
  BASIC_REGISTER_ENTRY(REG_SWDP_SCRATCH_REG1_OFFSET, scratch_reg1),
  BASIC_REGISTER_ENTRY(REG_SWDP_SCRATCH_REG2_OFFSET, scratch_reg2),
  BASIC_REGISTER_ENTRY(REG_SWDP_SCRATCH_REG3_OFFSET, scratch_reg3),
  BASIC_REGISTER_ENTRY(REG_SWDP_APPSVTOR_OFFSET, appsvtor),
  BASIC_REGISTER_ENTRY(REG_SWDP_XML_MD5SUM_OFFSET, xml_md5sum),
  BASIC_REGISTER_ENTRY(REG_SWDP_HEADER_MD5SUM_OFFSET, header_md5sum),
  BASIC_REGISTER_ENTRY(REG_SWDP_P4_LAST_SYNC_OFFSET, p4_last_sync),
  BASIC_REGISTER_ENTRY(REG_SWDP_BUILD_DATE_OFFSET, build_date),
  BASIC_REGISTER_ENTRY(REG_SWDP_BUILD_TIME_OFFSET, build_time),
  BASIC_REGISTER_ENTRY(REG_SWDP_TEST_PORT_DISABLE_OFFSET, test_port_disable),
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

  if (reg != NULL) {
    if (offset == REG_SWDP_P4_LAST_SYNC_OFFSET ||
        offset == REG_SWDP_BUILD_DATE_OFFSET ||
        offset == REG_SWDP_BUILD_TIME_OFFSET) {
      LOG_VBS("SWDP: ignored write to %s\n", reg->name);
      return;
    }

    *reg->value = (uint32_t)value;
    LOG_VBS("SWDP: wrote %s <- 0x%08x\n", reg->name, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *SWDP_Initialize(void) {
  trickbox_halt = 0;
  trickbox_uart = 0;
  trickbox_error = 0;
  trickbox_fatal = 0;
  scratch_reg0 = 0;
  scratch_reg1 = 0;
  scratch_reg2 = 0;
  scratch_reg3 = 0;
  appsvtor = 0;
  xml_md5sum = 0;
  header_md5sum = 0;
  p4_last_sync = 0;
  build_date = 0;
  build_time = 0;
  test_port_disable = 0;

  return component_register("SWDP", NULL, SWDP_Read, SWDP_Write);
}
