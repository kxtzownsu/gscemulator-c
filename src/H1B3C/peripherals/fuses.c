#include <H1B3C/peripherals/fuses.h>

#include <stddef.h>
#include <stdint.h>

#include <common/logger.h>

typedef struct {
  uint32_t offset;
  uint32_t value;
} FuseDefault;

static const FuseDefault fuse_defaults[] = {
  { REG_FUSE_DEV_ID0_OFFSET, 0x0300903C },
  { REG_FUSE_DEV_ID1_OFFSET, 0x942BCE84 },
  { REG_FUSE_RC_JTR_OSC48_CC_TRIM_OFFSET, 0x55555544 },
  { REG_FUSE_RC_JTR_OSC60_CC_TRIM_OFFSET, 0x5555552C },
  { REG_FUSE_RC_TIMER_OSC48_CC_TRIM_OFFSET, 0x55555548 },
  { REG_FUSE_RC_TIMER_OSC48_FC_TRIM_OFFSET, 0x55555550 },
  { REG_FUSE_RC_RTC_OSC256K_CC_TRIM_OFFSET, 0x555555F0 },
  { REG_FUSE_TRNG_LDO_CTRL_OFFSET, 0x5555554E },
  { REG_FUSE_OBFUSCATION_EN_OFFSET, 0x55555550 },
  { REG_FUSE_FW_DEFINED_DATA_BLK0_OFFSET, 0x55555502 },
  { REG_FUSE_FW_DEFINED_BROM_ERR_RESPONSE_OFFSET, 0x5555F1FF },
  { REG_FUSE_FW_DEFINED_BROM_APPLYSEC_OFFSET, 0x55555137 },
  { REG_FUSE_FW_DEFINED_BROM_CONFIG0_OFFSET, 0x55555506 },
  { REG_FUSE_FW_DEFINED_BROM_CONFIG1_OFFSET, 0x55555500 },
  { REG_FUSE_RBOX_KEY_COMBO0_VAL_OFFSET, 0x555555C0 },
  { REG_FUSE_RBOX_KEY_COMBO0_HOLD_OFFSET, 0x55555500 },
  { REG_FUSE_FW_DEFINED_DATA_EXTRA_BLK6_OFFSET, 0x55555540 },
};

static uint64_t fuses_Read(Component *self, uint64_t offset, unsigned size) {
  size_t i;

  (void)self;

  for (i = 0; i < sizeof(fuse_defaults) / sizeof(fuse_defaults[0]); i++) {
    if (fuse_defaults[i].offset == offset) {
      LOG_VBS("FUSE: read 0x%04llx (size=%u) -> 0x%08x\n", (unsigned long long)offset,
              size, fuse_defaults[i].value);
      return fuse_defaults[i].value;
    }
  }

  LOG_VBS("FUSE: read 0x%04llx (size=%u) -> blank\n", (unsigned long long)offset, size);
  return REG_FUSE_BLANK_VALUE;
}

static void fuses_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  (void)self;

  LOG_VBS("FUSE: ignored write to 0x%04llx (size=%u value=0x%llx)\n",
          (unsigned long long)offset, size, (unsigned long long)value);
}

Component *eFuses_Initialize(void) {
  return component_register("FUSE", NULL, fuses_Read, fuses_Write);
}
