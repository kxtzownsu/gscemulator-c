#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/m3.h>

#include <stdint.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define M3_CPUID_VALUE 0x410FC331
/* PCLK_FREQ is 24 MHz on H1B3C (chip/haven/config_chip.h). */
#define M3_CYCCNT_HZ 24000000ULL

DEFINE_BASIC_REGISTER(demcr)
DEFINE_BASIC_REGISTER(dwt_ctrl)
DEFINE_BASIC_REGISTER(vtor)

static uint32_t itcmcr;
static uint64_t cyccnt_start_ns;

static const BasicRegister m3_registers[] = {
  BASIC_REGISTER_ENTRY(REG_M3_DEMCR_OFFSET, demcr),
  BASIC_REGISTER_ENTRY(REG_M3_DWT_CTRL_OFFSET, dwt_ctrl),
  BASIC_REGISTER_ENTRY(REG_M3_VTOR_OFFSET, vtor),
};

static uint64_t m3_monotonic_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void M3_StartCycleCount(void) {
  cyccnt_start_ns = m3_monotonic_ns();
}

static uint64_t M3_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);

  switch (offset) {
    case REG_M3_DWT_CYCCNT_OFFSET: {
      uint64_t now = m3_monotonic_ns();
      uint64_t elapsed = now >= cyccnt_start_ns ? now - cyccnt_start_ns : 0;
      uint64_t cycles = (elapsed / 1000000000ULL) * M3_CYCCNT_HZ +
                        ((elapsed % 1000000000ULL) * M3_CYCCNT_HZ) / 1000000000ULL;

      LOG_VBS("M3: read DWT_CYCCNT (size=%u) -> %u\n", size, (uint32_t)cycles);
      return (uint32_t)cycles;
    }
    case REG_M3_CPUID_OFFSET:
      LOG_VBS("M3: read CPUID (size=%u) -> 0x%08x\n", size, M3_CPUID_VALUE);
      return M3_CPUID_VALUE;
    case REG_M3_ITCMCR_OFFSET:
      LOG_VBS("M3: read ITCMCR (size=%u) -> 0x%08x\n", size, itcmcr);
      return itcmcr;
    default:
      break;
  }

  if (reg != NULL) {
    LOG_VBS("M3: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void M3_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);

  switch (offset) {
    case REG_M3_DWT_CYCCNT_OFFSET:
      LOG_VBS("M3: ignored write to DWT_CYCCNT\n");
      return;
    case REG_M3_ITCMCR_OFFSET:
      /* Hardware always reads back 7 after a write. */
      itcmcr = 7;
      LOG_VBS("M3: wrote ITCMCR (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      break;
  }

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("M3: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *M3_Initialize(void) {
  demcr = 0;
  dwt_ctrl = 0;
  itcmcr = 0;
  vtor = 0;
  cyccnt_start_ns = 0;
  return component_register("M3", NULL, M3_Read, M3_Write);
}
