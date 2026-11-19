#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/sps.h>
#include <H1B3C/peripherals/m3.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>
#include <common/runtime/control.h>
#include <common/runtime/worker.h>

#define SPS_FIFO_MASK (REG_SPS_FIFO_SIZE - 1u)
#define SPS_FIFO_PTR_MASK ((SPS_FIFO_MASK << 1) | 1u)

#define SPS_FIFO_CTRL_TXFIFO_RST (1u << 0)
#define SPS_FIFO_CTRL_TXFIFO_EN (1u << 1)
#define SPS_FIFO_CTRL_RXFIFO_RST (1u << 3)
#define SPS_FIFO_CTRL_RXFIFO_EN (1u << 4)
#define SPS_FIFO_CTRL_MASK 0x3Fu

#define SPS_ISTATE_CS_ASSERT (1u << 8)
#define SPS_ISTATE_CS_DEASSERT (1u << 9)
#define SPS_ISTATE_RXFIFO_OVERFLOW (1u << 10)
#define SPS_ISTATE_TXFIFO_EMPTY (1u << 11)
#define SPS_ISTATE_TXFIFO_FULL (1u << 12)
#define SPS_ISTATE_TXFIFO_LVL (1u << 13)
#define SPS_ISTATE_RXFIFO_LVL (1u << 14)
#define SPS_ISTATE_LATCHED_MASK ((1u << 11) - 1u)
#define SPS_ISTATE_MASK ((1u << 15) - 1u)

#define SPS_HOST_OP_BASE 0x100000000ULL
#define SPS_HOST_OP_CHIP_SELECT_OFFSET (SPS_HOST_OP_BASE + 0)
#define SPS_HOST_OP_TRANSFER_OFFSET (SPS_HOST_OP_BASE + 1)
#define SPS_HOST_OP_STATUS_OFFSET (SPS_HOST_OP_BASE + 2)

#define SPS_HOST_STATUS_CS_ASSERTED (1u << 0)
#define SPS_HOST_STATUS_CS_DEASSERT (1u << 1)
#define SPS_HOST_STATUS_TX_EMPTY (1u << 2)
#define SPS_HOST_STATUS_RX_EMPTY (1u << 3)
#define SPS_HOST_STATUS_RX_OVERFLOW (1u << 4)
#define SPS_HOST_STATUS_TX_ENABLED (1u << 5)
#define SPS_HOST_STATUS_RX_ENABLED (1u << 6)
#define SPS_HOST_STATUS_CS_DEASSERT_IRQ (1u << 7)

typedef struct {
  uint32_t fifo_ctrl;
  uint32_t txfifo_rptr;
  uint32_t txfifo_wptr;
  uint32_t txfifo_threshold;
  uint32_t rxfifo_rptr;
  uint32_t rxfifo_wptr;
  uint32_t rxfifo_threshold;
  uint32_t ictrl;
  uint32_t istate;
  uint8_t txfifo[REG_SPS_FIFO_SIZE];
  uint8_t rxfifo[REG_SPS_FIFO_SIZE];
  uint8_t mosi_bit;
  uint8_t miso_bit;
  bool chip_select_asserted;
  const uint8_t *host_input;
  uint8_t *host_output;
} SpsState;

DEFINE_BASIC_REGISTER(ctrl)
DEFINE_BASIC_REGISTER(dummy_word)
DEFINE_BASIC_REGISTER(status01)
DEFINE_BASIC_REGISTER(status23)
DEFINE_BASIC_REGISTER(status45)
DEFINE_BASIC_REGISTER(status67)
DEFINE_BASIC_REGISTER(ctrl01)
DEFINE_BASIC_REGISTER(ctrl23)
DEFINE_BASIC_REGISTER(ctrl45)
DEFINE_BASIC_REGISTER(ctrl67)
DEFINE_BASIC_REGISTER(ovrd)
DEFINE_BASIC_REGISTER(itcr)
DEFINE_BASIC_REGISTER(itop)

