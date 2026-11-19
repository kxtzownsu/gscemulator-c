#include <H1B3C/peripherals/gpio.h>

#include <common/logger.h>

static uint64_t GPIO_Read(Component *self, uint64_t offset, unsigned size) {
  switch (offset) {
    case REG_GPIO_DATAIN_OFFSET:
      LOG_VBS("GPIO: read DATAIN (size=%u) -> 0\n", size);
      return 0;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void GPIO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  (void)size;
  (void)value;

  component_log_unhandled(self, "write", offset);
}

Component *GPIO_Initialize(void) {
  return component_register("GPIO", NULL, GPIO_Read, GPIO_Write);
}
