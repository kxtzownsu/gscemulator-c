#include <H1B3C/peripherals/pmu.h>

#include <stdbool.h>
#include <stdint.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(sw_pdb)
DEFINE_BASIC_REGISTER(sw_pdb_secure)

#define PMU_PERIPH_CLOCK_COUNT 48

static const BasicRegister pmu_registers[] = {
  BASIC_REGISTER_ENTRY(REG_PMU_SW_PDB_OFFSET, sw_pdb),
  BASIC_REGISTER_ENTRY(REG_PMU_SW_PDB_SECURE_OFFSET, sw_pdb_secure),
};

static bool periph_clocks_en[PMU_PERIPH_CLOCK_COUNT];
static uint32_t pwrdn_scratch[REG_PMU_PWRDN_SCRATCH_COUNT];
static uint32_t pwrdn_scratch_lock[REG_PMU_PWRDN_SCRATCH_LOCK_COUNT];

static int pwrdn_scratch_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_PMU_PWRDN_SCRATCH_OFFSET +
                 (uint64_t)REG_PMU_PWRDN_SCRATCH_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_PMU_PWRDN_SCRATCH_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_PMU_PWRDN_SCRATCH_OFFSET) / sizeof(uint32_t));
}

static int pwrdn_scratch_lock_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_PMU_PWRDN_SCRATCH_LOCK_OFFSET +
                 (uint64_t)REG_PMU_PWRDN_SCRATCH_LOCK_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_PMU_PWRDN_SCRATCH_LOCK_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_PMU_PWRDN_SCRATCH_LOCK_OFFSET) / sizeof(uint32_t));
}

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
  const BasicRegister *reg = basic_register_find(pmu_registers, ARRAY_LEN(pmu_registers), offset);
  int scratch_index;
  uint32_t value;

  if (reg != NULL) {
    LOG_VBS("PMU: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  scratch_index = pwrdn_scratch_index(offset);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: read PWRDN_SCRATCH%d (size=%u) -> 0x%08x\n", scratch_index, size,
            pwrdn_scratch[scratch_index]);
    return pwrdn_scratch[scratch_index];
  }

  scratch_index = pwrdn_scratch_lock_index(offset);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: read PWRDN_SCRATCH_LOCK%d (size=%u) -> 0x%08x\n", scratch_index, size,
            pwrdn_scratch_lock[scratch_index]);
    return pwrdn_scratch_lock[scratch_index];
  }

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
  const BasicRegister *reg = basic_register_find(pmu_registers, ARRAY_LEN(pmu_registers), offset);
  int scratch_index;

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("PMU: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  scratch_index = pwrdn_scratch_index(offset);
  if (scratch_index >= 0) {
    if ((scratch_index < 8 && pwrdn_scratch_lock[0] != 0) ||
        (scratch_index >= 8 && scratch_index < 16 && pwrdn_scratch_lock[1] != 0)) {
      LOG_VBS("PMU: ignored write to locked PWRDN_SCRATCH%d\n", scratch_index);
      return;
    }

    pwrdn_scratch[scratch_index] = (uint32_t)value;
    LOG_VBS("PMU: wrote PWRDN_SCRATCH%d (size=%u) <- 0x%08x\n", scratch_index, size,
            pwrdn_scratch[scratch_index]);
    return;
  }

  scratch_index = pwrdn_scratch_lock_index(offset);
  if (scratch_index >= 0) {
    if (value != 0) pwrdn_scratch_lock[scratch_index] = 1;
    LOG_VBS("PMU: wrote PWRDN_SCRATCH_LOCK%d (size=%u) <- 0x%08x\n", scratch_index, size,
            pwrdn_scratch_lock[scratch_index]);
    return;
  }

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
  }
}

Component *PMU_Initialize(void) {
  unsigned i;

  for (i = 0; i < PMU_PERIPH_CLOCK_COUNT; i++) periph_clocks_en[i] = false;
  for (i = 0; i < REG_PMU_PWRDN_SCRATCH_COUNT; i++) pwrdn_scratch[i] = 0;
  for (i = 0; i < REG_PMU_PWRDN_SCRATCH_LOCK_COUNT; i++) pwrdn_scratch_lock[i] = 0;

  sw_pdb = 0;
  sw_pdb_secure = 0;

  return component_register("PMU", NULL, PMU_Read, PMU_Write);
}
