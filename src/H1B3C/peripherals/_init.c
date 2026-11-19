#include <H1B3C/peripherals.h>

#include <common/common.h>

void initialize_peripherals(void) {
  Component *M3 = M3_Initialize();
  Component *eFuses = eFuses_Initialize();
  Component *GLOBALSEC = GLOBALSEC_Initialize();
  Component *PMU = PMU_Initialize();
  Component *PINMUX = PINMUX_Initialize();
  Component *UART0 = UART_Initialize(0);
  Component *UART1 = UART_Initialize(1);
  Component *UART2 = UART_Initialize(2);
  Component *TRNG = TRNG_Initialize();
  Component *SWDP = SWDP_Initialize();
  Component *XO = XO_Initialize();
  Component *GPIO0 = GPIO_Initialize(0);
  Component *GPIO1 = GPIO_Initialize(1);
  Component *KEYMGR = KEYMGR_Initialize();
  Component *FLASH = FLASH_Initialize();
  Component *USB = USB_Initialize();
  Component *RTC = RTC_Initialize();
  Component *TIMELS = TIMELS_Initialize();
  Component *WATCHDOG = WATCHDOG_Initialize();
  Component *RBOX = RBOX_Initialize();
  Component *CRYPTO = CRYPTO_Initialize();
  Component *RDD = RDD_Initialize();
  Component *SPI = SPI_Initialize();
  Component *SPS = SPS_Initialize();
  Component *I2C0 = I2C_Initialize(0);
  Component *I2C1 = I2C_Initialize(1);
  Component *I2CS = I2CS_Initialize();
  Component *TIMEHS0 = TIMEHS_Initialize(0);
  Component *TIMEHS1 = TIMEHS_Initialize(1);
  MmioRegion regions[] = {
    { .component = M3, .base = REG_M3_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = eFuses, .base = REG_FUSE_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = GLOBALSEC, .base = REG_GLOBALSEC_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = PMU, .base = REG_PMU_BASE_ADDR, .size = REG_PMU_LENGTH },
    { .component = PINMUX, .base = REG_PINMUX_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = UART0, .base = REG_UART0_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = UART1, .base = REG_UART1_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = UART2, .base = REG_UART2_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = TRNG, .base = REG_TRNG_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = SWDP, .base = REG_SWDP_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = XO, .base = REG_XO_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = GPIO0, .base = REG_GPIO0_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = GPIO1, .base = REG_GPIO1_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = KEYMGR, .base = REG_KEYMGR_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = FLASH, .base = REG_FLASH_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = USB, .base = REG_USB_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = RTC, .base = REG_RTC_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = TIMELS, .base = REG_TIMELS_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = WATCHDOG, .base = REG_WATCHDOG_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = RBOX, .base = REG_RBOX_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = CRYPTO, .base = REG_CRYPTO_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = RDD, .base = REG_RDD_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = SPI, .base = REG_SPI_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = SPS, .base = REG_SPS_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = I2C0, .base = REG_I2C0_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = I2C1, .base = REG_I2C1_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = I2CS, .base = REG_I2CS_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = TIMEHS0, .base = REG_TIMEHS0_BASE_ADDR, .size = DEFAULT_REG_LENGTH },
    { .component = TIMEHS1, .base = REG_TIMEHS1_BASE_ADDR, .size = DEFAULT_REG_LENGTH }
  };

  if (
      M3 == NULL ||
      eFuses == NULL ||
      GLOBALSEC == NULL ||
      PMU == NULL ||
      PINMUX == NULL ||
      UART0 == NULL ||
      UART1 == NULL ||
      UART2 == NULL ||
      TRNG == NULL ||
      SWDP == NULL ||
      XO == NULL ||
      GPIO0 == NULL ||
      GPIO1 == NULL ||
      KEYMGR == NULL ||
      FLASH == NULL ||
      USB == NULL ||
      RTC == NULL ||
      TIMELS == NULL ||
      WATCHDOG == NULL ||
      RBOX == NULL ||
      CRYPTO == NULL ||
      RDD == NULL ||
      SPI == NULL ||
      SPS == NULL ||
      I2C0 == NULL ||
      I2C1 == NULL ||
      I2CS == NULL ||
      TIMEHS0 == NULL ||
      TIMEHS1 == NULL
    ) {
    LOG_ERR("Failed to initialize peripherals\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (!component_map_mmio(ctx.uc, regions, sizeof(regions) / sizeof(regions[0]))) {
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
}