static const BasicRegister sps_registers[] = {
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL_OFFSET, ctrl),
  BASIC_REGISTER_ENTRY(REG_SPS_DUMMY_WORD_OFFSET, dummy_word),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS01_OFFSET, status01),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS23_OFFSET, status23),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS45_OFFSET, status45),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS67_OFFSET, status67),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL01_OFFSET, ctrl01),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL23_OFFSET, ctrl23),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL45_OFFSET, ctrl45),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL67_OFFSET, ctrl67),
  BASIC_REGISTER_ENTRY(REG_SPS_OVRD_OFFSET, ovrd),
  BASIC_REGISTER_ENTRY(REG_SPS_ITCR_OFFSET, itcr),
  BASIC_REGISTER_ENTRY(REG_SPS_ITOP_OFFSET, itop),
};

static const struct {
  uint32_t bit;
  unsigned irq;
} sps_irqs[] = {
  { .bit = SPS_ISTATE_CS_ASSERT, .irq = 129 },
  { .bit = SPS_ISTATE_CS_DEASSERT, .irq = 130 },
  { .bit = SPS_ISTATE_RXFIFO_OVERFLOW, .irq = 139 },
  { .bit = SPS_ISTATE_TXFIFO_EMPTY, .irq = 148 },
  { .bit = SPS_ISTATE_TXFIFO_FULL, .irq = 149 },
  { .bit = SPS_ISTATE_TXFIFO_LVL, .irq = 150 },
  { .bit = SPS_ISTATE_RXFIFO_LVL, .irq = 138 },
};

static SpsState sps_state;

static uint64_t sps_now_ms(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void sps_sleep_ms(uint32_t ms) {
  struct timespec ts = {
    .tv_sec = ms / 1000u,
    .tv_nsec = (long)(ms % 1000u) * 1000000L,
  };

  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

static uint32_t sps_txfifo_size(const SpsState *sps) {
  return (sps->txfifo_wptr - sps->txfifo_rptr) & SPS_FIFO_PTR_MASK;
}

static uint32_t sps_rxfifo_size(const SpsState *sps) {
  return (sps->rxfifo_wptr - sps->rxfifo_rptr) & SPS_FIFO_PTR_MASK;
}

static uint32_t sps_fifo_level_state(const SpsState *sps) {
  uint32_t tx_size = sps_txfifo_size(sps);
  uint32_t rx_size = sps_rxfifo_size(sps);
  uint32_t state = 0;

  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_TXFIFO_EN) != 0) {
    if (tx_size == 0) state |= SPS_ISTATE_TXFIFO_EMPTY;
    if (tx_size >= REG_SPS_FIFO_SIZE) state |= SPS_ISTATE_TXFIFO_FULL;
    if (tx_size <= sps->txfifo_threshold) state |= SPS_ISTATE_TXFIFO_LVL;
  }

  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_RXFIFO_EN) != 0 &&
      rx_size > sps->rxfifo_threshold) {
    state |= SPS_ISTATE_RXFIFO_LVL;
  }

  return state;
}

static void sps_set_irq_pending(uint32_t state_bit, bool pending) {
  size_t i;

  for (i = 0; i < ARRAY_LEN(sps_irqs); i++) {
    if (sps_irqs[i].bit != state_bit) continue;

    if (pending) {
      M3_PendIrq(sps_irqs[i].irq);
    } else {
      M3_UnpendIrq(sps_irqs[i].irq);
    }
    return;
  }
}

static void sps_refresh_interrupts(SpsState *sps) {
  static const uint32_t level_bits[] = {
    SPS_ISTATE_TXFIFO_EMPTY,
    SPS_ISTATE_TXFIFO_FULL,
    SPS_ISTATE_TXFIFO_LVL,
    SPS_ISTATE_RXFIFO_LVL,
  };
  uint32_t level_state = sps_fifo_level_state(sps);
  size_t i;

  for (i = 0; i < ARRAY_LEN(level_bits); i++) {
    bool pending = (level_state & sps->ictrl & level_bits[i]) != 0;

    sps_set_irq_pending(level_bits[i], pending);
  }
}

