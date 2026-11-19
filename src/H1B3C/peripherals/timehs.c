#include <H1B3C/peripherals/timehs.h>
#include <H1B3C/peripherals/m3.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define TIMEHS_TICK_HZ 24000000u
#define TIMEHS_CONTROL_ONESHOT (1u << 0)
#define TIMEHS_CONTROL_INTENABLE (1u << 5)
#define TIMEHS_CONTROL_MODE (1u << 6)
#define TIMEHS_CONTROL_ENABLE (1u << 7)

typedef struct {
  uint32_t load;
  uint32_t bgload;
  uint32_t control;
  uint32_t value;
  uint32_t ris;
  uint64_t start_ns;
  uint32_t start_value;
  bool running;
} TimehsTimer;

typedef struct {
  TimehsTimer timer[2];
  uint32_t itcr;
  uint32_t itop;
} TimehsState;

static TimehsState timehs_states[2];

static uint64_t timehs_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static unsigned timehs_timer_irq(unsigned index, unsigned timer) {
  return (index == 0 ? 153u : 156u) + timer;
}

static unsigned timehs_combined_irq(unsigned index) {
  return index == 0 ? 155u : 158u;
}

static uint32_t timehs_reload_value(const TimehsTimer *timer) {
  if (timer->load != 0) return timer->load;
  if (timer->bgload != 0) return timer->bgload;
  return 0xFFFFFFFFu;
}

static void timehs_update(TimehsState *state, unsigned index, unsigned timer_index) {
  TimehsTimer *timer = &state->timer[timer_index];
  uint64_t elapsed_ns;
  uint64_t ticks;

  if (!timer->running) return;

  elapsed_ns = timehs_now_ns() - timer->start_ns;
  ticks = elapsed_ns * TIMEHS_TICK_HZ / 1000000000ull;

  if (ticks < timer->start_value) {
    timer->value = timer->start_value - (uint32_t)ticks;
    return;
  }

  timer->ris |= 1u;

  if ((timer->control & TIMEHS_CONTROL_ONESHOT) != 0) {
    timer->running = false;
    timer->value = 0;
  } else {
    timer->start_ns = timehs_now_ns();
    timer->start_value = timehs_reload_value(timer);
    timer->value = timer->start_value;
  }

  if ((timer->control & TIMEHS_CONTROL_INTENABLE) != 0) {
    M3_PendIrq(timehs_timer_irq(index, timer_index));
    M3_PendIrq(timehs_combined_irq(index));
  }
}

