#ifndef H1B3C_PERIPHERALS_H
#define H1B3C_PERIPHERALS_H

#include <H1B3C/peripherals/m3.h>
#include <H1B3C/peripherals/fuses.h>
#include <H1B3C/peripherals/globalsec.h>
#include <H1B3C/peripherals/pmu.h>
#include <H1B3C/peripherals/pinmux.h>
#include <H1B3C/peripherals/uart.h>
#include <H1B3C/peripherals/trng.h>

#define DEFAULT_REG_LENGTH 0x10000

void initialize_peripherals(void);

#endif /* H1B3C_PERIPHERALS_H */
