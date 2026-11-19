#include <H1B3C/peripherals.h>

#include <common/common.h>

void initialize_peripherals(void) {
  Component *M3 = M3_Initialize();
  Component *eFuses = eFuses_Initialize();
  Component *GLOBALSEC = GLOBALSEC_Initialize();
  Component *PMU = PMU_Initialize();
  Component *PINMUX = PINMUX_Initialize();
  Component *UART = UART_Initialize();
  Component *TRNG = TRNG_Initialize();
  Component *SWDP = SWDP_Initialize();
  MmioRegion regions[] = {
    { .component = M3, .base = REG_M3_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = eFuses, .base = REG_FUSE_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = GLOBALSEC, .base = REG_GLOBALSEC_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = PMU, .base = REG_PMU_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = PINMUX, .base = REG_PINMUX_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = UART, .base = REG_UART_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = TRNG, .base = REG_TRNG_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = SWDP, .base = REG_SWDP_BASE_ADDR, .size = DEFAULT_REG_LENGTH }
  };

  if (
      M3 == NULL ||
      eFuses == NULL ||
      GLOBALSEC == NULL ||
      PMU == NULL ||
      PINMUX == NULL ||
      UART == NULL ||
      TRNG == NULL ||
      SWDP == NULL
    ) {
    LOG_ERR("Failed to initialize peripherals\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (!component_map_mmio(ctx.uc, regions, sizeof(regions) / sizeof(regions[0]))) {
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
}
