#include <H1B3C/peripherals/foobar.h>

#include <H1B3C/register_map.h>
#include <common/logger.h>

typedef struct {
  uint32_t bar;
} FoobarState;

static FoobarState foobar_state;

static uint64_t foobar_read(Component *self, uint64_t offset, unsigned size) {
  (void)size;

  switch (offset) {
    case REG_FOOBAR_FOO_OFFSET:
      LOG_VBS("FOOBAR: read FOO -> 0x%08x\n", REG_FOOBAR_FOO_VALUE);
      return REG_FOOBAR_FOO_VALUE;
    case REG_FOOBAR_BAR_OFFSET:
      LOG_ERR("FOOBAR: BAR is write-only\n");
      return 0;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void foobar_write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  FoobarState *state = self->state;

  (void)size;

  switch (offset) {
    case REG_FOOBAR_FOO_OFFSET:
      LOG_ERR("FOOBAR: FOO is read-only\n");
      return;
    case REG_FOOBAR_BAR_OFFSET:
      state->bar = (uint32_t)value;
      LOG_VBS("FOOBAR: wrote BAR (size=%u) <- 0x%08x\n", size, state->bar);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *Foobar_Initialize(void) {
  foobar_state.bar = 0;
  return component_register("FOOBAR", &foobar_state, foobar_read, foobar_write);
}
