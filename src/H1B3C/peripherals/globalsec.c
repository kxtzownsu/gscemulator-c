#include <H1B3C/peripherals/globalsec.h>

#include <stddef.h>
#include <stdint.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

#define DUMMYKEY_COUNT 3

DEFINE_BASIC_REGISTER(alert_control)
DEFINE_BASIC_REGISTER(dbg_control)

static uint32_t dummykey[DUMMYKEY_COUNT];
static const uint32_t dummykey_offsets[DUMMYKEY_COUNT] = {
  REG_GLOBALSEC_DUMMYKEY0_OFFSET,
  REG_GLOBALSEC_DUMMYKEY1_OFFSET,
  REG_GLOBALSEC_DUMMYKEY2_OFFSET,
};

static int dummykey_get_index(uint64_t offset) {
  size_t i;

  for (i = 0; i < DUMMYKEY_COUNT; i++) {
    if (dummykey_offsets[i] == offset) return (int)i;
  }

  return -1;
}

static uint64_t GLOBALSEC_Read(Component *self, uint64_t offset, unsigned size) {
  int index = dummykey_get_index(offset);

  if (index >= 0) {
    LOG_VBS("GLOBALSEC: read DUMMYKEY%d (size=%u) -> 0x%08x\n", index, size, dummykey[index]);
    return dummykey[index];
  }

  switch (offset) {
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      LOG_VBS("GLOBALSEC: read ALERT_CONTROL (size=%u) -> 0x%08x\n", size, alert_control);
      return read_alert_control(size);
    case REG_GLOBALSEC_DBG_CONTROL_OFFSET:
      LOG_VBS("GLOBALSEC: read DBG_CONTROL (size=%u) -> 0x%08x\n", size, dbg_control);
      return read_dbg_control(size);
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void GLOBALSEC_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index = dummykey_get_index(offset);

  if (index >= 0) {
    dummykey[index] = (uint32_t)value;
    LOG_VBS("GLOBALSEC: wrote DUMMYKEY%d (size=%u) <- 0x%08x\n", index, size, dummykey[index]);
    return;
  }

  switch (offset) {
    case REG_GLOBALSEC_ALERT_CONTROL_OFFSET:
      write_alert_control(size, (uint32_t)value);
      LOG_VBS("GLOBALSEC: wrote alert_control (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    case REG_GLOBALSEC_DBG_CONTROL_OFFSET:
      write_dbg_control(size, (uint32_t)value);
      LOG_VBS("GLOBALSEC: wrote DBG_CONTROL (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *GLOBALSEC_Initialize(void) {
  alert_control = 0;
  dbg_control = 0;
  return component_register("GLOBALSEC", NULL, GLOBALSEC_Read, GLOBALSEC_Write);
}
