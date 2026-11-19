#include <H1B3C/peripherals/rtc.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(ctrl)
DEFINE_BASIC_REGISTER(pinmux_en)
DEFINE_BASIC_REGISTER(pulse_stretch)
DEFINE_BASIC_REGISTER(sw_trim_en)
DEFINE_BASIC_REGISTER(sw_trim_counter)

static const BasicRegister rtc_registers[] = {
  BASIC_REGISTER_ENTRY(REG_RTC_CTRL_OFFSET, ctrl),
  BASIC_REGISTER_ENTRY(REG_RTC_PINMUX_EN_OFFSET, pinmux_en),
  BASIC_REGISTER_ENTRY(REG_RTC_PULSE_STRETCH_OFFSET, pulse_stretch),
  BASIC_REGISTER_ENTRY(REG_RTC_SW_TRIM_EN_OFFSET, sw_trim_en),
  BASIC_REGISTER_ENTRY(REG_RTC_SW_TRIM_COUNTER_OFFSET, sw_trim_counter),
};

static uint64_t RTC_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(rtc_registers, ARRAY_LEN(rtc_registers), offset);

  if (reg != NULL) {
    LOG_VBS("RTC: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void RTC_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(rtc_registers, ARRAY_LEN(rtc_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("RTC: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *RTC_Initialize(void) {
  ctrl = 0;
  pinmux_en = 0;
  pulse_stretch = 0;
  sw_trim_en = 0;
  sw_trim_counter = 0;

  return component_register("RTC", NULL, RTC_Read, RTC_Write);
}
