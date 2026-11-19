#include <H1B3C/peripherals/crypto.h>

#include <stdint.h>
#include <string.h>

#include <common/logger.h>

static uint32_t crypto_regs[REG_CRYPTO_COUNT];
static uint32_t crypto_dmem[REG_CRYPTO_DMEM_COUNT];
static uint32_t crypto_imem[REG_CRYPTO_IMEM_COUNT];

static int crypto_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_CRYPTO_OFFSET + (uint64_t)REG_CRYPTO_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)((offset - REG_CRYPTO_OFFSET) / sizeof(uint32_t));
}

static int dmem_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_CRYPTO_DMEM_OFFSET +
                 (uint64_t)REG_CRYPTO_DMEM_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_CRYPTO_DMEM_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_CRYPTO_DMEM_OFFSET) / sizeof(uint32_t));
}

static int imem_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_CRYPTO_IMEM_OFFSET +
                 (uint64_t)REG_CRYPTO_IMEM_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_CRYPTO_IMEM_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_CRYPTO_IMEM_OFFSET) / sizeof(uint32_t));
}

static uint64_t CRYPTO_Read(Component *self, uint64_t offset, unsigned size) {
  int index;

  index = crypto_index(offset);
  if (index >= 0) {
    LOG_VBS("CRYPTO: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset, size,
            crypto_regs[index]);
    return crypto_regs[index];
  }

  index = dmem_index(offset);
  if (index >= 0) return crypto_dmem[index];

  index = imem_index(offset);
  if (index >= 0) return crypto_imem[index];

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void CRYPTO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index;

  index = crypto_index(offset);
  if (index >= 0) {
    crypto_regs[index] = (uint32_t)value;

    if (offset == REG_CRYPTO_HOST_CMD_OFFSET) {
      crypto_regs[REG_CRYPTO_INT_STATE_OFFSET / 4] |= 2u;
    } else if (offset == REG_CRYPTO_INT_STATE_OFFSET) {
      crypto_regs[index] &= ~(uint32_t)value;
    } else if (offset == REG_CRYPTO_CONTROL_OFFSET && ((uint32_t)value & 1u) != 0) {
      memset(crypto_dmem, 0, sizeof(crypto_dmem));
      memset(crypto_imem, 0, sizeof(crypto_imem));
    } else if (offset == REG_CRYPTO_WIPE_SECRETS_OFFSET && value != 0) {
      memset(crypto_dmem, 0, sizeof(crypto_dmem));
      memset(crypto_imem, 0, sizeof(crypto_imem));
    }

    LOG_VBS("CRYPTO: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset, size,
            crypto_regs[index]);
    return;
  }

  index = dmem_index(offset);
  if (index >= 0) {
    crypto_dmem[index] = (uint32_t)value;
    return;
  }

  index = imem_index(offset);
  if (index >= 0) {
    crypto_imem[index] = (uint32_t)value;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *CRYPTO_Initialize(void) {
  memset(crypto_regs, 0, sizeof(crypto_regs));
  memset(crypto_dmem, 0, sizeof(crypto_dmem));
  memset(crypto_imem, 0, sizeof(crypto_imem));

  return component_register("CRYPTO", NULL, CRYPTO_Read, CRYPTO_Write);
}
