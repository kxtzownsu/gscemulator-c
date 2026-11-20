#include <H1B3C/peripherals/rbox.h>
#include <H1B3C/peripherals/gpio.h>
#include <H1B3C/peripherals/m3.h>
#include <H1B3C/peripherals/pinmux.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>

#define RBOX_POWER_BUTTON_PULSE_MS 200u

static uint32_t rbox_regs[REG_RBOX_COUNT];
static _Atomic unsigned rbox_power_button_requests;
static bool rbox_power_button_pressed;
static uint64_t rbox_power_button_release_at;

static int rbox_index(uint64_t offset) {
  uint64_t end = (uint64_t)REG_RBOX_OFFSET + (uint64_t)REG_RBOX_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset >= end) return -1;

  return (int)((offset - REG_RBOX_OFFSET) / sizeof(uint32_t));
}

static uint64_t rbox_now_ms(void) {
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;

  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void rbox_power_button_set(bool pressed) {
  uint32_t *check_input = &rbox_regs[REG_RBOX_CHECK_INPUT_OFFSET / sizeof(uint32_t)];
  uint32_t *int_state = &rbox_regs[REG_RBOX_INT_STATE_OFFSET / sizeof(uint32_t)];
  uint32_t int_enable = rbox_regs[REG_RBOX_INT_ENABLE_OFFSET / sizeof(uint32_t)];

  if (pressed) {
    *check_input &= ~REG_RBOX_CHECK_INPUT_PWRB_IN_MASK;
    *int_state |= REG_RBOX_INT_STATE_PWRB_IN_FED_MASK;

    if ((int_enable & REG_RBOX_INT_ENABLE_PWRB_IN_FED_MASK) != 0) {
      M3_PendIrq(REG_RBOX_IRQ_PWRB_IN_FED);
    }
  } else {
    *check_input |= REG_RBOX_CHECK_INPUT_PWRB_IN_MASK;
    *int_state |= REG_RBOX_INT_STATE_PWRB_IN_RED_MASK;

    if ((int_enable & REG_RBOX_INT_ENABLE_PWRB_IN_RED_MASK) != 0) {
      M3_PendIrq(REG_RBOX_IRQ_PWRB_IN_RED);
    }
  }

  rbox_power_button_pressed = pressed;
  LOG_VBS("RBOX: power button %s\n", pressed ? "pressed" : "released");
}

static void RBOX_Poll(Component *self) {
  uint64_t now;

  (void)self;

  if (rbox_power_button_pressed) {
    now = rbox_now_ms();

    if (now == 0 || now >= rbox_power_button_release_at) rbox_power_button_set(false);

    return;
  }

  if (atomic_load(&rbox_power_button_requests) == 0) return;

  atomic_fetch_sub(&rbox_power_button_requests, 1);
  rbox_power_button_set(true);
  rbox_power_button_release_at = rbox_now_ms() + RBOX_POWER_BUTTON_PULSE_MS;
}

static uint64_t RBOX_Read(Component *self, uint64_t offset, unsigned size) {
  int index = rbox_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "read", offset);
    return 0;
  }

  LOG_VBS("RBOX: read 0x%03llx (size=%u) -> 0x%08x\n", (unsigned long long)offset, size,
          rbox_regs[index]);
  return rbox_regs[index];
}

static void RBOX_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index = rbox_index(offset);

  if (index < 0) {
    component_log_unhandled(self, "write", offset);
    return;
  }

  if (offset == REG_RBOX_INT_STATE_OFFSET) {
    rbox_regs[index] &= ~(uint32_t)value;
  } else if (offset == REG_RBOX_ASSERT_EC_RST_OFFSET) {
    rbox_regs[index] = (uint32_t)value & 1u;
    PINMUX_SetPadLevel(0x1B, (uint32_t)value == 0);
    GPIO_Refresh();
  } else {
    rbox_regs[index] = (uint32_t)value;
  }

  LOG_VBS("RBOX: wrote 0x%03llx (size=%u) <- 0x%08x\n", (unsigned long long)offset, size,
          rbox_regs[index]);
}

void RBOX_RequestPowerButton(void) {
  atomic_fetch_add(&rbox_power_button_requests, 1);
}

Component *RBOX_Initialize(void) {
  Component *component;

  memset(rbox_regs, 0, sizeof(rbox_regs));
  rbox_regs[REG_RBOX_CHECK_INPUT_OFFSET / sizeof(uint32_t)] = REG_RBOX_CHECK_INPUT_PWRB_IN_MASK;
  rbox_regs[REG_RBOX_VERSION_OFFSET / sizeof(uint32_t)] = 0x1014125;

  atomic_store(&rbox_power_button_requests, 0);
  rbox_power_button_pressed = false;
  rbox_power_button_release_at = 0;

  component = component_register("RBOX", NULL, RBOX_Read, RBOX_Write);
  if (component != NULL) component_set_poll(component, RBOX_Poll);

  return component;
}
