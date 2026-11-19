#include <H1B3C/peripherals/uart.h>

#include <stdint.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(nco)

static const BasicRegister uart_registers[] = {
  BASIC_REGISTER_ENTRY(REG_UART_NCO_OFFSET, nco),
};

static uint32_t uart_ctrl;
static uint32_t uart_state = 1;
static uint32_t uart_rdata;

static void uart_tx_byte(uint8_t byte) {
  ssize_t written = write(STDOUT_FILENO, &byte, 1);
  (void)written;
}

static void uart_ctrl_process(uint32_t ctrl) {
  if ((ctrl & 1u) != 0) {
    uart_state &= ~1u;
    uart_state |= 0x10u;
    uart_state |= 0x20u;
  }

  if ((ctrl & 2u) != 0) {
    uart_state &= ~2u;
    uart_state |= 0x80u;
    uart_state |= 0x40u;
  }
}

static uint64_t UART_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(uart_registers, ARRAY_LEN(uart_registers), offset);

  if (reg != NULL) {
    LOG_VBS("UART: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  switch (offset) {
    case REG_UART_RDATA_OFFSET:
      if ((uart_state & 2u) != 0) {
        LOG_ERR("UART: RDATA read while RX is disabled\n");
        return 0;
      }
      LOG_VBS("UART: read RDATA (size=%u) -> 0x%08x\n", size, uart_rdata);
      return uart_rdata;
    case REG_UART_CTRL_OFFSET:
      LOG_VBS("UART: read CTRL (size=%u) -> 0x%08x\n", size, uart_ctrl);
      return uart_ctrl;
    case REG_UART_STATE_OFFSET:
      LOG_VBS("UART: read STATE (size=%u) -> 0x%08x\n", size, uart_state);
      return uart_state;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void UART_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(uart_registers, ARRAY_LEN(uart_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("UART: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  switch (offset) {
    case REG_UART_WDATA_OFFSET:
      if ((uart_state & 1u) != 0) {
        LOG_ERR("UART: WDATA write while TX is busy\n");
        return;
      }
      uart_tx_byte((uint8_t)value);
      LOG_VBS("UART: TX 0x%02x\n", (uint8_t)value);
      return;
    case REG_UART_CTRL_OFFSET:
      uart_ctrl = (uint32_t)value;
      uart_ctrl_process(uart_ctrl);
      LOG_VBS("UART: wrote CTRL (size=%u) <- 0x%08x\n", size, uart_ctrl);
      return;
    case REG_UART_STATE_OFFSET:
      uart_state = (uint32_t)value;
      LOG_VBS("UART: wrote STATE (size=%u) <- 0x%08x\n", size, uart_state);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *UART_Initialize(void) {
  nco = 0;
  uart_ctrl = 0;
  uart_state = 1;
  uart_rdata = 0;

  return component_register_threaded("UART", NULL, UART_Read, UART_Write);
}
