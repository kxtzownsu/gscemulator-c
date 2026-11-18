#include <H1B3C/peripherals/test.h>

#include <H1B3C/register_map.h>
#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(testreg)

static uint64_t test_read(Component *self, uint64_t offset, unsigned size) {
  switch (offset) {
    case REG_TEST_TESTREG_OFFSET:
      LOG_VBS("TEST: read TESTREG (size=%u) -> 0x%08x\n", size, testreg);
      return read_testreg(size);
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void test_write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  switch (offset) {
    case REG_TEST_TESTREG_OFFSET:
      write_testreg(size, (uint32_t)value);
      LOG_VBS("TEST: wrote TESTREG (size=%u) <- 0x%08x\n", size, (uint32_t)value);
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *Test_Initialize(void) {
  testreg = 0;
  return component_register("TEST", NULL, test_read, test_write);
}
