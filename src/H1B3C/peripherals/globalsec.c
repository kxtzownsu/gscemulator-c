#include <H1B3C/peripherals/globalsec.h>
#include <H1B3C/peripherals/m3.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>
#include <common/rc.h>
#include <common/runtime/control.h>

#define DUMMYKEY_COUNT 3
#define GLOBALSEC_ALERT_GROUP_COUNT 3
#define GLOBALSEC_ALERT_GROUP_IRQ0 36
#define GLOBALSEC_ALERT_IRQ0 18
#define GLOBALSEC_ALERT_IRQ_GAP_INDEX 18
#define GLOBALSEC_ALERT_IRQ_GAP 3
#define GLOBALSEC_ALERT_FW_TRIGGER_MAGIC 0xA9u
#define GLOBALSEC_ALERT_FW_FIRST_INDEX 19
#define GLOBALSEC_ALERT_FW_COUNT 4
#define GLOBALSEC_ALERT_DLYCTR_HZ 24000000ULL

#define GLOBALSEC_PERMISSION_LOW 0x00
#define GLOBALSEC_PERMISSION_MEDIUM 0x33
#define GLOBALSEC_PERMISSION_HIGH 0x3C
#define GLOBALSEC_PERMISSION_HIGHEST 0x55

DEFINE_BASIC_REGISTER(dbg_control)
DEFINE_BASIC_REGISTER(obfs_sw_en)

static const BasicRegister globalsec_registers[] = {
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_OBFS_SW_EN_OFFSET, obfs_sw_en),
  BASIC_REGISTER_ENTRY(REG_GLOBALSEC_DBG_CONTROL_OFFSET, dbg_control),
};

static uint32_t alert_intr_sts[2];
static uint32_t alert_nmi_en[2];
static uint32_t alert_group_en[GLOBALSEC_ALERT_GROUP_COUNT][2];
static uint32_t alert_dlyctr_en[GLOBALSEC_ALERT_GROUP_COUNT][2];
static uint32_t alert_group_ctr[GLOBALSEC_ALERT_GROUP_COUNT];
static uint32_t alert_group_threshold[GLOBALSEC_ALERT_GROUP_COUNT];
static uint32_t alert_control;
static uint32_t alert_cfg_lock;
static uint32_t alert_fw_trigger;
static uint32_t alert_dlyctr_ctr[GLOBALSEC_ALERT_GROUP_COUNT];
static uint32_t alert_dlyctr_len[GLOBALSEC_ALERT_GROUP_COUNT];
static uint32_t alert_dlyctr_shutdown[GLOBALSEC_ALERT_GROUP_COUNT];
static uint64_t alert_dlyctr_ns[GLOBALSEC_ALERT_GROUP_COUNT];
static bool alert_dlyctr_active[GLOBALSEC_ALERT_GROUP_COUNT];

static uint32_t region_regs[REG_GLOBALSEC_REGION_COUNT];
static uint32_t dummykey[DUMMYKEY_COUNT];
static const uint32_t dummykey_offsets[DUMMYKEY_COUNT] = {
  REG_GLOBALSEC_DUMMYKEY0_OFFSET,
  REG_GLOBALSEC_DUMMYKEY1_OFFSET,
  REG_GLOBALSEC_DUMMYKEY2_OFFSET,
};

static uint32_t sb_bl_sig[REG_GLOBALSEC_SB_BL_SIG_COUNT];
static uint32_t permission_runlevel[REG_GLOBALSEC_PERMISSION_COUNT];
static bool sb_comp_status;
static uint32_t hide_rom;

static int region_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_GLOBALSEC_REGION_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)(offset / sizeof(uint32_t));
}

static int permission_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_GLOBALSEC_CPU0_S_PERMISSION_OFFSET +
                 (uint64_t)REG_GLOBALSEC_PERMISSION_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_GLOBALSEC_CPU0_S_PERMISSION_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_GLOBALSEC_CPU0_S_PERMISSION_OFFSET) / sizeof(uint32_t));
}

static uint32_t permission_decrement(uint32_t level) {
  switch (level) {
    case GLOBALSEC_PERMISSION_HIGHEST:
      return GLOBALSEC_PERMISSION_HIGH;
    case GLOBALSEC_PERMISSION_HIGH:
      return GLOBALSEC_PERMISSION_MEDIUM;
    case GLOBALSEC_PERMISSION_MEDIUM:
      return GLOBALSEC_PERMISSION_LOW;
    default:
      return GLOBALSEC_PERMISSION_LOW;
  }
}

