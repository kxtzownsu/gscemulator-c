#include <H1B3C/peripherals/trng.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(post_processing_ctrl)
DEFINE_BASIC_REGISTER(go_event)
DEFINE_BASIC_REGISTER(output_time_counter)
DEFINE_BASIC_REGISTER(slice_max_upper_limit)
DEFINE_BASIC_REGISTER(slice_min_lower_limit)
DEFINE_BASIC_REGISTER(ldo_ctrl)
DEFINE_BASIC_REGISTER(power_down_b)
DEFINE_BASIC_REGISTER(analog_ctrl)

static const BasicRegister trng_registers[] = {
  BASIC_REGISTER_ENTRY(REG_TRNG_POST_PROCESSING_CTRL_OFFSET, post_processing_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_GO_EVENT_OFFSET, go_event),
  BASIC_REGISTER_ENTRY(REG_TRNG_OUTPUT_TIME_COUNTER_OFFSET, output_time_counter),
  BASIC_REGISTER_ENTRY(REG_TRNG_SLICE_MAX_UPPER_LIMIT_OFFSET, slice_max_upper_limit),
  BASIC_REGISTER_ENTRY(REG_TRNG_SLICE_MIN_LOWER_LIMIT_OFFSET, slice_min_lower_limit),
  BASIC_REGISTER_ENTRY(REG_TRNG_LDO_CTRL_OFFSET, ldo_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_POWER_DOWN_B_OFFSET, power_down_b),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANALOG_CTRL_OFFSET, analog_ctrl),
};

static uint64_t TRNG_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(trng_registers, ARRAY_LEN(trng_registers), offset);

  if (offset == REG_TRNG_READ_DATA_OFFSET) {
    LOG_VBS("TRNG: read READ_DATA (size=%u) -> 0\n", size);
    return 0;
  }

  if (reg != NULL) {
    LOG_VBS("TRNG: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void TRNG_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(trng_registers, ARRAY_LEN(trng_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("TRNG: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *TRNG_Initialize(void) {
  post_processing_ctrl = 0;
  go_event = 0;
  output_time_counter = 0;
  slice_max_upper_limit = 0;
  slice_min_lower_limit = 0;
  ldo_ctrl = 0;
  power_down_b = 0;
  analog_ctrl = 0;

  return component_register_threaded("TRNG", NULL, TRNG_Read, TRNG_Write);
}
