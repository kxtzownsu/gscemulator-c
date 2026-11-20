#include <H1B3C/peripherals/trng.h>

#include <stdint.h>
#include <sys/random.h>
#include <sys/types.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(secure_post_processing_ctrl)
DEFINE_BASIC_REGISTER(post_processing_ctrl)
DEFINE_BASIC_REGISTER(go_event)
DEFINE_BASIC_REGISTER(timeout_counter)
DEFINE_BASIC_REGISTER(timeout_max_try_num)
DEFINE_BASIC_REGISTER(output_time_counter)
DEFINE_BASIC_REGISTER(stop_work)
DEFINE_BASIC_REGISTER(fsm_state)
DEFINE_BASIC_REGISTER(allowed_values)
DEFINE_BASIC_REGISTER(slice_max_upper_limit)
DEFINE_BASIC_REGISTER(slice_min_lower_limit)
DEFINE_BASIC_REGISTER(ldo_ctrl)
DEFINE_BASIC_REGISTER(power_down_b)
DEFINE_BASIC_REGISTER(analog_ctrl)
DEFINE_BASIC_REGISTER(empty)
DEFINE_BASIC_REGISTER(int_enable)
DEFINE_BASIC_REGISTER(int_state)
DEFINE_BASIC_REGISTER(int_test)
DEFINE_BASIC_REGISTER(timer_counter)
DEFINE_BASIC_REGISTER(max_value)
DEFINE_BASIC_REGISTER(min_value)
DEFINE_BASIC_REGISTER(proc_lock_power_down_b)
DEFINE_BASIC_REGISTER(antest)
DEFINE_BASIC_REGISTER(analog_sen_lsr_input)
DEFINE_BASIC_REGISTER(analog_sen_lsr_output)
DEFINE_BASIC_REGISTER(analog_test)
DEFINE_BASIC_REGISTER(one_shot_mode)
DEFINE_BASIC_REGISTER(one_shot_reg)
DEFINE_BASIC_REGISTER(frequency_calls)
DEFINE_BASIC_REGISTER(cur_num_ones)

static const BasicRegister trng_registers[] = {
  BASIC_REGISTER_ENTRY(REG_TRNG_SECURE_POST_PROCESSING_CTRL_OFFSET, secure_post_processing_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_POST_PROCESSING_CTRL_OFFSET, post_processing_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_GO_EVENT_OFFSET, go_event),
  BASIC_REGISTER_ENTRY(REG_TRNG_TIMEOUT_COUNTER_OFFSET, timeout_counter),
  BASIC_REGISTER_ENTRY(REG_TRNG_TIMEOUT_MAX_TRY_NUM_OFFSET, timeout_max_try_num),
  BASIC_REGISTER_ENTRY(REG_TRNG_OUTPUT_TIME_COUNTER_OFFSET, output_time_counter),
  BASIC_REGISTER_ENTRY(REG_TRNG_STOP_WORK_OFFSET, stop_work),
  BASIC_REGISTER_ENTRY(REG_TRNG_FSM_STATE_OFFSET, fsm_state),
  BASIC_REGISTER_ENTRY(REG_TRNG_ALLOWED_VALUES_OFFSET, allowed_values),
  BASIC_REGISTER_ENTRY(REG_TRNG_SLICE_MAX_UPPER_LIMIT_OFFSET, slice_max_upper_limit),
  BASIC_REGISTER_ENTRY(REG_TRNG_SLICE_MIN_LOWER_LIMIT_OFFSET, slice_min_lower_limit),
  BASIC_REGISTER_ENTRY(REG_TRNG_LDO_CTRL_OFFSET, ldo_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_POWER_DOWN_B_OFFSET, power_down_b),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANALOG_CTRL_OFFSET, analog_ctrl),
  BASIC_REGISTER_ENTRY(REG_TRNG_EMPTY_OFFSET, empty),
  BASIC_REGISTER_ENTRY(REG_TRNG_INT_ENABLE_OFFSET, int_enable),
  BASIC_REGISTER_ENTRY(REG_TRNG_INT_STATE_OFFSET, int_state),
  BASIC_REGISTER_ENTRY(REG_TRNG_INT_TEST_OFFSET, int_test),
  BASIC_REGISTER_ENTRY(REG_TRNG_TIMER_COUNTER_OFFSET, timer_counter),
  BASIC_REGISTER_ENTRY(REG_TRNG_MAX_VALUE_OFFSET, max_value),
  BASIC_REGISTER_ENTRY(REG_TRNG_MIN_VALUE_OFFSET, min_value),
  BASIC_REGISTER_ENTRY(REG_TRNG_PROC_LOCK_POWER_DOWN_B_OFFSET, proc_lock_power_down_b),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANTEST_OFFSET, antest),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANALOG_SEN_LSR_INPUT_OFFSET, analog_sen_lsr_input),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANALOG_SEN_LSR_OUTPUT_OFFSET, analog_sen_lsr_output),
  BASIC_REGISTER_ENTRY(REG_TRNG_ANALOG_TEST_OFFSET, analog_test),
  BASIC_REGISTER_ENTRY(REG_TRNG_ONE_SHOT_MODE_OFFSET, one_shot_mode),
  BASIC_REGISTER_ENTRY(REG_TRNG_ONE_SHOT_REG_OFFSET, one_shot_reg),
  BASIC_REGISTER_ENTRY(REG_TRNG_FREQUENCY_CALLS_OFFSET, frequency_calls),
  BASIC_REGISTER_ENTRY(REG_TRNG_CUR_NUM_ONES_OFFSET, cur_num_ones),
};