static void sps_raise_latched_interrupt(SpsState *sps, uint32_t state_bit) {
  sps->istate |= state_bit;
  if ((sps->ictrl & state_bit) != 0) sps_set_irq_pending(state_bit, true);
}

static void sps_sync_latched_interrupts(SpsState *sps) {
  static const uint32_t latched_bits[] = {
    SPS_ISTATE_CS_ASSERT,
    SPS_ISTATE_CS_DEASSERT,
    SPS_ISTATE_RXFIFO_OVERFLOW,
  };
  size_t i;

  for (i = 0; i < ARRAY_LEN(latched_bits); i++) {
    bool pending = (sps->istate & sps->ictrl & latched_bits[i]) != 0;

    sps_set_irq_pending(latched_bits[i], pending);
  }
}

static void sps_set_chip_select(SpsState *sps, bool asserted) {
  if (asserted == sps->chip_select_asserted) return;

  sps->chip_select_asserted = asserted;

  if (asserted) {
    sps_raise_latched_interrupt(sps, SPS_ISTATE_CS_ASSERT);
  } else {
    sps_raise_latched_interrupt(sps, SPS_ISTATE_CS_DEASSERT);
  }
}

static uint8_t sps_transfer_byte(SpsState *sps, uint8_t value) {
  uint8_t output = (uint8_t)(dummy_word & 0xFFu);

  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_TXFIFO_EN) != 0 && sps_txfifo_size(sps) != 0) {
    output = sps->txfifo[sps->txfifo_rptr & SPS_FIFO_MASK];
    sps->txfifo_rptr = (sps->txfifo_rptr + 1u) & SPS_FIFO_PTR_MASK;
  }

  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_RXFIFO_EN) != 0) {
    if (sps_rxfifo_size(sps) < REG_SPS_FIFO_SIZE) {
      sps->rxfifo[sps->rxfifo_wptr & SPS_FIFO_MASK] = value;
      sps->rxfifo_wptr = (sps->rxfifo_wptr + 1u) & SPS_FIFO_PTR_MASK;
    } else {
      sps_raise_latched_interrupt(sps, SPS_ISTATE_RXFIFO_OVERFLOW);
    }
  }

  sps->mosi_bit = value & 1u;
  sps->miso_bit = output & 1u;
  sps_refresh_interrupts(sps);

  return output;
}

static bool sps_fifo_range(uint64_t offset, unsigned size, bool *tx, uint32_t *index) {
  if (offset >= REG_SPS_TXFIFO_OFFSET && offset < REG_SPS_RXFIFO_OFFSET) {
    if (offset + size > REG_SPS_RXFIFO_OFFSET) return false;
    *tx = true;
    *index = (uint32_t)(offset - REG_SPS_TXFIFO_OFFSET);
    return true;
  }

  if (offset >= REG_SPS_RXFIFO_OFFSET && offset + size <= REG_SPS_FIFO_END_OFFSET) {
    *tx = false;
    *index = (uint32_t)(offset - REG_SPS_RXFIFO_OFFSET);
    return true;
  }

  return false;
}

static uint64_t sps_read_fifo(const SpsState *sps, bool tx, uint32_t index, unsigned size) {
  const uint8_t *fifo = tx ? sps->txfifo : sps->rxfifo;
  uint64_t value = 0;
  unsigned i;

  for (i = 0; i < size; i++) value |= (uint64_t)fifo[index + i] << (8u * i);

  return value;
}

static void sps_write_fifo(SpsState *sps, uint32_t index, unsigned size, uint64_t value) {
  unsigned i;

  for (i = 0; i < size; i++) sps->txfifo[index + i] = (uint8_t)(value >> (8u * i));
}

