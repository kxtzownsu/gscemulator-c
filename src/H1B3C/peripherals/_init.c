#include <H1B3C/peripherals.h>

#include <common/common.h>
#include <H1B3C/register_map.h>

void initialize_peripherals(void) {
  Component *test = Test_Initialize();
  Component *foobar = Foobar_Initialize();
  MmioRegion regions[] = {
    { .component = test, .base = REG_TEST_BASE_ADDR, .size = REG_TEST_SIZE },
    { .component = foobar, .base = REG_FOOBAR_BASE_ADDR, .size = REG_FOOBAR_SIZE },
  };

  if (test == NULL || foobar == NULL) {
    LOG_ERR("Failed to initialize peripherals\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (!component_map_mmio(ctx.uc, regions, sizeof(regions) / sizeof(regions[0]))) {
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
}
