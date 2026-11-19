#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/timels.h>
#include <H1B3C/peripherals/m3.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <common/logger.h>

#define TIMELS_FREQ_HZ 262144ULL
#define TIMELS_TIMER_STRIDE 0x40

typedef struct {
  uint32_t control;
  uint32_t status;
  uint32_t load;
  uint32_t reloadval;
  uint32_t step;
  uint32_t ier;
  uint32_t isr;
  uint32_t ipr;
  uint32_t iar;
  uint32_t wakeup_ack;
  uint64_t start_ns;
  uint32_t start_value;
  bool running;
} TimelsTimer;

static TimelsTimer timers[2];
static uint32_t itcr;
static uint32_t itop;

static uint64_t timels_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int timels_decode(uint64_t offset, unsigned *index, uint32_t *reg) {
  if ((offset & 3u) != 0) return -1;

  if (offset < TIMELS_TIMER_STRIDE) {
    *index = 0;
    *reg = (uint32_t)offset;
    return 0;
  }

  if (offset < TIMELS_TIMER_STRIDE * 2) {
    *index = 1;
    *reg = (uint32_t)(offset - TIMELS_TIMER_STRIDE);
    return 0;
  }

  return -1;
}

static uint64_t timels_ticks(uint64_t elapsed_ns) {
  return (elapsed_ns * TIMELS_FREQ_HZ) / 1000000000ULL;
}

static uint32_t timels_value(const TimelsTimer *timer) {
  uint64_t elapsed;
  uint64_t ticks;

  if (!timer->running) return timer->start_value;

  elapsed = timels_now_ns() - timer->start_ns;
  ticks = timels_ticks(elapsed);
  if (ticks >= timer->start_value) return 0;

  return timer->start_value - (uint32_t)ticks;
}

static void timels_refresh(TimelsTimer *timer) {
  uint64_t elapsed;
  uint64_t ticks;
  uint64_t now;

  if (!timer->running) return;

  now = timels_now_ns();
  elapsed = now - timer->start_ns;
  ticks = timels_ticks(elapsed);
  if (ticks < timer->start_value) return;

  timer->status |= 1u;
  timer->isr |= 1u;
  timer->ipr |= 1u;

  if ((timer->control & 2u) != 0) {
    timer->start_value = timer->reloadval;
    timer->start_ns = now;
  } else {
    timer->start_value = 0;
    timer->running = false;
  }
}