static uint32_t sps_host_status(const SpsState *sps) {
  uint32_t status = 0;

  if (sps->chip_select_asserted) status |= SPS_HOST_STATUS_CS_ASSERTED;
  if ((sps->istate & SPS_ISTATE_CS_DEASSERT) != 0) status |= SPS_HOST_STATUS_CS_DEASSERT;
  if (sps_txfifo_size(sps) == 0) status |= SPS_HOST_STATUS_TX_EMPTY;
  if (sps_rxfifo_size(sps) == 0) status |= SPS_HOST_STATUS_RX_EMPTY;
  if ((sps->istate & SPS_ISTATE_RXFIFO_OVERFLOW) != 0) status |= SPS_HOST_STATUS_RX_OVERFLOW;
  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_TXFIFO_EN) != 0) status |= SPS_HOST_STATUS_TX_ENABLED;
  if ((sps->fifo_ctrl & SPS_FIFO_CTRL_RXFIFO_EN) != 0) status |= SPS_HOST_STATUS_RX_ENABLED;
  if ((sps->ictrl & SPS_ISTATE_CS_DEASSERT) != 0) status |= SPS_HOST_STATUS_CS_DEASSERT_IRQ;

  return status;
}

static uint64_t sps_read_val(const SpsState *sps) {
  uint64_t value = 0;

  if (sps->miso_bit != 0) value |= 1u << 0;
  if (sps->mosi_bit != 0) value |= 1u << 1;
  if (!sps->chip_select_asserted) value |= 1u << 2;

  return value;
}

