#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/m3.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define M3_CPUID_VALUE 0x410FC331
/* PCLK_FREQ is 24 MHz on H1B3C (chip/haven/config_chip.h). */
#define M3_CYCCNT_HZ 24000000ULL
#define M3_SYST_TENMS 240000u
#define M3_PRIO_NONE INT_MAX
#define M3_SYST_CSR_COUNTFLAG (1u << 16)

#define M3_SHCSR_MEMFAULTENA (1u << 16)
#define M3_SHCSR_BUSFAULTENA (1u << 17)
#define M3_SHCSR_USGFAULTENA (1u << 18)
#define M3_HFSR_FORCED (1u << 30)

DEFINE_BASIC_REGISTER(demcr)
DEFINE_BASIC_REGISTER(dwt_ctrl)
DEFINE_BASIC_REGISTER(vtor)
DEFINE_BASIC_REGISTER(ccr)
DEFINE_BASIC_REGISTER(shpr1)
DEFINE_BASIC_REGISTER(shpr2)
DEFINE_BASIC_REGISTER(shpr3)
DEFINE_BASIC_REGISTER(shcsr)
DEFINE_BASIC_REGISTER(cfsr)
DEFINE_BASIC_REGISTER(hfsr)
DEFINE_BASIC_REGISTER(dfsr)
DEFINE_BASIC_REGISTER(mmfar)
DEFINE_BASIC_REGISTER(bfar)
DEFINE_BASIC_REGISTER(nvic_stir)

static uint32_t itcmcr;
static uint32_t m3_debug_regs[REG_M3_DEBUG_BLOCK_LIMIT / 4];
static uint32_t nvic_en[REG_M3_NVIC_ISER_COUNT];
static _Atomic uint32_t nvic_pend[REG_M3_NVIC_ISPR_COUNT];
static uint32_t nvic_ipr[REG_M3_NVIC_IPR_COUNT];
static _Atomic uint32_t sys_pend[16];
static uint64_t cyccnt_start_ns;

static uint32_t syst_csr;
static uint32_t syst_rvr;
static uint32_t syst_cvr;
static uint64_t syst_last_ns;
static uint32_t syst_pending_wraps;
static bool syst_counting;