static uint64_t globalsec_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static unsigned globalsec_alert_irq(unsigned index) {
  unsigned irq = GLOBALSEC_ALERT_IRQ0 + index;

  if (index >= GLOBALSEC_ALERT_IRQ_GAP_INDEX) irq += GLOBALSEC_ALERT_IRQ_GAP;
  return irq;
}

static void globalsec_dlyctr_poll(unsigned index) {
  uint64_t now;
  uint64_t elapsed;
  uint64_t ticks;

  if (!alert_dlyctr_active[index]) return;

  now = globalsec_now_ns();
  elapsed = now >= alert_dlyctr_ns[index] ? now - alert_dlyctr_ns[index] : 0;
  alert_dlyctr_ns[index] = now;
  ticks = (elapsed * GLOBALSEC_ALERT_DLYCTR_HZ) / 1000000000ULL;
  if (ticks == 0) return;

  if (ticks < alert_dlyctr_ctr[index]) {
    alert_dlyctr_ctr[index] -= (uint32_t)ticks;
    return;
  }

  alert_dlyctr_ctr[index] = 0;
  alert_dlyctr_active[index] = false;

  if (alert_dlyctr_shutdown[index] != 0) {
    runtime_fault(RC_CHIPEMU_ALERT_SHUTDOWN, "globalsec alert delay %u expired", index);
  }
}

void GLOBALSEC_Poll(void) {
  unsigned i;

  for (i = 0; i < GLOBALSEC_ALERT_GROUP_COUNT; i++) globalsec_dlyctr_poll(i);
}

