#ifndef H1B3C_PERIPHERALS_H
#define H1B3C_PERIPHERALS_H

#include <H1B3C/peripherals/m3.h>
#include <H1B3C/peripherals/fuses.h>
#include <H1B3C/peripherals/globalsec.h>
#include <H1B3C/peripherals/pmu.h>
#include <H1B3C/peripherals/pinmux.h>
#include <H1B3C/peripherals/uart.h>
#include <H1B3C/peripherals/trng.h>
#include <H1B3C/peripherals/swdp.h>
#include <H1B3C/peripherals/xo.h>
#include <H1B3C/peripherals/gpio.h>
#include <H1B3C/peripherals/keymgr.h>
#include <H1B3C/peripherals/flash.h>
#include <H1B3C/peripherals/usb.h>
#include <H1B3C/peripherals/rtc.h>
#include <H1B3C/peripherals/timels.h>
#include <H1B3C/peripherals/watchdog.h>
#include <H1B3C/peripherals/rbox.h>
#include <H1B3C/peripherals/crypto.h>
#include <H1B3C/peripherals/rdd.h>
#include <H1B3C/peripherals/spi.h>

#define DEFAULT_REG_LENGTH 0x10000

void initialize_peripherals(void);

#endif /* H1B3C_PERIPHERALS_H */
