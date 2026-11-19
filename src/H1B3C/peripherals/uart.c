#include <H1B3C/peripherals/uart.h>
#include <H1B3C/peripherals/m3.h>
#include <H1B3C/rma.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/runtime/console.h>

typedef struct {
  unsigned tx_irq;
  unsigned rx_irq;
  uint32_t nco;
  uint32_t ictrl;
  uint32_t fifo;
  uint32_t ctrl;
  uint32_t state;
  uint32_t rdata;
  uint8_t rx_byte;
  bool rx_buffered;
  bool console;
} UartState;

static UartState uarts[3];

#define UART_RMA_LINE_SIZE 64

typedef struct {
  char line[UART_RMA_LINE_SIZE];
  size_t length;
} UartRmaScanner;

static UartRmaScanner rma_scanner;

static void uart_scan_rma_byte(uint8_t byte) {
  if (byte == '\r') return;

  if (byte != '\n') {
    if (rma_scanner.length < sizeof(rma_scanner.line) - 1) {
      rma_scanner.line[rma_scanner.length++] = (char)byte;
    }
    return;
  }

  rma_scanner.line[rma_scanner.length] = '\0';

  if (strcmp(rma_scanner.line, "generated challenge:") == 0) RMA_ChallengeSeen();

  rma_scanner.length = 0;
}

static void uart_tx_byte(UartState *uart, uint8_t byte) {
  ssize_t written;

  if (!uart->console) return;

  uart_scan_rma_byte(byte);

  if (console_active() && console_uart_write(&byte, 1) == 1) {
    M3_PendIrq(uart->tx_irq);
    return;
  }

  written = write(STDOUT_FILENO, &byte, 1);
  (void)written;
  M3_PendIrq(uart->tx_irq);
}

static bool uart_rx_fill(UartState *uart) {
  uint8_t byte;

  if (!uart->console) return false;
  if (uart->rx_buffered) return true;
  if ((uart->state & 2u) != 0) return false;
  if (console_uart_read(&byte, 1) != 1) return false;

  uart->rx_byte = byte;
  uart->rx_buffered = true;
  M3_PendIrq(uart->rx_irq);
  return true;
}

void UART_Poll(Component *self) {
  UartState *uart = self->state;

  (void)uart_rx_fill(uart);
}

static void uart_ctrl_process(UartState *uart, uint32_t ctrl) {
  if ((ctrl & 1u) != 0) {
    uart->state &= ~1u;
    uart->state |= 0x10u;
    uart->state |= 0x20u;
  }

  if ((ctrl & 2u) != 0) {
    uart->state &= ~2u;
    uart->state |= 0x80u;
    uart->state |= 0x40u;
  }
}

static uint64_t UART_Read(Component *self, uint64_t offset, unsigned size) {
  UartState *uart = self->state;

  switch (offset) {
    case REG_UART_RDATA_OFFSET:
      if ((uart->state & 2u) != 0) {
        LOG_ERR("%s: RDATA read while RX is disabled\n", self->name);
        return 0;
      }

      if (!uart_rx_fill(uart)) {
        uart->state |= 0x80u;
        return 0;
      }

      uart->rdata = uart->rx_byte;
      uart->rx_buffered = false;

      if (uart_rx_fill(uart)) {
        uart->state &= ~0x80u;
      } else {
        uart->state |= 0x80u;
      }

      LOG_VBS("%s: read RDATA (size=%u) -> 0x%08x\n", self->name, size, uart->rdata);
      return uart->rdata;
    case REG_UART_NCO_OFFSET:
      LOG_VBS("%s: read NCO (size=%u) -> 0x%08x\n", self->name, size, uart->nco);
      return uart->nco;
    case REG_UART_CTRL_OFFSET:
      LOG_VBS("%s: read CTRL (size=%u) -> 0x%08x\n", self->name, size, uart->ctrl);
      return uart->ctrl;
    case REG_UART_ICTRL_OFFSET:
      LOG_VBS("%s: read ICTRL (size=%u) -> 0x%08x\n", self->name, size, uart->ictrl);
      return uart->ictrl;
    case REG_UART_STATE_OFFSET:
      if (uart_rx_fill(uart)) {
        uart->state &= ~0x80u;
      } else {
        uart->state |= 0x80u;
      }
      LOG_VBS("%s: read STATE (size=%u) -> 0x%08x\n", self->name, size, uart->state);
      return uart->state;
    case REG_UART_ISTATECLR_OFFSET:
      LOG_VBS("%s: read ISTATECLR (size=%u) -> 0\n", self->name, size);
      return 0;
    case REG_UART_FIFO_OFFSET:
      LOG_VBS("%s: read FIFO (size=%u) -> 0x%08x\n", self->name, size, uart->fifo);
      return uart->fifo;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void UART_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  UartState *uart = self->state;

  switch (offset) {
    case REG_UART_WDATA_OFFSET:
      if ((uart->state & 1u) != 0) {
        LOG_ERR("%s: WDATA write while TX is busy\n", self->name);
        return;
      }
      uart_tx_byte(uart, (uint8_t)value);
      LOG_VBS("%s: TX 0x%02x\n", self->name, (uint8_t)value);
      return;
    case REG_UART_NCO_OFFSET:
      uart->nco = (uint32_t)value;
      LOG_VBS("%s: wrote NCO (size=%u) <- 0x%08x\n", self->name, size, uart->nco);
      return;
    case REG_UART_CTRL_OFFSET:
      uart->ctrl = (uint32_t)value;
      uart_ctrl_process(uart, uart->ctrl);
      LOG_VBS("%s: wrote CTRL (size=%u) <- 0x%08x\n", self->name, size, uart->ctrl);
      return;
    case REG_UART_ICTRL_OFFSET:
      uart->ictrl = (uint32_t)value;
      LOG_VBS("%s: wrote ICTRL (size=%u) <- 0x%08x\n", self->name, size, uart->ictrl);
      return;
    case REG_UART_STATE_OFFSET:
      uart->state = (uint32_t)value;
      LOG_VBS("%s: wrote STATE (size=%u) <- 0x%08x\n", self->name, size, uart->state);
      return;
    case REG_UART_ISTATECLR_OFFSET:
      LOG_VBS("%s: ignored write to ISTATECLR (0x%08x)\n", self->name, (uint32_t)value);
      return;
    case REG_UART_FIFO_OFFSET:
      uart->fifo = (uint32_t)value;
      LOG_VBS("%s: wrote FIFO (size=%u) <- 0x%08x\n", self->name, size, uart->fifo);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *UART_Initialize(unsigned index) {
  const char *name;

  if (index > 2) return NULL;

  name = index == 0 ? "UART0" : index == 1 ? "UART1" : "UART2";
  memset(&uarts[index], 0, sizeof(uarts[index]));
  uarts[index].state = 1;
  uarts[index].console = index == 0;
  uarts[index].tx_irq = index == 0 ? 177u : index == 1 ? 184u : 191u;
  uarts[index].rx_irq = index == 0 ? 174u : index == 1 ? 181u : 188u;

  if (index == 0) {
    Component *component = component_register_threaded(name, &uarts[index], UART_Read, UART_Write);

    if (component != NULL) component_set_poll(component, UART_Poll);
    return component;
  }

  return component_register(name, &uarts[index], UART_Read, UART_Write);
}
