#include <H1B3C/peripherals/watchdog.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(wdogload)
DEFINE_BASIC_REGISTER(wdogvalue)
DEFINE_BASIC_REGISTER(wdogcontrol)
DEFINE_BASIC_REGISTER(wdogintclr)
DEFINE_BASIC_REGISTER(wdogris)
DEFINE_BASIC_REGISTER(wdogmis)
DEFINE_BASIC_REGISTER(wdoglock)
DEFINE_BASIC_REGISTER(wdogitcr)
DEFINE_BASIC_REGISTER(wdogitop)
DEFINE_BASIC_REGISTER(wdogperiphid4)
DEFINE_BASIC_REGISTER(wdogperiphid5)
DEFINE_BASIC_REGISTER(wdogperiphid6)
DEFINE_BASIC_REGISTER(wdogperiphid7)
DEFINE_BASIC_REGISTER(wdogperiphid0)
DEFINE_BASIC_REGISTER(wdogperiphid1)
DEFINE_BASIC_REGISTER(wdogperiphid2)
DEFINE_BASIC_REGISTER(wdogperiphid3)
DEFINE_BASIC_REGISTER(wdogpcellid0)
DEFINE_BASIC_REGISTER(wdogpcellid1)
DEFINE_BASIC_REGISTER(wdogpcellid2)
DEFINE_BASIC_REGISTER(wdogpcellid3)

static const BasicRegister watchdog_registers[] = {
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGLOAD_OFFSET, wdogload),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGVALUE_OFFSET, wdogvalue),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGCONTROL_OFFSET, wdogcontrol),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGINTCLR_OFFSET, wdogintclr),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGRIS_OFFSET, wdogris),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGMIS_OFFSET, wdogmis),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGLOCK_OFFSET, wdoglock),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGITCR_OFFSET, wdogitcr),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGITOP_OFFSET, wdogitop),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID4_OFFSET, wdogperiphid4),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID5_OFFSET, wdogperiphid5),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID6_OFFSET, wdogperiphid6),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID7_OFFSET, wdogperiphid7),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID0_OFFSET, wdogperiphid0),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID1_OFFSET, wdogperiphid1),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID2_OFFSET, wdogperiphid2),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPERIPHID3_OFFSET, wdogperiphid3),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPCELLID0_OFFSET, wdogpcellid0),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPCELLID1_OFFSET, wdogpcellid1),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPCELLID2_OFFSET, wdogpcellid2),
  BASIC_REGISTER_ENTRY(REG_WATCHDOG_WDOGPCELLID3_OFFSET, wdogpcellid3),
};

static uint64_t WATCHDOG_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg =
      basic_register_find(watchdog_registers, ARRAY_LEN(watchdog_registers), offset);

  if (reg != NULL) {
    LOG_VBS("WATCHDOG: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void WATCHDOG_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg =
      basic_register_find(watchdog_registers, ARRAY_LEN(watchdog_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("WATCHDOG: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *WATCHDOG_Initialize(void) {
  wdogload = 0;
  wdogvalue = 0;
  wdogcontrol = 0;
  wdogintclr = 0;
  wdogris = 0;
  wdogmis = 0;
  wdoglock = 0;
  wdogitcr = 0;
  wdogitop = 0;
  wdogperiphid4 = 0;
  wdogperiphid5 = 0;
  wdogperiphid6 = 0;
  wdogperiphid7 = 0;
  wdogperiphid0 = 0;
  wdogperiphid1 = 0;
  wdogperiphid2 = 0;
  wdogperiphid3 = 0;
  wdogpcellid0 = 0;
  wdogpcellid1 = 0;
  wdogpcellid2 = 0;
  wdogpcellid3 = 0;

  return component_register("WATCHDOG", NULL, WATCHDOG_Read, WATCHDOG_Write);
}
