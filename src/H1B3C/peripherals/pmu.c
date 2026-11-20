#include <H1B3C/peripherals/pmu.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/logger.h>
#include <common/peripherals/define.h>
#include <common/runtime/control.h>

DEFINE_BASIC_REGISTER(sw_pdb)
DEFINE_BASIC_REGISTER(sw_pdb_secure)
DEFINE_BASIC_REGISTER(low_power_dis)
DEFINE_BASIC_REGISTER(exitpd_mask)

#define PMU_PERIPH_CLOCK_COUNT 48
#define PMU_CHIP_ID_VALUE 0x4485694Du
#define PMU_GLOBAL_RESET_KEY 0x7041776u

static const BasicRegister pmu_registers[] = {
  BASIC_REGISTER_ENTRY(REG_PMU_SW_PDB_OFFSET, sw_pdb),
  BASIC_REGISTER_ENTRY(REG_PMU_SW_PDB_SECURE_OFFSET, sw_pdb_secure),
  BASIC_REGISTER_ENTRY(REG_PMU_LOW_POWER_DIS_OFFSET, low_power_dis),
  BASIC_REGISTER_ENTRY(REG_PMU_EXITPD_MASK_OFFSET, exitpd_mask),
};

static bool periph_clocks_en[PMU_PERIPH_CLOCK_COUNT];
static uint32_t pwrdn_scratch[REG_PMU_PWRDN_SCRATCH_COUNT];
static uint32_t pwrdn_scratch_lock[REG_PMU_PWRDN_SCRATCH_LOCK_COUNT];
static bool rst_wr_en[REG_PMU_RST_WR_EN_COUNT];
static uint32_t long_life_scratch[REG_PMU_LONG_LIFE_SCRATCH_COUNT];
static bool long_life_scratch_wr_en[REG_PMU_LONG_LIFE_SCRATCH_COUNT];
static bool int_enable;
static uint32_t rstsrc;
static uint32_t pmu_flat[REG_PMU_LOW_BLOCK_COUNT];
static uint32_t pmu_int_state;
static uint32_t pmu_int_test;
static uint32_t antest_top_ctrl;
static uint32_t antest_regdig;

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

static int rst_index(uint64_t offset, uint32_t base, unsigned count) {
  unsigned i;

  for (i = 0; i < count; i++) {
    if (offset == base + (uint64_t)i * 8u) return (int)i;
  }

  return -1;
}