static uint64_t TIMELS_Read(Component *self, uint64_t offset, unsigned size) {
  TimelsTimer *timer;
  unsigned index;
  uint32_t reg;

  (void)size;

  if (offset == REG_TIMELS_ITCR_OFFSET) return itcr;
  if (offset == REG_TIMELS_ITOP_OFFSET) return itop;

  if (timels_decode(offset, &index, &reg) != 0) {
    component_log_unhandled(self, "read", offset);
    return 0;
  }

  timer = &timers[index];

  switch (reg) {
    case REG_TIMELS_TIMER_CONTROL_OFFSET:
      return timer->control;
    case REG_TIMELS_TIMER_STATUS_OFFSET:
      timels_refresh(timer);
      return timer->status;
    case REG_TIMELS_TIMER_LOAD_OFFSET:
      return timer->load;
    case REG_TIMELS_TIMER_RELOADVAL_OFFSET:
      return timer->reloadval;
    case REG_TIMELS_TIMER_VALUE_OFFSET:
      timels_refresh(timer);
      return timels_value(timer);
    case REG_TIMELS_TIMER_STEP_OFFSET:
      return timer->step;
    case REG_TIMELS_TIMER_IER_OFFSET:
      return timer->ier;
    case REG_TIMELS_TIMER_ISR_OFFSET:
      timels_refresh(timer);
      return timer->isr;
    case REG_TIMELS_TIMER_IPR_OFFSET:
      timels_refresh(timer);
      return timer->ipr;
    case REG_TIMELS_TIMER_IAR_OFFSET:
      return timer->iar;
    case REG_TIMELS_TIMER_WAKEUP_ACK_OFFSET:
      return timer->wakeup_ack;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void TIMELS_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  TimelsTimer *timer;
  unsigned index;
  uint32_t reg;
  uint32_t v = (uint32_t)value;

  if (offset == REG_TIMELS_ITCR_OFFSET) {
    itcr = v;
    LOG_VBS("TIMELS: wrote ITCR (size=%u) <- 0x%08x\n", size, itcr);
    return;
  }

  if (offset == REG_TIMELS_ITOP_OFFSET) {
    itop = v;
    LOG_VBS("TIMELS: wrote ITOP (size=%u) <- 0x%08x\n", size, itop);
    return;
  }

  if (timels_decode(offset, &index, &reg) != 0) {
    component_log_unhandled(self, "write", offset);
    return;
  }

  timer = &timers[index];

  switch (reg) {
    case REG_TIMELS_TIMER_CONTROL_OFFSET: {
      bool was_enabled = (timer->control & 1u) != 0;
      bool enabled = (v & 1u) != 0;

      timer->control = v;
      if (enabled && !was_enabled) {
        if (timer->start_value == 0) timer->start_value = timer->load;
        timer->start_ns = timels_now_ns();
        timer->running = true;
      } else if (!enabled && was_enabled) {
        timer->start_value = timels_value(timer);
        timer->running = false;
      }
      LOG_VBS("TIMELS: wrote TIMER%u CONTROL (size=%u) <- 0x%08x\n", index, size,
              timer->control);
      return;
    }
    case REG_TIMELS_TIMER_STATUS_OFFSET:
      timer->status &= ~v;
      LOG_VBS("TIMELS: wrote TIMER%u STATUS (size=%u) <- 0x%08x\n", index, size,
              timer->status);
      return;
    case REG_TIMELS_TIMER_LOAD_OFFSET:
      timer->load = v;
      timer->start_value = v;
      if (timer->running) timer->start_ns = timels_now_ns();
      LOG_VBS("TIMELS: wrote TIMER%u LOAD (size=%u) <- 0x%08x\n", index, size,
              timer->load);
      return;
    case REG_TIMELS_TIMER_RELOADVAL_OFFSET:
      timer->reloadval = v;
      LOG_VBS("TIMELS: wrote TIMER%u RELOADVAL (size=%u) <- 0x%08x\n", index, size,
              timer->reloadval);
      return;
    case REG_TIMELS_TIMER_VALUE_OFFSET:
      timer->start_value = v;
      if (timer->running) timer->start_ns = timels_now_ns();
      LOG_VBS("TIMELS: wrote TIMER%u VALUE (size=%u) <- 0x%08x\n", index, size, v);
      return;
    case REG_TIMELS_TIMER_STEP_OFFSET:
      timer->step = v;
      return;
    case REG_TIMELS_TIMER_IER_OFFSET:
      timer->ier = v;
      LOG_VBS("TIMELS: wrote TIMER%u IER (size=%u) <- 0x%08x\n", index, size,
              timer->ier);
      return;
    case REG_TIMELS_TIMER_ISR_OFFSET:
      timer->isr &= ~v;
      return;
    case REG_TIMELS_TIMER_IPR_OFFSET:
      timer->ipr &= ~v;
      return;
    case REG_TIMELS_TIMER_IAR_OFFSET:
      timer->iar = v;
      return;
    case REG_TIMELS_TIMER_WAKEUP_ACK_OFFSET:
      timer->wakeup_ack = v;
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

void TIMELS_Poll(void) {
  unsigned i;

  for (i = 0; i < 2; i++) {
    timels_refresh(&timers[i]);
    if ((timers[i].status & 1u) != 0 && (timers[i].ier & 1u) != 0) {
      M3_PendIrq(159u + i);
    }
  }
}

Component *TIMELS_Initialize(void) {
  unsigned i;

  for (i = 0; i < 2; i++) {
    timers[i].control = 0;
    timers[i].status = 0;
    timers[i].load = 0;
    timers[i].reloadval = 0xFFFFFFFFu;
    timers[i].step = 0;
    timers[i].ier = 0;
    timers[i].isr = 0;
    timers[i].ipr = 0;
    timers[i].iar = 0;
    timers[i].wakeup_ack = 0;
    timers[i].start_ns = 0;
    timers[i].start_value = 0;
    timers[i].running = false;
  }

  itcr = 0;
  itop = 0;

  return component_register("TIMELS", NULL, TIMELS_Read, TIMELS_Write);
}
