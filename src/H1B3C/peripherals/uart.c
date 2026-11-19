#include <H1B3C/peripherals/uart.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(nco)
DEFINE_BASIC_REGISTER(ctrl)

static const BasicRegister uart_registers[] = {
  BASIC_REGISTER_ENTRY(REG_UART_NCO_OFFSET, nco),
  BASIC_REGISTER_ENTRY(REG_UART_CTRL_OFFSET, ctrl),
};

static uint64_t UART_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(uart_registers, ARRAY_LEN(uart_registers), offset);

  if (reg != NULL) {
    LOG_VBS("UART: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void UART_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(uart_registers, ARRAY_LEN(uart_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("UART: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *UART_Initialize(void) {
  nco = 0;
  ctrl = 0;

  return component_register_threaded("UART", NULL, UART_Read, UART_Write);
}
