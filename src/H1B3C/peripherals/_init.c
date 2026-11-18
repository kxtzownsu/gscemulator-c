#include <H1B3C/peripherals.h>

#include <common/common.h>

void initialize_peripherals(void) {
  Component *m3 = M3_Initialize();
  MmioRegion regions[] = {
    { .component = m3, .base = REG_M3_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
  };

  if (m3 == NULL) {
    LOG_ERR("Failed to initialize peripherals\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (!component_map_mmio(ctx.uc, regions, sizeof(regions) / sizeof(regions[0]))) {
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
}