static uint64_t TIMEHS_Read(Component *self, uint64_t offset, unsigned size) {
  TimehsState *state = self->state;
  unsigned index = self->state == &timehs_states[0] ? 0 : 1;
  unsigned timer_index;

  (void)size;

  for (timer_index = 0; timer_index < 2; timer_index++) {
    TimehsTimer *timer = &state->timer[timer_index];
    uint32_t base = timer_index == 0 ? REG_TIMEHS_TIMER1LOAD_OFFSET : REG_TIMEHS_TIMER2LOAD_OFFSET;

    if (offset == base) return timer->load;
    if (offset == base + 4) {
      timehs_update(state, index, timer_index);
      return timer->value;
    }
    if (offset == base + 8) return timer->control;
    if (offset == base + 0xC) return 0;
    if (offset == base + 0x10) {
      timehs_update(state, index, timer_index);
      return timer->ris;
    }
    if (offset == base + 0x14) {
      timehs_update(state, index, timer_index);
      return timer->ris & ((timer->control & TIMEHS_CONTROL_INTENABLE) ? 1u : 0u);
    }
    if (offset == base + 0x18) return timer->bgload;
  }

  switch (offset) {
    case REG_TIMEHS_TIMERITCR_OFFSET:
      return state->itcr;
    case REG_TIMEHS_TIMERITOP_OFFSET:
      return state->itop;
    case REG_TIMEHS_TIMERPERIPHID4_OFFSET:
      return 0x4;
    case REG_TIMEHS_TIMERPERIPHID5_OFFSET:
      return 0x0;
    case REG_TIMEHS_TIMERPERIPHID6_OFFSET:
      return 0x0;
    case REG_TIMEHS_TIMERPERIPHID7_OFFSET:
      return 0x0;
    case REG_TIMEHS_TIMERPERIPHID0_OFFSET:
      return 0x23;
    case REG_TIMEHS_TIMERPERIPHID1_OFFSET:
      return 0xB8;
    case REG_TIMEHS_TIMERPERIPHID2_OFFSET:
      return 0xB;
    case REG_TIMEHS_TIMERPERIPHID3_OFFSET:
      return 0x0;
    case REG_TIMEHS_TIMERPCELLID0_OFFSET:
      return 0xD;
    case REG_TIMEHS_TIMERPCELLID1_OFFSET:
      return 0xF0;
    case REG_TIMEHS_TIMERPCELLID2_OFFSET:
      return 0x5;
    case REG_TIMEHS_TIMERPCELLID3_OFFSET:
      return 0xB1;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void TIMEHS_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  TimehsState *state = self->state;
  unsigned index = self->state == &timehs_states[0] ? 0 : 1;
  unsigned timer_index;

  (void)size;

  for (timer_index = 0; timer_index < 2; timer_index++) {
    TimehsTimer *timer = &state->timer[timer_index];
    uint32_t base = timer_index == 0 ? REG_TIMEHS_TIMER1LOAD_OFFSET : REG_TIMEHS_TIMER2LOAD_OFFSET;

    if (offset == base) {
      timer->load = (uint32_t)value;
      if (!timer->running) timer->value = timer->load;
      return;
    }
    if (offset == base + 4) return;
    if (offset == base + 8) {
      timehs_update(state, index, timer_index);
      timer->control = (uint32_t)value;

      if ((timer->control & TIMEHS_CONTROL_ENABLE) != 0 && !timer->running) {
        timer->start_ns = timehs_now_ns();
        timer->start_value = timehs_reload_value(timer);
        timer->value = timer->start_value;
        timer->running = true;
      } else if ((timer->control & TIMEHS_CONTROL_ENABLE) == 0 && timer->running) {
        timer->running = false;
      }
      return;
    }
    if (offset == base + 0xC) {
      if (value != 0) {
        timer->ris = 0;
        M3_UnpendIrq(timehs_timer_irq(index, timer_index));
        if ((state->timer[0].ris | state->timer[1].ris) == 0) {
          M3_UnpendIrq(timehs_combined_irq(index));
        }
      }
      return;
    }
    if (offset == base + 0x10 || offset == base + 0x14) return;
    if (offset == base + 0x18) {
      timer->bgload = (uint32_t)value;
      if (!timer->running) {
        timer->load = timer->bgload;
        timer->value = timer->load;
      }
      return;
    }
  }

  if (offset == REG_TIMEHS_TIMERITCR_OFFSET) {
    state->itcr = (uint32_t)value;
    return;
  }

  if (offset == REG_TIMEHS_TIMERITOP_OFFSET) {
    state->itop = (uint32_t)value;
    return;
  }

  if (offset >= REG_TIMEHS_TIMERPERIPHID4_OFFSET && offset <= REG_TIMEHS_TIMERPCELLID3_OFFSET) {
    return;
  }

  component_log_unhandled(self, "write", offset);
}

void TIMEHS_Poll(void) {
  unsigned index;
  unsigned timer_index;

  for (index = 0; index < 2; index++) {
    for (timer_index = 0; timer_index < 2; timer_index++) {
      timehs_update(&timehs_states[index], index, timer_index);
    }
  }
}

Component *TIMEHS_Initialize(unsigned index) {
  const char *name;

  if (index > 1) return NULL;

  name = index == 0 ? "TIMEHS0" : "TIMEHS1";
  memset(&timehs_states[index], 0, sizeof(timehs_states[index]));
  timehs_states[index].timer[0].control = 0x20;
  timehs_states[index].timer[1].control = 0x20;

  return component_register(name, &timehs_states[index], TIMEHS_Read, TIMEHS_Write);
}
