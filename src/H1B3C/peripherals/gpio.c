#include <H1B3C/peripherals/gpio.h>
#include <H1B3C/peripherals/m3.h>
#include <H1B3C/peripherals/pinmux.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/logger.h>

#define GPIO0_PIN_IRQ0 65u
#define GPIO1_PIN_IRQ0 82u
#define GPIO0_COMB_IRQ 81u
#define GPIO1_COMB_IRQ 98u
#define GPIO_POLL_MASK 0x3Fu

typedef struct {
  unsigned bank;
  uint32_t levels;
  uint32_t dataout;
  uint32_t douten;
  uint32_t inten;
  uint32_t inttype;
  uint32_t intpol;
  uint32_t intstat;
  uint32_t reserved0;
  uint32_t reserved1;
  uint32_t override_mask;
  uint32_t override_levels;
} GpioState;

static GpioState gpio_states[2];

static unsigned gpio_pin_irq(unsigned bank, unsigned pin) {
  return (bank == 0 ? GPIO0_PIN_IRQ0 : GPIO1_PIN_IRQ0) + pin;
}

static unsigned gpio_comb_irq(unsigned bank) {
  return bank == 0 ? GPIO0_COMB_IRQ : GPIO1_COMB_IRQ;
}

static uint32_t gpio_sample_levels(const GpioState *state) {
  uint32_t levels = 0;
  unsigned pin;

  for (pin = 0; pin < 16; pin++) {
    bool level;

    if ((state->override_mask & (1u << pin)) != 0) {
      level = (state->override_levels & (1u << pin)) != 0;
    } else if ((state->douten & (1u << pin)) != 0) {
      level = (state->dataout & (1u << pin)) != 0;
    } else {
      level = PINMUX_PadLevel(PINMUX_GetGpioPad(state->bank, pin)) != 0;
    }

    if (level) levels |= 1u << pin;
  }

  return levels;
}

static void gpio_pend_pin(GpioState *state, unsigned pin) {
  state->intstat |= 1u << pin;
  M3_PendIrq(gpio_pin_irq(state->bank, pin));
  M3_PendIrq(gpio_comb_irq(state->bank));
}

static void gpio_apply_levels(unsigned bank) {
  GpioState *state = &gpio_states[bank];
  uint32_t levels = gpio_sample_levels(state);
  uint32_t changed = levels ^ state->levels;
  unsigned pin;

  state->levels = levels;

  for (pin = 0; pin < 16; pin++) {
    uint32_t bit = 1u << pin;
    bool high = (levels & bit) != 0;
    bool enabled = (state->inten & bit) != 0;
    bool edge = (state->inttype & bit) != 0;
    bool active = (state->intpol & bit) != 0;

    if (!edge) {
      if (high == active) {
        if (enabled && (state->intstat & bit) == 0) gpio_pend_pin(state, pin);
      } else if ((state->intstat & bit) != 0) {
        state->intstat &= ~bit;
        M3_UnpendIrq(gpio_pin_irq(bank, pin));
        if (state->intstat == 0) M3_UnpendIrq(gpio_comb_irq(bank));
      }
      continue;
    }

    if ((changed & bit) == 0) continue;
    if (!enabled || high != active) continue;
    if ((state->intstat & bit) != 0) continue;

    gpio_pend_pin(state, pin);
  }
}

static void gpio_refresh_interrupts(unsigned bank) {
  GpioState *state = &gpio_states[bank];
  unsigned pin;

  for (pin = 0; pin < 16; pin++) {
    uint32_t bit = 1u << pin;
    bool high = (state->levels & bit) != 0;
    bool active = (state->intpol & bit) != 0;
    bool edge = (state->inttype & bit) != 0;
    bool fire;

    if ((state->inten & bit) == 0) continue;
    if ((state->intstat & bit) != 0) continue;

    fire = edge ? false : (high == active);
    if (fire) gpio_pend_pin(state, pin);
  }
}

void GPIO_SetInputOverride(unsigned bank, unsigned pin, bool level) {
  GpioState *state;

  if (bank > 1 || pin > 15) return;

  state = &gpio_states[bank];
  state->override_mask |= 1u << pin;

  if (level) {
    state->override_levels |= 1u << pin;
  } else {
    state->override_levels &= ~(1u << pin);
  }

  state->levels = gpio_sample_levels(state);
}