static void globalsec_alert_trigger(unsigned index) {
  unsigned i;
  unsigned word = index / 32u;
  uint32_t bit = 1u << (index % 32u);

  if (index >= REG_GLOBALSEC_ALERT_COUNT) return;

  alert_intr_sts[word] |= bit;
  if ((alert_nmi_en[word] & bit) != 0) M3_PendSysException(2);
  M3_PendIrq(globalsec_alert_irq(index));

  for (i = 0; i < GLOBALSEC_ALERT_GROUP_COUNT; i++) {
    if ((alert_group_en[i][word] & bit) != 0) {
      alert_group_ctr[i]++;
      if (alert_group_threshold[i] != 0 && alert_group_ctr[i] == alert_group_threshold[i]) {
        M3_PendIrq(GLOBALSEC_ALERT_GROUP_IRQ0 + i);
      }
    }

    if ((alert_dlyctr_en[i][word] & bit) != 0) {
      alert_dlyctr_ctr[i] = alert_dlyctr_len[i];
      alert_dlyctr_ns[i] = globalsec_now_ns();
      alert_dlyctr_active[i] = true;
    }
  }

  LOG_VBS("GLOBALSEC: alert %u triggered\n", index);
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

  index = permission_index(offset);
  if (index >= 0) {
    LOG_VBS("GLOBALSEC: read PERMISSION[0x%03llx] (size=%u) -> 0x%02x\n",
            (unsigned long long)offset, size, permission_runlevel[index]);
    return permission_runlevel[index];
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
    case REG_GLOBALSEC_ALERT_CFG_LOCK_OFFSET:
      return alert_cfg_lock;
    case REG_GLOBALSEC_ALERT_FW_TRIGGER_OFFSET:
      return alert_fw_trigger;
    case REG_GLOBALSEC_ALERT_INTR_STS0_OFFSET:
      return alert_intr_sts[0];
    case REG_GLOBALSEC_ALERT_INTR_STS1_OFFSET:
      return alert_intr_sts[1];
    case REG_GLOBALSEC_ALERT_NMI_EN0_OFFSET:
      return alert_nmi_en[0];
    case REG_GLOBALSEC_ALERT_NMI_EN1_OFFSET:
      return alert_nmi_en[1];
    case REG_GLOBALSEC_ALERT_GROUPA_EN0_OFFSET:
      return alert_group_en[0][0];
    case REG_GLOBALSEC_ALERT_GROUPA_EN1_OFFSET:
      return alert_group_en[0][1];
    case REG_GLOBALSEC_ALERT_GROUPB_EN0_OFFSET:
      return alert_group_en[1][0];
    case REG_GLOBALSEC_ALERT_GROUPB_EN1_OFFSET:
      return alert_group_en[1][1];
    case REG_GLOBALSEC_ALERT_GROUPC_EN0_OFFSET:
      return alert_group_en[2][0];
    case REG_GLOBALSEC_ALERT_GROUPC_EN1_OFFSET:
      return alert_group_en[2][1];
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN0_OFFSET:
      return alert_dlyctr_en[0][0];
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN1_OFFSET:
      return alert_dlyctr_en[0][1];
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN0_OFFSET:
      return alert_dlyctr_en[1][0];
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN1_OFFSET:
      return alert_dlyctr_en[1][1];
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN0_OFFSET:
      return alert_dlyctr_en[2][0];
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN1_OFFSET:
      return alert_dlyctr_en[2][1];
    case REG_GLOBALSEC_ALERT_GROUPA_CTR_OFFSET:
      return alert_group_ctr[0];
    case REG_GLOBALSEC_ALERT_GROUPB_CTR_OFFSET:
      return alert_group_ctr[1];
    case REG_GLOBALSEC_ALERT_GROUPC_CTR_OFFSET:
      return alert_group_ctr[2];
    case REG_GLOBALSEC_ALERT_GROUPA_THRESHOLD_OFFSET:
      return alert_group_threshold[0];
    case REG_GLOBALSEC_ALERT_GROUPB_THRESHOLD_OFFSET:
      return alert_group_threshold[1];
    case REG_GLOBALSEC_ALERT_GROUPC_THRESHOLD_OFFSET:
      return alert_group_threshold[2];
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      return alert_control;
    case REG_GLOBALSEC_ALERT_DLYCTR0_OFFSET:
      globalsec_dlyctr_poll(0);
      return alert_dlyctr_ctr[0];
    case REG_GLOBALSEC_ALERT_DLYCTR1_OFFSET:
      globalsec_dlyctr_poll(1);
      return alert_dlyctr_ctr[1];
    case REG_GLOBALSEC_ALERT_DLYCTR2_OFFSET:
      globalsec_dlyctr_poll(2);
      return alert_dlyctr_ctr[2];
    case REG_GLOBALSEC_ALERT_DLYCTR0_LEN_OFFSET:
      return alert_dlyctr_len[0];
    case REG_GLOBALSEC_ALERT_DLYCTR1_LEN_OFFSET:
      return alert_dlyctr_len[1];
    case REG_GLOBALSEC_ALERT_DLYCTR2_LEN_OFFSET:
      return alert_dlyctr_len[2];
    case REG_GLOBALSEC_ALERT_DLYCTR0_SHUTDOWN_EN_OFFSET:
      return alert_dlyctr_shutdown[0];
    case REG_GLOBALSEC_ALERT_DLYCTR1_SHUTDOWN_EN_OFFSET:
      return alert_dlyctr_shutdown[1];
    case REG_GLOBALSEC_ALERT_DLYCTR2_SHUTDOWN_EN_OFFSET:
      return alert_dlyctr_shutdown[2];
    case REG_GLOBALSEC_ALERT_DLYCTR0_CLEAR_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_CLEAR_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_CLEAR_OFFSET:
      return 0;
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
  unsigned i;

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("GLOBALSEC: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  index = permission_index(offset);
  if (index >= 0) {
    permission_runlevel[index] = permission_decrement(permission_runlevel[index]);
    LOG_VBS("GLOBALSEC: wrote PERMISSION[0x%03llx] (size=%u) -> 0x%02x\n",
            (unsigned long long)offset, size, permission_runlevel[index]);
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
    case REG_GLOBALSEC_ALERT_CFG_LOCK_OFFSET:
      alert_cfg_lock = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_FW_TRIGGER_OFFSET:
      alert_fw_trigger = (uint32_t)value;
      if ((uint32_t)value == GLOBALSEC_ALERT_FW_TRIGGER_MAGIC) {
        for (i = 0; i < GLOBALSEC_ALERT_FW_COUNT; i++) {
          globalsec_alert_trigger(GLOBALSEC_ALERT_FW_FIRST_INDEX + i);
        }
      }
      return;
    case REG_GLOBALSEC_ALERT_INTR_STS0_OFFSET:
      alert_intr_sts[0] &= ~(uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_INTR_STS1_OFFSET:
      alert_intr_sts[1] &= ~(uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_NMI_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_NMI_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPA_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPA_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPB_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPB_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPC_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPC_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN0_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN1_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPA_THRESHOLD_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPB_THRESHOLD_OFFSET:
    case REG_GLOBALSEC_ALERT_GROUPC_THRESHOLD_OFFSET:
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR0_LEN_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_LEN_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_LEN_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR0_SHUTDOWN_EN_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_SHUTDOWN_EN_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_SHUTDOWN_EN_OFFSET:
      if (alert_cfg_lock != 0) {
        LOG_VBS("GLOBALSEC: ignored locked alert config write 0x%03llx\n",
                (unsigned long long)offset);
        return;
      }
      break;
    case REG_GLOBALSEC_ALERT_GROUPA_CTR_OFFSET:
      alert_group_ctr[0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPB_CTR_OFFSET:
      alert_group_ctr[1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPC_CTR_OFFSET:
      alert_group_ctr[2] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_OFFSET:
      alert_dlyctr_ctr[0] = (uint32_t)value;
      alert_dlyctr_active[0] = false;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR1_OFFSET:
      alert_dlyctr_ctr[1] = (uint32_t)value;
      alert_dlyctr_active[1] = false;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR2_OFFSET:
      alert_dlyctr_ctr[2] = (uint32_t)value;
      alert_dlyctr_active[2] = false;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_CLEAR_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR1_CLEAR_OFFSET:
    case REG_GLOBALSEC_ALERT_DLYCTR2_CLEAR_OFFSET:
      if (value != 0) {
        unsigned dlyctr = (unsigned)((offset - REG_GLOBALSEC_ALERT_DLYCTR0_CLEAR_OFFSET) / 4u);

        alert_dlyctr_ctr[dlyctr] = 0;
        alert_dlyctr_active[dlyctr] = false;
      }
      return;
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

  switch (offset) {
    case REG_GLOBALSEC_ALERT_NMI_EN0_OFFSET:
      alert_nmi_en[0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_NMI_EN1_OFFSET:
      alert_nmi_en[1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPA_EN0_OFFSET:
      alert_group_en[0][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPA_EN1_OFFSET:
      alert_group_en[0][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPB_EN0_OFFSET:
      alert_group_en[1][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPB_EN1_OFFSET:
      alert_group_en[1][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPC_EN0_OFFSET:
      alert_group_en[2][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPC_EN1_OFFSET:
      alert_group_en[2][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN0_OFFSET:
      alert_dlyctr_en[0][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_EN1_OFFSET:
      alert_dlyctr_en[0][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN0_OFFSET:
      alert_dlyctr_en[1][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR1_EN1_OFFSET:
      alert_dlyctr_en[1][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN0_OFFSET:
      alert_dlyctr_en[2][0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR2_EN1_OFFSET:
      alert_dlyctr_en[2][1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPA_THRESHOLD_OFFSET:
      alert_group_threshold[0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPB_THRESHOLD_OFFSET:
      alert_group_threshold[1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_GROUPC_THRESHOLD_OFFSET:
      alert_group_threshold[2] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      alert_control = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_LEN_OFFSET:
      alert_dlyctr_len[0] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR1_LEN_OFFSET:
      alert_dlyctr_len[1] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR2_LEN_OFFSET:
      alert_dlyctr_len[2] = (uint32_t)value;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR0_SHUTDOWN_EN_OFFSET:
      alert_dlyctr_shutdown[0] = (uint32_t)value & 1u;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR1_SHUTDOWN_EN_OFFSET:
      alert_dlyctr_shutdown[1] = (uint32_t)value & 1u;
      return;
    case REG_GLOBALSEC_ALERT_DLYCTR2_SHUTDOWN_EN_OFFSET:
      alert_dlyctr_shutdown[2] = (uint32_t)value & 1u;
      return;
    default:
      return;
  }
}

Component *GLOBALSEC_Initialize(void) {
  unsigned i;

  alert_control = 0;
  alert_cfg_lock = 0;
  alert_fw_trigger = 0xAA;
  alert_intr_sts[0] = 0;
  alert_intr_sts[1] = 0;
  alert_nmi_en[0] = 0;
  alert_nmi_en[1] = 0;
  dbg_control = 0;
  obfs_sw_en = 0;
  sb_comp_status = false;
  hide_rom = 0;

  for (i = 0; i < GLOBALSEC_ALERT_GROUP_COUNT; i++) {
    alert_group_en[i][0] = 0;
    alert_group_en[i][1] = 0;
    alert_dlyctr_en[i][0] = 0;
    alert_dlyctr_en[i][1] = 0;
    alert_group_ctr[i] = 0;
    alert_group_threshold[i] = 0x64;
    alert_dlyctr_ctr[i] = 0;
    alert_dlyctr_len[i] = 0xFFFF;
    alert_dlyctr_shutdown[i] = 0;
    alert_dlyctr_ns[i] = 0;
    alert_dlyctr_active[i] = false;
  }

  memset(region_regs, 0, sizeof(region_regs));
  memset(dummykey, 0, sizeof(dummykey));
  memset(sb_bl_sig, 0, sizeof(sb_bl_sig));
  for (i = 0; i < REG_GLOBALSEC_PERMISSION_COUNT; i++) {
    permission_runlevel[i] = GLOBALSEC_PERMISSION_HIGHEST;
  }

  return component_register("GLOBALSEC", NULL, GLOBALSEC_Read, GLOBALSEC_Write);
}