static int long_life_scratch_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_PMU_LONG_LIFE_SCRATCH_OFFSET +
                 (uint64_t)REG_PMU_LONG_LIFE_SCRATCH_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_PMU_LONG_LIFE_SCRATCH_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_PMU_LONG_LIFE_SCRATCH_OFFSET) / sizeof(uint32_t));
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

  scratch_index = rst_index(offset, REG_PMU_RST_WR_EN_OFFSET, REG_PMU_RST_WR_EN_COUNT);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: read RST_WR_EN%d (size=%u) -> %u\n", scratch_index, size,
            rst_wr_en[scratch_index]);
    return rst_wr_en[scratch_index];
  }

  scratch_index = rst_index(offset, REG_PMU_RST_OFFSET, REG_PMU_RST_COUNT);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: read RST%d (size=%u) -> 0\n", scratch_index, size);
    return 0;
  }

  scratch_index = long_life_scratch_index(offset);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: read LONG_LIFE_SCRATCH%d (size=%u) -> 0x%08x\n", scratch_index, size,
            long_life_scratch[scratch_index]);
    return long_life_scratch[scratch_index];
  }

  if (offset == REG_PMU_LONG_LIFE_SCRATCH_WR_EN_OFFSET) {
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < REG_PMU_LONG_LIFE_SCRATCH_COUNT; i++) {
      if (long_life_scratch_wr_en[i]) value |= 1u << i;
    }

    LOG_VBS("PMU: read LONG_LIFE_SCRATCH_WR_EN (size=%u) -> 0x%08x\n", size, value);
    return value;
  }

  switch (offset) {
    case REG_PMU_RSTSRC_OFFSET:
      LOG_VBS("PMU: read RSTSRC (size=%u) -> 0x%08x\n", size, rstsrc);
      return rstsrc;
    case REG_PMU_CLRRST_OFFSET:
      LOG_VBS("PMU: read CLRRST (size=%u) -> 0\n", size);
      return 0;
    case REG_PMU_GLOBAL_RESET_OFFSET:
      LOG_VBS("PMU: read GLOBAL_RESET (size=%u) -> 0\n", size);
      return 0;
    case REG_PMU_INT_ENABLE_OFFSET:
      LOG_VBS("PMU: read INT_ENABLE (size=%u) -> %u\n", size, int_enable);
      return int_enable;
    case REG_PMU_CHIP_ID_OFFSET:
      LOG_VBS("PMU: read CHIP_ID (size=%u) -> 0x%08x\n", size, PMU_CHIP_ID_VALUE);
      return PMU_CHIP_ID_VALUE;
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
      break;
  }

  if (offset < (uint64_t)REG_PMU_LOW_BLOCK_COUNT * 4u) return pmu_flat[offset / 4u];
  if (offset == REG_PMU_INT_STATE_OFFSET) return pmu_int_state;
  if (offset == REG_PMU_INT_TEST_OFFSET) return pmu_int_test;
  if (offset == REG_PMU_ANTEST_TOP_CTRL_OFFSET) return antest_top_ctrl;
  if (offset == REG_PMU_ANTEST_REGDIG_OFFSET) return antest_regdig;

  component_log_unhandled(self, "read", offset);
  return 0;
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

  scratch_index = rst_index(offset, REG_PMU_RST_WR_EN_OFFSET, REG_PMU_RST_WR_EN_COUNT);
  if (scratch_index >= 0) {
    rst_wr_en[scratch_index] = value != 0;
    LOG_VBS("PMU: wrote RST_WR_EN%d (size=%u) <- %u\n", scratch_index, size,
            rst_wr_en[scratch_index]);
    return;
  }

  scratch_index = rst_index(offset, REG_PMU_RST_OFFSET, REG_PMU_RST_COUNT);
  if (scratch_index >= 0) {
    LOG_VBS("PMU: reset %d requested (0x%08x), %s\n", scratch_index, (uint32_t)value,
            rst_wr_en[scratch_index] ? "allowed" : "blocked");
    return;
  }

  scratch_index = long_life_scratch_index(offset);
  if (scratch_index >= 0) {
    if (!long_life_scratch_wr_en[scratch_index]) {
      LOG_VBS("PMU: ignored write to locked LONG_LIFE_SCRATCH%d\n", scratch_index);
      return;
    }

    long_life_scratch[scratch_index] = (uint32_t)value;
    LOG_VBS("PMU: wrote LONG_LIFE_SCRATCH%d (size=%u) <- 0x%08x\n", scratch_index, size,
            long_life_scratch[scratch_index]);
    return;
  }

  if (offset == REG_PMU_LONG_LIFE_SCRATCH_WR_EN_OFFSET) {
    unsigned i;

    for (i = 0; i < REG_PMU_LONG_LIFE_SCRATCH_COUNT; i++) {
      long_life_scratch_wr_en[i] = ((uint32_t)value & (1u << i)) != 0;
    }

    LOG_VBS("PMU: wrote LONG_LIFE_SCRATCH_WR_EN (size=%u) <- 0x%08x\n", size,
            (uint32_t)value);
    return;
  }

  switch (offset) {
    case REG_PMU_RSTSRC_OFFSET:
      LOG_VBS("PMU: ignored write to RSTSRC\n");
      return;
    case REG_PMU_CLRRST_OFFSET:
      if (value != 0) rstsrc = 0;
      LOG_VBS("PMU: wrote CLRRST (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_PMU_GLOBAL_RESET_OFFSET:
      if ((uint32_t)value == PMU_GLOBAL_RESET_KEY) {
        LOG("PMU: global reset requested\n");
        runtime_request_reboot();
      } else {
        LOG_VBS("PMU: ignored GLOBAL_RESET write 0x%08x\n", (uint32_t)value);
      }
      return;
    case REG_PMU_INT_ENABLE_OFFSET:
      int_enable = value != 0;
      LOG_VBS("PMU: wrote INT_ENABLE (size=%u) <- %u\n", size, int_enable);
      return;
    case REG_PMU_CHIP_ID_OFFSET:
      LOG_VBS("PMU: ignored write to CHIP_ID\n");
      return;
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
      break;
  }

  if (offset < (uint64_t)REG_PMU_LOW_BLOCK_COUNT * 4u) {
    pmu_flat[offset / 4u] = (uint32_t)value;
    return;
  }
  if (offset == REG_PMU_INT_STATE_OFFSET) {
    pmu_int_state &= ~(uint32_t)value;
    return;
  }
  if (offset == REG_PMU_INT_TEST_OFFSET) {
    pmu_int_test = (uint32_t)value;
    return;
  }
  if (offset == REG_PMU_ANTEST_TOP_CTRL_OFFSET) {
    antest_top_ctrl = (uint32_t)value;
    return;
  }
  if (offset == REG_PMU_ANTEST_REGDIG_OFFSET) {
    antest_regdig = (uint32_t)value;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *PMU_Initialize(void) {
  unsigned i;

  for (i = 0; i < PMU_PERIPH_CLOCK_COUNT; i++) periph_clocks_en[i] = false;
  for (i = 0; i < REG_PMU_PWRDN_SCRATCH_COUNT; i++) pwrdn_scratch[i] = 0;
  for (i = 0; i < REG_PMU_PWRDN_SCRATCH_LOCK_COUNT; i++) pwrdn_scratch_lock[i] = 0;
  for (i = 0; i < REG_PMU_RST_WR_EN_COUNT; i++) rst_wr_en[i] = true;
  for (i = 0; i < REG_PMU_LONG_LIFE_SCRATCH_COUNT; i++) long_life_scratch[i] = 0;
  for (i = 0; i < REG_PMU_LONG_LIFE_SCRATCH_COUNT; i++) long_life_scratch_wr_en[i] = true;

  sw_pdb = 0;
  sw_pdb_secure = 0;
  low_power_dis = 0;
  exitpd_mask = 0;
  int_enable = false;
  rstsrc = 0;
  memset(pmu_flat, 0, sizeof(pmu_flat));
  pmu_int_state = 0;
  pmu_int_test = 0;
  antest_top_ctrl = 0;
  antest_regdig = 0;

  return component_register("PMU", NULL, PMU_Read, PMU_Write);
}
