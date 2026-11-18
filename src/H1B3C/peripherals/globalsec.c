#include <H1B3C/peripherals/globalsec.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(alert_control)

static uint64_t GLOBALSEC_Read(Component *self, uint64_t offset, unsigned size) {
  switch (offset) {
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      LOG_VBS("GLOBALSEC: read ALERT_CONTROL (size=%u) -> 0x%08x\n", size, alert_control);
      return read_alert_control(size);
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void GLOBALSEC_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  switch (offset) {
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      write_alert_control(size, (uint32_t)value);
      LOG_VBS("GLOBALSEC: wrote alert_control (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *GLOBALSEC_Initialize(void) {
  alert_control = 0;
  return component_register("GLOBALSEC", NULL, GLOBALSEC_Read, GLOBALSEC_Write);
}
