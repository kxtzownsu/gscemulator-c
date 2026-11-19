#include <H1B3C/peripherals/pinmux.h>

#include <stdbool.h>
#include <stdint.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

typedef enum {
  PINMUX_KIND_SEL = 0,
  PINMUX_KIND_CTL,
  PINMUX_KIND_OTHER,
} PinmuxKind;

#define PINMUX_PAD_ID_MAX 0x1Eu
#define PINMUX_CTL_PD (1u << 3)
#define PINMUX_CTL_PU (1u << 4)

static uint32_t pinmux_regs[REG_PINMUX_REG_COUNT];

static const struct {
  uint32_t pad;
  uint8_t level;
} pinmux_external_levels[] = {
  { 0x18, 1 },
  { 0x10, 1 },
  { 0x13, 0 },
  { 0x0D, 0 },
};

static bool pinmux_lookup(uint64_t offset, PinmuxKind *kind) {
  if ((offset & 3u) != 0) return false;
  if (offset >= (uint64_t)REG_PINMUX_REG_COUNT * sizeof(uint32_t)) return false;

  if (offset <= REG_PINMUX_PAD_SEL_END_OFFSET) {
    *kind = (offset & 4u) != 0 ? PINMUX_KIND_CTL : PINMUX_KIND_SEL;
  } else if (offset <= REG_PINMUX_OTHER_SEL_END_OFFSET) {
    *kind = PINMUX_KIND_SEL;
  } else {
    *kind = PINMUX_KIND_OTHER;
  }

  return true;
}

static uint64_t PINMUX_Read(Component *self, uint64_t offset, unsigned size) {
  PinmuxKind kind;

  if (!pinmux_lookup(offset, &kind)) {
    component_log_unhandled(self, "read", offset);
    return 0;
  }

  LOG_VBS("PINMUX: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset,
          size, pinmux_regs[offset / sizeof(uint32_t)]);
  return pinmux_regs[offset / sizeof(uint32_t)];
}

static void PINMUX_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  PinmuxKind kind;
  uint32_t reg_value = (uint32_t)value;

  if (!pinmux_lookup(offset, &kind)) {
    component_log_unhandled(self, "write", offset);
    return;
  }

  if (kind == PINMUX_KIND_SEL && reg_value > 0x63) {
    LOG_ERR("PINMUX: invalid SEL value 0x%x at 0x%03llx\n", reg_value,
            (unsigned long long)offset);
    reg_value = 0;
  } else if (kind == PINMUX_KIND_CTL && (reg_value & 0x18u) == 0x18u) {
    LOG_ERR("PINMUX: conflicting pull-up/pull-down at 0x%03llx\n",
            (unsigned long long)offset);
  }

  pinmux_regs[offset / sizeof(uint32_t)] = reg_value;
  LOG_VBS("PINMUX: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset,
          size, reg_value);
}

uint32_t PINMUX_GetGpioPad(unsigned bank, unsigned pin) {
  uint32_t base = REG_PINMUX_GPIO0_SEL_OFFSET + bank * (REG_PINMUX_GPIO1_SEL_OFFSET -
                                                          REG_PINMUX_GPIO0_SEL_OFFSET);

  return pinmux_regs[(base + pin * 4u) / 4u] & 0x1Fu;
}

int PINMUX_PadLevel(uint32_t pad) {
  uint32_t ctl;
  unsigned i;

  if (pad == 0 || pad > PINMUX_PAD_ID_MAX) return 0;

  ctl = pinmux_regs[((PINMUX_PAD_ID_MAX - pad) * 8u + 4u) / 4u];
  if ((ctl & PINMUX_CTL_PU) != 0) return 1;
  if ((ctl & PINMUX_CTL_PD) != 0) return 0;

  for (i = 0; i < ARRAY_LEN(pinmux_external_levels); i++) {
    if (pinmux_external_levels[i].pad == pad) return pinmux_external_levels[i].level;
  }

  return 0;
}

Component *PINMUX_Initialize(void) {
  return component_register("PINMUX", NULL, PINMUX_Read, PINMUX_Write);
}
