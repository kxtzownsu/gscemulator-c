#include <H1B3C/peripherals/globalsec.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define DUMMYKEY_COUNT 3

DEFINE_BASIC_REGISTER(alert_control)
DEFINE_BASIC_REGISTER(dbg_control)
DEFINE_BASIC_REGISTER(alert_intr_sts0)
DEFINE_BASIC_REGISTER(alert_intr_sts1)

static const BasicRegister globalsec_registers[] = {
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_ALERT_INTR_STS0_OFFSET, alert_intr_sts0),
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_ALERT_INTR_STS1_OFFSET, alert_intr_sts1),
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_ALERT_CONTROL_OFFSET, alert_control),
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_DBG_CONTROL_OFFSET, dbg_control),
};

static uint32_t region_regs[REG_GLOBALSEC_REGION_COUNT];
static uint32_t dummykey[DUMMYKEY_COUNT];
static const uint32_t dummykey_offsets[DUMMYKEY_COUNT] = {
  REG_GLOBALSEC_DUMMYKEY0_OFFSET,
  REG_GLOBALSEC_DUMMYKEY1_OFFSET,
  REG_GLOBALSEC_DUMMYKEY2_OFFSET,
};

static uint32_t sb_bl_sig[REG_GLOBALSEC_SB_BL_SIG_COUNT];
static bool sb_comp_status;
static uint32_t hide_rom;

static int region_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_GLOBALSEC_REGION_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)(offset / sizeof(uint32_t));
}

static int dummykey_get_index(uint64_t offset) {
  size_t i;

  for (i = 0; i < DUMMYKEY_COUNT; i++) {
    if (dummykey_offsets[i] == offset) return (int)i;
  }

  return -1;
}

static int sb_bl_sig_index(uint64_t offset) {
  uint64_t start = REG_GLOBALSEC_SB_BL_SIG0_OFFSET;
  uint64_t end = start + (uint64_t)REG_GLOBALSEC_SB_BL_SIG_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < start || offset >= end) return -1;

  return (int)((offset - start) / sizeof(uint32_t));
}

static uint64_t GLOBALSEC_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg =
      basic_register_find(globalsec_registers, ARRAY_LEN(globalsec_registers), offset);
  int index;

  if (reg != NULL) {
    LOG_VBS("GLOBALSEC: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  index = region_index(offset);
  if (index >= 0) {
    LOG_VBS("GLOBALSEC: read REGION[0x%03llx] (size=%u) -> 0x%08x\n",
            (unsigned long long)offset, size, region_regs[index]);
    return region_regs[index];
  }

  index = dummykey_get_index(offset);
  if (index >= 0) {
    LOG_VBS("GLOBALSEC: read DUMMYKEY%d (size=%u) -> 0x%08x\n", index, size, dummykey[index]);
    return dummykey[index];
  }

  index = sb_bl_sig_index(offset);
  if (index >= 0) {
    LOG_VBS("GLOBALSEC: read SB_BL_SIG%d (size=%u) -> 0xfacecafe\n", index, size);
    return 0xFACECAFE;
  }

  switch (offset) {
    case REG_GLOBALSEC_SB_COMP_STATUS_OFFSET:
      LOG_VBS("GLOBALSEC: read SB_COMP_STATUS (size=%u) -> %u\n", size, sb_comp_status);
      return sb_comp_status;
    case REG_GLOBALSEC_SIG_UNLOCK_OFFSET:
      LOG_VBS("GLOBALSEC: read SIG_UNLOCK (size=%u) -> 0\n", size);
      return 0;
    case REG_GLOBALSEC_HIDE_ROM_OFFSET:
      LOG_VBS("GLOBALSEC: read HIDE_ROM (size=%u) -> 0x%08x\n", size, hide_rom);
      return hide_rom;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void GLOBALSEC_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg =
      basic_register_find(globalsec_registers, ARRAY_LEN(globalsec_registers), offset);
  int index;

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("GLOBALSEC: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  index = region_index(offset);
  if (index >= 0) {
    region_regs[index] = (uint32_t)value;
    LOG_VBS("GLOBALSEC: wrote REGION[0x%03llx] (size=%u) <- 0x%08x\n",
            (unsigned long long)offset, size, (uint32_t)value);
    return;
  }

  index = dummykey_get_index(offset);
  if (index >= 0) {
    dummykey[index] = (uint32_t)value;
    LOG_VBS("GLOBALSEC: wrote DUMMYKEY%d (size=%u) <- 0x%08x\n", index, size, dummykey[index]);
    return;
  }

  index = sb_bl_sig_index(offset);
  if (index >= 0) {
    sb_bl_sig[index] = (uint32_t)value;
    return;
  }

  switch (offset) {
    case REG_GLOBALSEC_SB_COMP_STATUS_OFFSET:
      LOG_VBS("GLOBALSEC: ignored write to SB_COMP_STATUS\n");
      return;
    case REG_GLOBALSEC_SIG_UNLOCK_OFFSET:
      sb_comp_status = true;
      LOG_VBS("GLOBALSEC: SIG_UNLOCK -> SB_COMP_STATUS %u\n", sb_comp_status);
      return;
    case REG_GLOBALSEC_HIDE_ROM_OFFSET:
      if (hide_rom != 0) {
        LOG_VBS("GLOBALSEC: ignored write to locked HIDE_ROM\n");
        return;
      }
      hide_rom = (uint32_t)value;
      LOG_VBS("GLOBALSEC: wrote HIDE_ROM (size=%u) <- 0x%08x\n", size, hide_rom);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *GLOBALSEC_Initialize(void) {
  alert_control = 0;
  alert_intr_sts0 = 0;
  alert_intr_sts1 = 0;
  dbg_control = 0;
  sb_comp_status = false;
  hide_rom = 0;

  memset(region_regs, 0, sizeof(region_regs));
  memset(dummykey, 0, sizeof(dummykey));
  memset(sb_bl_sig, 0, sizeof(sb_bl_sig));

  return component_register("GLOBALSEC", NULL, GLOBALSEC_Read, GLOBALSEC_Write);
}
