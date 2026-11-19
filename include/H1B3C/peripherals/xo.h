#ifndef H1B3C_PERIPHERALS_XO_H
#define H1B3C_PERIPHERALS_XO_H

#include <common/peripherals/component.h>

#define REG_XO_BASE_ADDR 0x400B0000
#define REG_XO_OFFSET 0x0
#define REG_XO_COUNT 29

Component *XO_Initialize(void);

#endif /* H1B3C_PERIPHERALS_XO_H */
