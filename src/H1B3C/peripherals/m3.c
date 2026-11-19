#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/m3.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define M3_CPUID_VALUE 0x410FC331
/* PCLK_FREQ is 24 MHz on H1B3C (chip/haven/config_chip.h). */
#define M3_CYCCNT_HZ 24000000ULL

DEFINE_BASIC_REGISTER(demcr)
DEFINE_BASIC_REGISTER(dwt_ctrl)
DEFINE_BASIC_REGISTER(vtor)
DEFINE_BASIC_REGISTER(ccr)
DEFINE_BASIC_REGISTER(shcsr)
DEFINE_BASIC_REGISTER(cfsr)
DEFINE_BASIC_REGISTER(hfsr)
DEFINE_BASIC_REGISTER(dfsr)
DEFINE_BASIC_REGISTER(mmfar)
DEFINE_BASIC_REGISTER(bfar)
DEFINE_BASIC_REGISTER(syst_csr)
DEFINE_BASIC_REGISTER(syst_rvr)
DEFINE_BASIC_REGISTER(syst_cvr)
DEFINE_BASIC_REGISTER(syst_calib)
DEFINE_BASIC_REGISTER(nvic_stir)

static uint32_t itcmcr;
static uint32_t nvic_en[REG_M3_NVIC_ISER_COUNT];
static _Atomic uint32_t nvic_pend[REG_M3_NVIC_ISPR_COUNT];
static uint32_t nvic_ipr[REG_M3_NVIC_IPR_COUNT];
static uint64_t cyccnt_start_ns;

static const BasicRegister m3_registers[] = {
  BASIC_REGISTER_ENTRY(REG_M3_DEMCR_OFFSET, demcr),
  BASIC_REGISTER_ENTRY(REG_M3_DWT_CTRL_OFFSET, dwt_ctrl),
  BASIC_REGISTER_ENTRY(REG_M3_VTOR_OFFSET, vtor),
  BASIC_REGISTER_ENTRY(REG_M3_CCR_OFFSET, ccr),
  BASIC_REGISTER_ENTRY(REG_M3_SHCSR_OFFSET, shcsr),
  BASIC_REGISTER_ENTRY(REG_M3_CFSR_OFFSET, cfsr),
  BASIC_REGISTER_ENTRY(REG_M3_HFSR_OFFSET, hfsr),
  BASIC_REGISTER_ENTRY(REG_M3_DFSR_OFFSET, dfsr),
  BASIC_REGISTER_ENTRY(REG_M3_MMFAR_OFFSET, mmfar),
  BASIC_REGISTER_ENTRY(REG_M3_BFAR_OFFSET, bfar),
  BASIC_REGISTER_ENTRY(REG_M3_SYST_CSR_OFFSET, syst_csr),
  BASIC_REGISTER_ENTRY(REG_M3_SYST_RVR_OFFSET, syst_rvr),
  BASIC_REGISTER_ENTRY(REG_M3_SYST_CVR_OFFSET, syst_cvr),
  BASIC_REGISTER_ENTRY(REG_M3_SYST_CALIB_OFFSET, syst_calib),
  BASIC_REGISTER_ENTRY(REG_M3_NVIC_STIR_OFFSET, nvic_stir),
};

static int m3_index(uint64_t offset, uint32_t base, unsigned count) {
  uint64_t end = (uint64_t)base + (uint64_t)count * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < base || offset >= end) return -1;

  return (int)((offset - base) / sizeof(uint32_t));
}

static uint64_t m3_monotonic_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void M3_StartCycleCount(void) {
  cyccnt_start_ns = m3_monotonic_ns();
}

uint32_t M3_GetVtor(void) {
  return vtor;
}

void M3_PendIrq(unsigned irq) {
  if (irq >= REG_M3_NVIC_ISPR_COUNT * 32u) return;

  atomic_fetch_or(&nvic_pend[irq / 32u], 1u << (irq % 32u));
}

void M3_UnpendIrq(unsigned irq) {
  if (irq >= REG_M3_NVIC_ISPR_COUNT * 32u) return;

  atomic_fetch_and(&nvic_pend[irq / 32u], ~(1u << (irq % 32u)));
}

int M3_TakePendingIrq(void) {
  unsigned i;

  for (i = 0; i < REG_M3_NVIC_ISPR_COUNT; i++) {
    uint32_t pending = atomic_load(&nvic_pend[i]);
    uint32_t ready = pending & nvic_en[i];

    if (ready == 0) continue;

    atomic_fetch_and(&nvic_pend[i], ~(ready & (~ready + 1u)));
    return (int)(i * 32u + (unsigned)__builtin_ctz(ready));
  }

  return -1;
}

