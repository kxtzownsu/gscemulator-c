#include <H1B3C/peripherals/rdd.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(version)
DEFINE_BASIC_REGISTER(int_enable)
DEFINE_BASIC_REGISTER(int_state)
DEFINE_BASIC_REGISTER(int_test)
DEFINE_BASIC_REGISTER(power_down_b)
DEFINE_BASIC_REGISTER(antest)
DEFINE_BASIC_REGISTER(max_wait_time_counter)
DEFINE_BASIC_REGISTER(cur_wait_time_counter)
DEFINE_BASIC_REGISTER(ref_adj)
DEFINE_BASIC_REGISTER(input_pin_values)
DEFINE_BASIC_REGISTER(prog_debug_state_map)
DEFINE_BASIC_REGISTER(cur_stable_state)

static const BasicRegister rdd_registers[] = {
  BASIC_REGISTER_ENTRY(REG_RDD_VERSION_OFFSET, version),
  BASIC_REGISTER_ENTRY(REG_RDD_INT_ENABLE_OFFSET, int_enable),
  BASIC_REGISTER_ENTRY(REG_RDD_INT_STATE_OFFSET, int_state),
  BASIC_REGISTER_ENTRY(REG_RDD_INT_TEST_OFFSET, int_test),
  BASIC_REGISTER_ENTRY(REG_RDD_POWER_DOWN_B_OFFSET, power_down_b),
  BASIC_REGISTER_ENTRY(REG_RDD_ANTEST_OFFSET, antest),
  BASIC_REGISTER_ENTRY(REG_RDD_MAX_WAIT_TIME_COUNTER_OFFSET, max_wait_time_counter),
  BASIC_REGISTER_ENTRY(REG_RDD_CUR_WAIT_TIME_COUNTER_OFFSET, cur_wait_time_counter),
  BASIC_REGISTER_ENTRY(REG_RDD_REF_ADJ_OFFSET, ref_adj),
  BASIC_REGISTER_ENTRY(REG_RDD_INPUT_PIN_VALUES_OFFSET, input_pin_values),
  BASIC_REGISTER_ENTRY(REG_RDD_PROG_DEBUG_STATE_MAP_OFFSET, prog_debug_state_map),
  BASIC_REGISTER_ENTRY(REG_RDD_CUR_STABLE_STATE_OFFSET, cur_stable_state),
};

static uint64_t RDD_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(rdd_registers, ARRAY_LEN(rdd_registers), offset);

  if (reg != NULL) {
    LOG_VBS("RDD: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void RDD_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(rdd_registers, ARRAY_LEN(rdd_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("RDD: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *RDD_Initialize(void) {
  version = 0;
  int_enable = 0;
  int_state = 0;
  int_test = 0;
  power_down_b = 0;
  antest = 0;
  max_wait_time_counter = 0;
  cur_wait_time_counter = 0;
  ref_adj = 0;
  input_pin_values = 0;
  prog_debug_state_map = 0;
  cur_stable_state = 0;

  return component_register("RDD", NULL, RDD_Read, RDD_Write);
}
