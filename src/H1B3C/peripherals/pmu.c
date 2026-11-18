#include <H1B3C/peripherals/pmu.h>

#include <stdbool.h>
#include <stdint.h>

#include <common/logger.h>

#define PMU_PERIPH_CLOCK_COUNT 48

static bool periph_clocks_en[PMU_PERIPH_CLOCK_COUNT];

static uint32_t pmu_periclk_value(unsigned base, unsigned count) {
  uint32_t value = 0;
  unsigned i;

  for (i = 0; i < count; i++) {
    if (periph_clocks_en[base + i]) value |= 1u << i;
  }

  return value;
}

static void pmu_periclk_apply(unsigned base, unsigned count, uint32_t value, bool enable) {
  unsigned i;

  for (i = 0; i < count; i++) {
    if (value & (1u << i)) periph_clocks_en[base + i] = enable;
  }
}

static uint64_t PMU_Read(Component *self, uint64_t offset, unsigned size) {
  uint32_t value;

  switch (offset) {
    case REG_PMU_PERICLKSET0_OFFSET:
      value = pmu_periclk_value(0, 32);
      LOG_VBS("PMU: read PERICLKSET0 (size=%u) -> 0x%08x\n", size, value);
      return value;
    case REG_PMU_PERICLKCLR0_OFFSET:
      value = pmu_periclk_value(0, 32);
      LOG_VBS("PMU: read PERICLKCLR0 (size=%u) -> 0x%08x\n", size, value);
      return value;
    case REG_PMU_PERICLKSET1_OFFSET:
      value = pmu_periclk_value(32, 16);
      LOG_VBS("PMU: read PERICLKSET1 (size=%u) -> 0x%08x\n", size, value);
      return value;
    case REG_PMU_PERICLKCLR1_OFFSET:
      value = pmu_periclk_value(32, 16);
      LOG_VBS("PMU: read PERICLKCLR1 (size=%u) -> 0x%08x\n", size, value);
      return value;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void PMU_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  switch (offset) {
    case REG_PMU_PERICLKSET0_OFFSET:
      pmu_periclk_apply(0, 32, (uint32_t)value, true);
      LOG_VBS("PMU: wrote PERICLKSET0 (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_PMU_PERICLKCLR0_OFFSET:
      pmu_periclk_apply(0, 32, (uint32_t)value, false);
      LOG_VBS("PMU: wrote PERICLKCLR0 (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_PMU_PERICLKSET1_OFFSET:
      pmu_periclk_apply(32, 16, (uint32_t)value, true);
      LOG_VBS("PMU: wrote PERICLKSET1 (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_PMU_PERICLKCLR1_OFFSET:
      pmu_periclk_apply(32, 16, (uint32_t)value, false);
      LOG_VBS("PMU: wrote PERICLKCLR1 (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *PMU_Initialize(void) {
  unsigned i;

  for (i = 0; i < PMU_PERIPH_CLOCK_COUNT; i++) periph_clocks_en[i] = false;

  return component_register("PMU", NULL, PMU_Read, PMU_Write);
}