void GPIO_Refresh(void) {
  gpio_apply_levels(0);
  gpio_apply_levels(1);
}

void GPIO_Poll(void) {
  static unsigned count;

  if ((count++ & GPIO_POLL_MASK) != 0) return;

  gpio_apply_levels(0);
  gpio_apply_levels(1);
}

static int mask_index(uint64_t offset, uint32_t base) {
  uint64_t end = (uint64_t)base + (uint64_t)REG_GPIO_MASK_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < base || offset >= end) return -1;

  return (int)((offset - base) / sizeof(uint32_t));
}

static uint64_t GPIO_Read(Component *self, uint64_t offset, unsigned size) {
  GpioState *state = self->state;
  int index;

  switch (offset) {
    case REG_GPIO_DATAIN_OFFSET: {
      uint32_t value = 0;
      unsigned pin;

      for (pin = 0; pin < 16; pin++) {
        bool level;

        if ((state->override_mask & (1u << pin)) != 0) {
          level = (state->override_levels & (1u << pin)) != 0;
        } else if ((state->douten & (1u << pin)) != 0) {
          level = (state->dataout & (1u << pin)) != 0;
        } else {
          level = PINMUX_PadLevel(PINMUX_GetGpioPad(state->bank, pin)) != 0;
        }

        if (level) value |= 1u << pin;
      }

      state->levels = value;
      LOG_VBS("GPIO: read DATAIN (size=%u) -> 0x%04x\n", size, value);
      return value;
    }
    case REG_GPIO_DOUT_OFFSET:
      LOG_VBS("GPIO: read DOUT (size=%u) -> 0x%04x\n", size, state->dataout);
      return state->dataout;
    case REG_GPIO_SETDOUTEN_OFFSET:
    case REG_GPIO_CLRDOUTEN_OFFSET:
      LOG_VBS("GPIO: read DOUTEN (size=%u) -> 0x%04x\n", size, state->douten);
      return state->douten;
    case REG_GPIO_RESERVED0_OFFSET:
      return state->reserved0;
    case REG_GPIO_RESERVED1_OFFSET:
      return state->reserved1;
    case REG_GPIO_SETINTEN_OFFSET:
    case REG_GPIO_CLRINTEN_OFFSET:
      LOG_VBS("GPIO: read INTEN (size=%u) -> 0x%04x\n", size, state->inten);
      return state->inten;
    case REG_GPIO_SETINTTYPE_OFFSET:
    case REG_GPIO_CLRINTTYPE_OFFSET:
      LOG_VBS("GPIO: read INTTYPE (size=%u) -> 0x%04x\n", size, state->inttype);
      return state->inttype;
    case REG_GPIO_SETINTPOL_OFFSET:
    case REG_GPIO_CLRINTPOL_OFFSET:
      LOG_VBS("GPIO: read INTPOL (size=%u) -> 0x%04x\n", size, state->intpol);
      return state->intpol;
    case REG_GPIO_CLRINTSTAT_OFFSET:
      LOG_VBS("GPIO: read INTSTAT (size=%u) -> 0x%04x\n", size, state->intstat);
      return state->intstat;
    default:
      break;
  }

  if (offset >= REG_GPIO_ARMPID4_OFFSET && offset <= REG_GPIO_ARMCID3_OFFSET) {
    switch (offset) {
      case REG_GPIO_ARMCID0_OFFSET:
        return 0x0D;
      case REG_GPIO_ARMCID1_OFFSET:
        return 0xF0;
      case REG_GPIO_ARMCID2_OFFSET:
        return 0x05;
      case REG_GPIO_ARMCID3_OFFSET:
        return 0xB1;
      default:
        return 0;
    }
  }

  index = mask_index(offset, REG_GPIO_MASKLOWBYTE_OFFSET);
  if (index >= 0) {
    LOG_VBS("GPIO: read MASKLOWBYTE%d (size=%u) -> 0x%04x\n", index, size,
            state->dataout & (uint32_t)index);
    return state->dataout & (uint32_t)index;
  }

  index = mask_index(offset, REG_GPIO_MASKHIGHBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index << 8;

    LOG_VBS("GPIO: read MASKHIGHBYTE%d (size=%u) -> 0x%04x\n", index, size,
            state->dataout & mask);
    return state->dataout & mask;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void GPIO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  GpioState *state = self->state;
  uint32_t bits = (uint32_t)value & 0xFFFFu;
  int index;

  switch (offset) {
    case REG_GPIO_DATAIN_OFFSET:
      LOG_VBS("GPIO: ignored write to DATAIN\n");
      return;
    case REG_GPIO_DOUT_OFFSET:
      state->dataout = bits;
      LOG_VBS("GPIO: wrote DOUT (size=%u) <- 0x%04x\n", size, state->dataout);
      return;
    case REG_GPIO_SETDOUTEN_OFFSET:
      state->douten |= bits;
      LOG_VBS("GPIO: wrote SETDOUTEN (size=%u) <- 0x%04x\n", size, state->douten);
      return;
    case REG_GPIO_CLRDOUTEN_OFFSET:
      state->douten &= ~bits;
      LOG_VBS("GPIO: wrote CLRDOUTEN (size=%u) <- 0x%04x\n", size, state->douten);
      return;
    case REG_GPIO_RESERVED0_OFFSET:
      state->reserved0 = bits;
      return;
    case REG_GPIO_RESERVED1_OFFSET:
      state->reserved1 = bits;
      return;
    case REG_GPIO_SETINTEN_OFFSET:
      state->inten |= bits;
      LOG_VBS("GPIO: wrote SETINTEN (size=%u) <- 0x%04x\n", size, state->inten);
      gpio_refresh_interrupts(state->bank);
      return;
    case REG_GPIO_CLRINTEN_OFFSET:
      state->inten &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTEN (size=%u) <- 0x%04x\n", size, state->inten);
      return;
    case REG_GPIO_SETINTTYPE_OFFSET:
      state->inttype |= bits;
      LOG_VBS("GPIO: wrote SETINTTYPE (size=%u) <- 0x%04x\n", size, state->inttype);
      gpio_refresh_interrupts(state->bank);
      return;
    case REG_GPIO_CLRINTTYPE_OFFSET:
      state->inttype &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTTYPE (size=%u) <- 0x%04x\n", size, state->inttype);
      return;
    case REG_GPIO_SETINTPOL_OFFSET:
      state->intpol |= bits;
      LOG_VBS("GPIO: wrote SETINTPOL (size=%u) <- 0x%04x\n", size, state->intpol);
      gpio_refresh_interrupts(state->bank);
      return;
    case REG_GPIO_CLRINTPOL_OFFSET:
      state->intpol &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTPOL (size=%u) <- 0x%04x\n", size, state->intpol);
      return;
    case REG_GPIO_CLRINTSTAT_OFFSET: {
      unsigned pin;

      state->intstat &= ~bits;
      for (pin = 0; pin < 16; pin++) {
        if ((bits & (1u << pin)) != 0) M3_UnpendIrq(gpio_pin_irq(state->bank, pin));
      }
      if (state->intstat == 0) M3_UnpendIrq(gpio_comb_irq(state->bank));
      LOG_VBS("GPIO: wrote CLRINTSTAT (size=%u) <- 0x%04x\n", size, state->intstat);
      return;
    }
    default:
      break;
  }

  if (offset >= REG_GPIO_ARMPID4_OFFSET && offset <= REG_GPIO_ARMCID3_OFFSET) return;

  index = mask_index(offset, REG_GPIO_MASKLOWBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index;

    state->dataout = (state->dataout & ~mask) | (bits & mask);
    LOG_VBS("GPIO: wrote MASKLOWBYTE%d (size=%u) <- 0x%04x\n", index, size, state->dataout);
    return;
  }

  index = mask_index(offset, REG_GPIO_MASKHIGHBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index << 8;

    state->dataout = (state->dataout & ~mask) | (bits & mask);
    LOG_VBS("GPIO: wrote MASKHIGHBYTE%d (size=%u) <- 0x%04x\n", index, size, state->dataout);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *GPIO_Initialize(unsigned bank) {
  const char *name;

  if (bank > 1) return NULL;

  name = bank == 0 ? "GPIO0" : "GPIO1";
  memset(&gpio_states[bank], 0, sizeof(gpio_states[bank]));
  gpio_states[bank].bank = bank;

  return component_register(name, &gpio_states[bank], GPIO_Read, GPIO_Write);
}