static uint32_t trng_random(void) {
  uint32_t value = 0;
  ssize_t received = getrandom(&value, sizeof(value), 0);

  if (received != (ssize_t)sizeof(value)) {
    LOG_ERR("TRNG: getrandom failed\n");
    return 0;
  }

  return value;
}

static uint64_t TRNG_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(trng_registers, ARRAY_LEN(trng_registers), offset);

  if (offset == REG_TRNG_VERSION_OFFSET) return 0x1014125;

  if (offset == REG_TRNG_READ_DATA_OFFSET) {
    uint32_t value = trng_random();

    LOG_VBS("TRNG: read READ_DATA (size=%u) -> 0x%08x\n", size, value);
    return value;
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

  if (offset == REG_TRNG_VERSION_OFFSET || offset == REG_TRNG_READ_DATA_OFFSET) {
    LOG_VBS("TRNG: ignored write to read-only register\n");
    return;
  }

  if (offset == REG_TRNG_INT_STATE_OFFSET) {
    int_state &= ~(uint32_t)value;
    return;
  }

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("TRNG: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *TRNG_Initialize(void) {
  secure_post_processing_ctrl = 0;
  post_processing_ctrl = 0;
  go_event = 0;
  timeout_counter = 0;
  timeout_max_try_num = 0;
  output_time_counter = 0;
  stop_work = 0;
  fsm_state = 0;
  allowed_values = 0;
  slice_max_upper_limit = 0;
  slice_min_lower_limit = 0;
  ldo_ctrl = 0;
  power_down_b = 0;
  analog_ctrl = 0;
  empty = 0;
  int_enable = 0;
  int_state = 0;
  int_test = 0;
  timer_counter = 0;
  max_value = 0;
  min_value = 0;
  proc_lock_power_down_b = 0;
  antest = 0;
  analog_sen_lsr_input = 0;
  analog_sen_lsr_output = 0;
  analog_test = 0;
  one_shot_mode = 0;
  one_shot_reg = 0;
  frequency_calls = 0;
  cur_num_ones = 0;

  return component_register_threaded("TRNG", NULL, TRNG_Read, TRNG_Write);
}