static uint64_t SPS_Read(Component *self, uint64_t offset, unsigned size) {
  SpsState *sps = self->state;
  const BasicRegister *reg;
  bool tx;
  uint32_t index;

  if (offset == SPS_HOST_OP_STATUS_OFFSET) return sps_host_status(sps);

  if (offset == SPS_HOST_OP_TRANSFER_OFFSET) {
    unsigned i;

    if (sps->host_input == NULL || sps->host_output == NULL || size > REG_SPS_FIFO_SIZE) {
      LOG_ERR("%s: invalid host transfer\n", self->name);
      return 0;
    }

    for (i = 0; i < size; i++) {
      sps->host_output[i] = sps_transfer_byte(sps, sps->host_input[i]);
    }

    return 0;
  }

  if (sps_fifo_range(offset, size, &tx, &index)) {
    uint64_t value = sps_read_fifo(sps, tx, index, size);

    LOG_VBS("%s: read %s FIFO[0x%x] (size=%u) -> 0x%llx\n",
            self->name, tx ? "TX" : "RX", index, size, (unsigned long long)value);
    return value;
  }

  switch (offset) {
    case REG_SPS_FIFO_CTRL_OFFSET:
      LOG_VBS("%s: read FIFO_CTRL (size=%u) -> 0x%08x\n", self->name, size, sps->fifo_ctrl);
      return sps->fifo_ctrl;
    case REG_SPS_TXFIFO_SIZE_OFFSET:
      return sps_txfifo_size(sps);
    case REG_SPS_TXFIFO_RPTR_OFFSET:
      return sps->txfifo_rptr;
    case REG_SPS_TXFIFO_WPTR_OFFSET:
      return sps->txfifo_wptr;
    case REG_SPS_TXFIFO_THRESHOLD_OFFSET:
      return sps->txfifo_threshold;
    case REG_SPS_RXFIFO_SIZE_OFFSET:
      return sps_rxfifo_size(sps);
    case REG_SPS_RXFIFO_RPTR_OFFSET:
      return sps->rxfifo_rptr;
    case REG_SPS_RXFIFO_WPTR_OFFSET:
      return sps->rxfifo_wptr;
    case REG_SPS_RXFIFO_THRESHOLD_OFFSET:
      return sps->rxfifo_threshold;
    case REG_SPS_VAL_OFFSET:
      return sps_read_val(sps);
    case REG_SPS_ISTATE_OFFSET:
      return sps->istate | sps_fifo_level_state(sps);
    case REG_SPS_ISTATE_CLR_OFFSET:
      return 0;
    case REG_SPS_ICTRL_OFFSET:
      return sps->ictrl;
    default:
      break;
  }

  reg = basic_register_find(sps_registers, ARRAY_LEN(sps_registers), offset);
  if (reg != NULL) {
    LOG_VBS("%s: read %s (size=%u) -> 0x%08x\n", self->name, reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void SPS_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  SpsState *sps = self->state;
  const BasicRegister *reg;
  bool tx;
  uint32_t index;

  if (offset == SPS_HOST_OP_CHIP_SELECT_OFFSET) {
    sps_set_chip_select(sps, value != 0);
    return;
  }

  if (sps_fifo_range(offset, size, &tx, &index)) {
    if (tx) {
      sps_write_fifo(sps, index, size, value);
      LOG_VBS("%s: wrote TX FIFO[0x%x] (size=%u) <- 0x%llx\n",
              self->name, index, size, (unsigned long long)value);
    }
    return;
  }

  switch (offset) {
    case REG_SPS_FIFO_CTRL_OFFSET:
      sps->fifo_ctrl = (uint32_t)value & SPS_FIFO_CTRL_MASK;

      if ((sps->fifo_ctrl & SPS_FIFO_CTRL_TXFIFO_RST) != 0) {
        sps->txfifo_rptr = 0;
        sps->txfifo_wptr = 0;
        memset(sps->txfifo, 0, sizeof(sps->txfifo));
        sps->fifo_ctrl &= ~SPS_FIFO_CTRL_TXFIFO_RST;
      }

      if ((sps->fifo_ctrl & SPS_FIFO_CTRL_RXFIFO_RST) != 0) {
        sps->rxfifo_rptr = 0;
        sps->rxfifo_wptr = 0;
        memset(sps->rxfifo, 0, sizeof(sps->rxfifo));
        sps->fifo_ctrl &= ~SPS_FIFO_CTRL_RXFIFO_RST;
      }

      sps_refresh_interrupts(sps);
      LOG_VBS("%s: wrote FIFO_CTRL (size=%u) <- 0x%08x\n", self->name, size, sps->fifo_ctrl);
      return;
    case REG_SPS_TXFIFO_SIZE_OFFSET:
      return;
    case REG_SPS_TXFIFO_RPTR_OFFSET:
      return;
    case REG_SPS_TXFIFO_WPTR_OFFSET:
      sps->txfifo_wptr = (uint32_t)value & SPS_FIFO_PTR_MASK;
      sps_refresh_interrupts(sps);
      return;
    case REG_SPS_TXFIFO_THRESHOLD_OFFSET:
      sps->txfifo_threshold = (uint32_t)value & SPS_FIFO_MASK;
      sps_refresh_interrupts(sps);
      return;
    case REG_SPS_RXFIFO_SIZE_OFFSET:
      return;
    case REG_SPS_RXFIFO_RPTR_OFFSET:
      sps->rxfifo_rptr = (uint32_t)value & SPS_FIFO_PTR_MASK;
      sps_refresh_interrupts(sps);
      return;
    case REG_SPS_RXFIFO_WPTR_OFFSET:
      return;
    case REG_SPS_RXFIFO_THRESHOLD_OFFSET:
      sps->rxfifo_threshold = (uint32_t)value & SPS_FIFO_MASK;
      sps_refresh_interrupts(sps);
      return;
    case REG_SPS_VAL_OFFSET:
      return;
    case REG_SPS_ISTATE_OFFSET:
      return;
    case REG_SPS_ISTATE_CLR_OFFSET: {
      uint32_t clear = (uint32_t)value & SPS_ISTATE_LATCHED_MASK;

      sps->istate &= ~clear;
      sps_sync_latched_interrupts(sps);
      return;
    }
    case REG_SPS_ICTRL_OFFSET:
      sps->ictrl = (uint32_t)value & SPS_ISTATE_MASK;
      sps_sync_latched_interrupts(sps);
      sps_refresh_interrupts(sps);
      return;
    default:
      break;
  }

  reg = basic_register_find(sps_registers, ARRAY_LEN(sps_registers), offset);
  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("%s: wrote %s (size=%u) <- 0x%08x\n", self->name, reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

static Component *sps_host_component(void) {
  if (runtime_is_shutting_down()) return NULL;
  return component_find("SPS");
}

bool SPS_HostSetChipSelect(bool asserted) {
  Component *sps = sps_host_component();

  if (sps == NULL) return false;
  if (runtime_is_shutting_down()) return false;

  if (sps->worker != NULL) {
    worker_write(sps->worker, sps, SPS_HOST_OP_CHIP_SELECT_OFFSET, 4, asserted ? 1u : 0u, 0, false);
  } else {
    sps->write(sps, SPS_HOST_OP_CHIP_SELECT_OFFSET, 4, asserted ? 1u : 0u);
  }

  return true;
}

bool SPS_HostTransfer(const uint8_t *input, uint8_t *output, size_t len) {
  Component *sps;
  SpsState *sps_state_ptr;

  if (input == NULL || output == NULL || len == 0 || len > REG_SPS_FIFO_SIZE) return false;

  sps = sps_host_component();
  if (sps == NULL || sps->state == NULL) return false;
  if (runtime_is_shutting_down()) return false;

  sps_state_ptr = sps->state;
  sps_state_ptr->host_input = input;
  sps_state_ptr->host_output = output;

  if (sps->worker != NULL) {
    worker_read(sps->worker, sps, SPS_HOST_OP_TRANSFER_OFFSET, (unsigned)len, 0);
  } else {
    sps->read(sps, SPS_HOST_OP_TRANSFER_OFFSET, (unsigned)len);
  }

  sps_state_ptr->host_input = NULL;
  sps_state_ptr->host_output = NULL;

  return true;
}

static bool sps_host_read_status(uint32_t *status) {
  Component *sps = sps_host_component();
  uint64_t value;

  if (sps == NULL || status == NULL) return false;
  if (runtime_is_shutting_down()) return false;

  if (sps->worker != NULL) {
    value = worker_read(sps->worker, sps, SPS_HOST_OP_STATUS_OFFSET, 4, 0);
  } else {
    value = sps->read(sps, SPS_HOST_OP_STATUS_OFFSET, 4);
  }

  *status = (uint32_t)value;
  return true;
}

bool SPS_HostWaitIdle(uint32_t timeout_ms) {
  uint64_t deadline = sps_now_ms() + timeout_ms;

  for (;;) {
    uint32_t status;
    uint32_t busy;

    if (!sps_host_read_status(&status)) return false;

    busy = SPS_HOST_STATUS_CS_DEASSERT | SPS_HOST_STATUS_CS_ASSERTED |
           SPS_HOST_STATUS_TX_EMPTY | SPS_HOST_STATUS_RX_EMPTY;
    if ((status & busy) == (SPS_HOST_STATUS_TX_EMPTY | SPS_HOST_STATUS_RX_EMPTY)) return true;

    if (sps_now_ms() >= deadline) return false;
    sps_sleep_ms(1);
  }
}

bool SPS_HostWaitDeassert(uint32_t timeout_ms) {
  uint64_t deadline = sps_now_ms() + timeout_ms;

  for (;;) {
    uint32_t status;

    if (!sps_host_read_status(&status)) return false;
    if ((status & SPS_HOST_STATUS_CS_DEASSERT) == 0) return true;

    if (sps_now_ms() >= deadline) return false;
    sps_sleep_ms(1);
  }
}

Component *SPS_Initialize(void) {
  memset(&sps_state, 0, sizeof(sps_state));

  ctrl = 0x1;
  dummy_word = 0xFF;
  status01 = 0;
  status23 = 0;
  status45 = 0;
  status67 = 0;
  ctrl01 = 0;
  ctrl23 = 0;
  ctrl45 = 0;
  ctrl67 = 0;
  ovrd = 0;
  itcr = 0;
  itop = 0;

  return component_register_threaded("SPS", &sps_state, SPS_Read, SPS_Write);
}