static const BasicRegister m3_registers[] = {
  BASIC_REGISTER_ENTRY(REG_M3_DEMCR_OFFSET, demcr),
  BASIC_REGISTER_ENTRY(REG_M3_DWT_CTRL_OFFSET, dwt_ctrl),
  BASIC_REGISTER_ENTRY(REG_M3_VTOR_OFFSET, vtor),
  BASIC_REGISTER_ENTRY(REG_M3_CCR_OFFSET, ccr),
  BASIC_REGISTER_ENTRY(REG_M3_SHPR1_OFFSET, shpr1),
  BASIC_REGISTER_ENTRY(REG_M3_SHPR2_OFFSET, shpr2),
  BASIC_REGISTER_ENTRY(REG_M3_SHPR3_OFFSET, shpr3),
  BASIC_REGISTER_ENTRY(REG_M3_SHCSR_OFFSET, shcsr),
  BASIC_REGISTER_ENTRY(REG_M3_CFSR_OFFSET, cfsr),
  BASIC_REGISTER_ENTRY(REG_M3_HFSR_OFFSET, hfsr),
  BASIC_REGISTER_ENTRY(REG_M3_DFSR_OFFSET, dfsr),
  BASIC_REGISTER_ENTRY(REG_M3_MMFAR_OFFSET, mmfar),
  BASIC_REGISTER_ENTRY(REG_M3_BFAR_OFFSET, bfar),
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

static unsigned m3_priority_byte(uint32_t word, unsigned index) {
  return ((word >> (8u * (index & 3u))) & 0xFFu) >> 5;
}

static uint32_t syst_reload(void) {
  return syst_rvr & 0xFFFFFFu;
}

static void syst_restart(void) {
  syst_last_ns = m3_monotonic_ns();
  syst_pending_wraps = 0;
  syst_counting = (syst_csr & 1u) != 0;
}

static void syst_update(void) {
  uint64_t now;
  uint64_t elapsed;
  uint64_t ticks;
  uint64_t value;
  uint64_t over;
  uint64_t period;
  uint64_t wraps;

  if (!syst_counting) return;

  now = m3_monotonic_ns();
  elapsed = now >= syst_last_ns ? now - syst_last_ns : 0;
  syst_last_ns = now;
  ticks = (elapsed * M3_CYCCNT_HZ) / 1000000000ULL;
  if (ticks == 0) return;

  value = syst_cvr;
  if (ticks <= value) {
    syst_cvr = (uint32_t)(value - ticks);
    return;
  }

  over = ticks - value;
  period = (uint64_t)syst_reload() + 1u;
  wraps = 1u + (over - 1u) / period;
  syst_cvr = (uint32_t)((period - 1u) - ((over - 1u) % period));
  syst_pending_wraps += (uint32_t)wraps;
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

void M3_PendSysException(unsigned exc_num) {
  if (exc_num == 0 || exc_num > 15) return;

  atomic_store(&sys_pend[exc_num], 1u);
}

void M3_Poll(void) {
  if (!syst_counting) return;

  syst_update();
  if (syst_pending_wraps == 0) return;

  syst_csr |= M3_SYST_CSR_COUNTFLAG;
  if ((syst_csr & 2u) != 0) M3_PendSysException(15);
  syst_pending_wraps = 0;
}

int M3_ExceptionPriority(unsigned exc_num) {
  if (exc_num == 1) return -3;
  if (exc_num == 2) return -2;
  if (exc_num == 3) return -1;

  if (exc_num >= 4 && exc_num <= 7) return (int)m3_priority_byte(shpr1, exc_num - 4);
  if (exc_num == 11 || exc_num == 12) return (int)m3_priority_byte(shpr2, exc_num - 11);
  if (exc_num >= 13 && exc_num <= 15) return (int)m3_priority_byte(shpr3, exc_num - 13);

  if (exc_num >= 16 && exc_num < 16u + REG_M3_NVIC_IPR_COUNT * 4u) {
    unsigned index = exc_num - 16u;

    return (int)m3_priority_byte(nvic_ipr[index / 4u], index % 4u);
  }

  return M3_PRIO_NONE;
}

static bool m3_exception_blocked(unsigned exc_num, int primask, int faultmask) {
  if (exc_num == 2) return false;
  if (exc_num == 3) return false;
  if (faultmask) return true;
  if (primask && exc_num >= 4) return true;

  return false;
}

int M3_TakeException(unsigned ipsr, int primask, int faultmask) {
  unsigned exc;
  unsigned best = 0;
  int best_prio = M3_PRIO_NONE;
  int current_prio = M3_PRIO_NONE;

  M3_Poll();

  if (ipsr != 0) {
    current_prio = M3_ExceptionPriority(ipsr);
  }

  for (exc = 2; exc <= 15; exc++) {
    int prio;

    if (atomic_load(&sys_pend[exc]) == 0) continue;
    if (m3_exception_blocked(exc, primask, faultmask)) continue;

    prio = M3_ExceptionPriority(exc);
    if (prio >= current_prio) continue;
    if (prio < best_prio || (prio == best_prio && exc < best)) {
      best = exc;
      best_prio = prio;
    }
  }

  for (exc = 0; exc < REG_M3_NVIC_ISPR_COUNT * 32u; exc++) {
    unsigned index = exc / 32u;
    unsigned bit = exc % 32u;
    int prio;

    if ((atomic_load(&nvic_pend[index]) & (1u << bit)) == 0) continue;
    if ((nvic_en[index] & (1u << bit)) == 0) continue;
    if (m3_exception_blocked(16u + exc, primask, faultmask)) continue;

    prio = M3_ExceptionPriority(16u + exc);
    if (prio >= current_prio) continue;
    if (prio < best_prio || (prio == best_prio && 16u + exc < best)) {
      best = 16u + exc;
      best_prio = prio;
    }
  }

  if (best == 0) return 0;

  if (best < 16) {
    atomic_store(&sys_pend[best], 0);
  } else {
    unsigned irq = best - 16u;

    atomic_fetch_and(&nvic_pend[irq / 32u], ~(1u << (irq % 32u)));
  }

  return (int)best;
}

unsigned M3_EscalateFault(unsigned kind, uint32_t status) {
  unsigned exc;

  switch (kind) {
    case M3_FAULT_USAGE:
      cfsr |= status & 0xFFFF0000u;
      exc = (shcsr & M3_SHCSR_USGFAULTENA) != 0 ? 6u : 3u;
      break;
    case M3_FAULT_BUS:
      cfsr |= status & 0x0000FF00u;
      exc = (shcsr & M3_SHCSR_BUSFAULTENA) != 0 ? 5u : 3u;
      break;
    case M3_FAULT_MEM:
      cfsr |= status & 0x000000FFu;
      exc = (shcsr & M3_SHCSR_MEMFAULTENA) != 0 ? 4u : 3u;
      break;
    default:
      exc = 3u;
      break;
  }

  if (exc == 3) hfsr |= M3_HFSR_FORCED;

  LOG_VBS("M3: escalated fault kind=%u status=0x%08x -> exception %u (cfsr=0x%08x hfsr=0x%08x)\n",
          kind, status, exc, cfsr, hfsr);
  return exc;
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
    case REG_M3_SYST_CSR_OFFSET: {
      uint32_t value;

      M3_Poll();
      value = syst_csr;
      syst_csr &= ~M3_SYST_CSR_COUNTFLAG;
      LOG_VBS("M3: read SYST_CSR (size=%u) -> 0x%08x\n", size, value);
      return value;
    }
    case REG_M3_SYST_RVR_OFFSET:
      LOG_VBS("M3: read SYST_RVR (size=%u) -> 0x%08x\n", size, syst_rvr);
      return syst_rvr;
    case REG_M3_SYST_CVR_OFFSET:
      syst_update();
      LOG_VBS("M3: read SYST_CVR (size=%u) -> 0x%08x\n", size, syst_cvr);
      return syst_cvr;
    case REG_M3_SYST_CALIB_OFFSET:
      LOG_VBS("M3: read SYST_CALIB (size=%u) -> 0x%08x\n", size, M3_SYST_TENMS);
      return M3_SYST_TENMS;
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

  if (offset < REG_M3_DEBUG_BLOCK_LIMIT) return m3_debug_regs[offset / 4u];

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
  memset(m3_debug_regs, 0, sizeof(m3_debug_regs));
      LOG_VBS("M3: wrote ITCMCR (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_M3_CFSR_OFFSET:
      cfsr &= ~(uint32_t)value;
      LOG_VBS("M3: cleared CFSR bits 0x%08x -> 0x%08x\n", (uint32_t)value, cfsr);
      return;
    case REG_M3_HFSR_OFFSET:
      hfsr &= ~(uint32_t)value;
      LOG_VBS("M3: cleared HFSR bits 0x%08x -> 0x%08x\n", (uint32_t)value, hfsr);
      return;
    case REG_M3_DFSR_OFFSET:
      dfsr &= ~(uint32_t)value;
      LOG_VBS("M3: cleared DFSR bits 0x%08x -> 0x%08x\n", (uint32_t)value, dfsr);
      return;
    case REG_M3_SYST_CSR_OFFSET:
      syst_csr = (uint32_t)value & 7u;
      if ((syst_csr & 1u) != 0 && !syst_counting) {
        syst_cvr = syst_reload();
      }
      syst_restart();
      LOG_VBS("M3: wrote SYST_CSR (size=%u) <- 0x%08x\n", size, syst_csr);
      return;
    case REG_M3_SYST_RVR_OFFSET:
      syst_rvr = (uint32_t)value & 0xFFFFFFu;
      syst_cvr = syst_reload();
      syst_restart();
      LOG_VBS("M3: wrote SYST_RVR (size=%u) <- 0x%08x\n", size, syst_rvr);
      return;
    case REG_M3_SYST_CVR_OFFSET:
      syst_cvr = (uint32_t)value & 0xFFFFFFu;
      syst_csr &= ~M3_SYST_CSR_COUNTFLAG;
      syst_restart();
      LOG_VBS("M3: wrote SYST_CVR (size=%u) <- 0x%08x\n", size, syst_cvr);
      return;
    case REG_M3_SYST_CALIB_OFFSET:
      LOG_VBS("M3: ignored write to SYST_CALIB\n");
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
      for (i = 0; i < 16; i++) {
        atomic_store(&sys_pend[i], 0);
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

  if (offset < REG_M3_DEBUG_BLOCK_LIMIT) {
    m3_debug_regs[offset / 4u] = (uint32_t)value;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *M3_Initialize(void) {
  unsigned i;

  demcr = 0;
  dwt_ctrl = 0;
  itcmcr = 0;
  vtor = 0;
  ccr = 0;
  shpr1 = 0;
  shpr2 = 0;
  shpr3 = 0;
  shcsr = 0;
  cfsr = 0;
  hfsr = 0;
  dfsr = 0;
  mmfar = 0;
  bfar = 0;
  nvic_stir = 0;
  syst_csr = 0;
  syst_rvr = 0;
  syst_cvr = 0;
  syst_last_ns = 0;
  syst_pending_wraps = 0;
  syst_counting = false;
  memset(nvic_en, 0, sizeof(nvic_en));
  for (i = 0; i < REG_M3_NVIC_ISPR_COUNT; i++) {
    atomic_store(&nvic_pend[i], 0);
  }
  for (i = 0; i < 16; i++) {
    atomic_store(&sys_pend[i], 0);
  }
  memset(nvic_ipr, 0, sizeof(nvic_ipr));
  cyccnt_start_ns = 0;

  return component_register("M3", NULL, M3_Read, M3_Write);
}
