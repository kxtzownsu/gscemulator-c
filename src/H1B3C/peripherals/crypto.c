#include <H1B3C/peripherals/crypto.h>
#include <H1B3C/peripherals/crypto-engine.h>
#include <H1B3C/peripherals/m3.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/logger.h>

#define CRYPTO_HOST_CMD_BASE 0x08000000u
#define CRYPTO_INT_HOST_CMD_RECV (1u << 0)
#define CRYPTO_INT_HOST_CMD_DONE (1u << 1)
#define CRYPTO_INT_PC_STACK_OVERFLOW (1u << 2)
#define CRYPTO_IRQ_HOST_CMD_DONE 4u

static uint32_t crypto_regs[REG_CRYPTO_COUNT];
static uint32_t crypto_int_state;

static int crypto_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_CRYPTO_OFFSET + (uint64_t)REG_CRYPTO_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)((offset - REG_CRYPTO_OFFSET) / sizeof(uint32_t));
}

static void crypto_complete(void) {
  crypto_int_state |= CRYPTO_INT_HOST_CMD_DONE;
  M3_PendIrq(CRYPTO_IRQ_HOST_CMD_DONE);
}

static void crypto_run_host_cmd(uint32_t value) {
  CryptoEngineStatus status;
  uint32_t entry;

  crypto_int_state |= CRYPTO_INT_HOST_CMD_RECV;

  if (value < CRYPTO_HOST_CMD_BASE) {
    LOG_VBS("CRYPTO: host command 0x%08x below the IMEM base\n", value);
    crypto_int_state |= CRYPTO_INT_PC_STACK_OVERFLOW;
    crypto_complete();
    return;
  }

  entry = value - CRYPTO_HOST_CMD_BASE;
  if (entry >= CRYPTO_ENGINE_IMEM_WORDS) {
    LOG_VBS("CRYPTO: host command entry %u is outside IMEM\n", entry);
    crypto_int_state |= CRYPTO_INT_PC_STACK_OVERFLOW;
    crypto_complete();
    return;
  }

  /* Old RW images load the computeRR mask from DMEM cell 6, but their
     own context layout overwrites that cell with modulus data. Rewrite
     the prologue to the newer zero-and-immediate sequence. */
  if (entry == 0x343u && crypto_engine_get_imem(595) == 0x95800000u &&
      crypto_engine_get_imem(596) == 0x840c20c0u) {
    crypto_engine_set_imem(595, 0x4c0c6300u);
    crypto_engine_set_imem(596, 0x800cffffu);
  }

  crypto_engine_set_pc(entry);
  status = crypto_engine_run();
  if (status != CRYPTO_ENGINE_OK) {
    LOG_VBS("CRYPTO: engine stopped: %s\n", crypto_engine_status_string(status));
  }

  crypto_complete();
}

static uint64_t CRYPTO_Read(Component *self, uint64_t offset, unsigned size) {
  int index;

  index = crypto_index(offset);
  if (index >= 0) {
    switch (offset) {
      case REG_CRYPTO_CONTROL_OFFSET:
        return 0;
      case REG_CRYPTO_INT_STATE_OFFSET:
        return crypto_int_state;
      case REG_CRYPTO_HOST_CMD_OFFSET:
        return 0;
      case REG_CRYPTO_WIPE_SECRETS_OFFSET:
        return 0;
      default:
        break;
    }

    LOG_VBS("CRYPTO: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset, size,
            crypto_regs[index]);
    return crypto_regs[index];
  }

  if (offset >= REG_CRYPTO_DMEM_OFFSET &&
      offset < REG_CRYPTO_DMEM_OFFSET + (uint64_t)REG_CRYPTO_DMEM_COUNT * 4u) {
    return crypto_engine_get_dmem((uint32_t)((offset - REG_CRYPTO_DMEM_OFFSET) / 4u));
  }

  if (offset >= REG_CRYPTO_IMEM_OFFSET &&
      offset < REG_CRYPTO_IMEM_OFFSET + (uint64_t)REG_CRYPTO_IMEM_COUNT * 4u) {
    return crypto_engine_get_imem((uint32_t)((offset - REG_CRYPTO_IMEM_OFFSET) / 4u));
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void CRYPTO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index;

  index = crypto_index(offset);
  if (index >= 0) {
    switch (offset) {
      case REG_CRYPTO_CONTROL_OFFSET: {
        uint32_t control = (uint32_t)value & 7u;

        if ((control & (control - 1u)) != 0) return;

        if ((control & 1u) != 0) {
          crypto_engine_reset(true, true);
          crypto_int_state = 0;
        }

        crypto_regs[index] = 0;
        LOG_VBS("CRYPTO: wrote CONTROL (size=%u) <- 0x%08x\n", size, control);
        return;
      }
      case REG_CRYPTO_INT_STATE_OFFSET:
        crypto_int_state &= ~(uint32_t)value;
        return;
      case REG_CRYPTO_HOST_CMD_OFFSET:
        LOG_VBS("CRYPTO: host command 0x%08x\n", (uint32_t)value);
        crypto_run_host_cmd((uint32_t)value);
        crypto_regs[index] = 0;
        return;
      case REG_CRYPTO_WIPE_SECRETS_OFFSET:
        if (value != 0) {
          crypto_engine_reset(true, true);
          crypto_int_state = 0;
        }
        crypto_regs[index] = 0;
        return;
      default:
        break;
    }

    crypto_regs[index] = (uint32_t)value;
    LOG_VBS("CRYPTO: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset, size,
            crypto_regs[index]);
    return;
  }

  if (offset >= REG_CRYPTO_DMEM_OFFSET &&
      offset < REG_CRYPTO_DMEM_OFFSET + (uint64_t)REG_CRYPTO_DMEM_COUNT * 4u) {
    crypto_engine_set_dmem((uint32_t)((offset - REG_CRYPTO_DMEM_OFFSET) / 4u), (uint32_t)value);
    return;
  }

  if (offset >= REG_CRYPTO_IMEM_OFFSET &&
      offset < REG_CRYPTO_IMEM_OFFSET + (uint64_t)REG_CRYPTO_IMEM_COUNT * 4u) {
    crypto_engine_set_imem((uint32_t)((offset - REG_CRYPTO_IMEM_OFFSET) / 4u), (uint32_t)value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *CRYPTO_Initialize(void) {
  memset(crypto_regs, 0, sizeof(crypto_regs));
  crypto_int_state = 0;
  crypto_engine_reset(true, true);

  return component_register("CRYPTO", NULL, CRYPTO_Read, CRYPTO_Write);
}
