#include <H1B3C/peripherals/uart.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(nco)
DEFINE_BASIC_REGISTER(ctrl)

static uint64_t UART_Read(Component *self, uint64_t offset, unsigned size) {
  switch (offset) {
    case REG_UART_NCO_OFFSET:
      LOG_VBS("UART: read NCO (size=%u) -> 0x%08x\n", size, nco);
      return read_nco(size);
    case REG_UART_CTRL_OFFSET:
      LOG_VBS("UART: read CTRL (size=%u) -> 0x%08x\n", size, ctrl);
      return read_ctrl(size);
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void UART_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  switch (offset) {
    case REG_UART_NCO_OFFSET:
      write_nco(size, (uint32_t)value);
      LOG_VBS("UART: wrote NCO (size=%u) <- 0x%08x\n", size, nco);
      return;
    case REG_UART_CTRL_OFFSET:
      write_ctrl(size, (uint32_t)value);
      LOG_VBS("UART: wrote CTRL (size=%u) <- 0x%08x\n", size, ctrl);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *UART_Initialize(void) {
  nco = 0;
  ctrl = 0;

  return component_register_threaded("UART", NULL, UART_Read, UART_Write);
}