static uint64_t M3_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);
  int index;

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

  index = m3_index(offset, REG_M3_NVIC_ISER_OFFSET, REG_M3_NVIC_ISER_COUNT);
  if (index < 0) index = m3_index(offset, REG_M3_NVIC_ICER_OFFSET, REG_M3_NVIC_ICER_COUNT);
  if (index >= 0) {
    LOG_VBS("M3: read NVIC enabled[%d] (size=%u) -> 0x%08x\n", index, size, nvic_en[index]);
    return nvic_en[index];
  }

  index = m3_index(offset, REG_M3_NVIC_ISPR_OFFSET, REG_M3_NVIC_ISPR_COUNT);
  if (index < 0) index = m3_index(offset, REG_M3_NVIC_ICPR_OFFSET, REG_M3_NVIC_ICPR_COUNT);
  if (index >= 0) {
    uint32_t value = atomic_load(&nvic_pend[index]);

    LOG_VBS("M3: read NVIC pending[%d] (size=%u) -> 0x%08x\n", index, size, value);
    return value;
  }

  index = m3_index(offset, REG_M3_NVIC_IPR_OFFSET, REG_M3_NVIC_IPR_COUNT);
  if (index >= 0) {
    LOG_VBS("M3: read NVIC priority[%d] (size=%u) -> 0x%08x\n", index, size, nvic_ipr[index]);
    return nvic_ipr[index];
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void M3_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(m3_registers, ARRAY_LEN(m3_registers), offset);
  int index;

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
    if (offset == REG_M3_VTOR_OFFSET) {
      unsigned i;

      for (i = 0; i < REG_M3_NVIC_ISER_COUNT; i++) {
        nvic_en[i] = 0;
        atomic_store(&nvic_pend[i], 0);
      }
    } else if (offset == REG_M3_NVIC_STIR_OFFSET && value != 0) {
      M3_PendIrq((unsigned)value & 0x1FFu);
    }

    *reg->value = (uint32_t)value;
    LOG_VBS("M3: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  index = m3_index(offset, REG_M3_NVIC_ISER_OFFSET, REG_M3_NVIC_ISER_COUNT);
  if (index >= 0) {
    nvic_en[index] |= (uint32_t)value;
    LOG_VBS("M3: wrote NVIC_ISER%d <- 0x%08x\n", index, nvic_en[index]);
    return;
  }

  index = m3_index(offset, REG_M3_NVIC_ICER_OFFSET, REG_M3_NVIC_ICER_COUNT);
  if (index >= 0) {
    nvic_en[index] &= ~(uint32_t)value;
    LOG_VBS("M3: wrote NVIC_ICER%d <- 0x%08x\n", index, nvic_en[index]);
    return;
  }

  index = m3_index(offset, REG_M3_NVIC_ISPR_OFFSET, REG_M3_NVIC_ISPR_COUNT);
  if (index >= 0) {
    uint32_t old = atomic_fetch_or(&nvic_pend[index], (uint32_t)value);

    LOG_VBS("M3: wrote NVIC_ISPR%d <- 0x%08x\n", index, old | (uint32_t)value);
    return;
  }

  index = m3_index(offset, REG_M3_NVIC_ICPR_OFFSET, REG_M3_NVIC_ICPR_COUNT);
  if (index >= 0) {
    uint32_t old = atomic_fetch_and(&nvic_pend[index], ~(uint32_t)value);

    LOG_VBS("M3: wrote NVIC_ICPR%d <- 0x%08x\n", index, old & ~(uint32_t)value);
    return;
  }

  index = m3_index(offset, REG_M3_NVIC_IPR_OFFSET, REG_M3_NVIC_IPR_COUNT);
  if (index >= 0) {
    nvic_ipr[index] = (uint32_t)value;
    LOG_VBS("M3: wrote NVIC_IPR%d <- 0x%08x\n", index, nvic_ipr[index]);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *M3_Initialize(void) {
  demcr = 0;
  dwt_ctrl = 0;
  itcmcr = 0;
  vtor = 0;
  ccr = 0;
  shcsr = 0;
  cfsr = 0;
  hfsr = 0;
  dfsr = 0;
  mmfar = 0;
  bfar = 0;
  syst_csr = 0;
  syst_rvr = 0;
  syst_cvr = 0;
  syst_calib = 0;
  nvic_stir = 0;
  memset(nvic_en, 0, sizeof(nvic_en));
  for (unsigned i = 0; i < REG_M3_NVIC_ISPR_COUNT; i++) {
    atomic_store(&nvic_pend[i], 0);
  }
  memset(nvic_ipr, 0, sizeof(nvic_ipr));
  cyccnt_start_ns = 0;
  return component_register("M3", NULL, M3_Read, M3_Write);
}
