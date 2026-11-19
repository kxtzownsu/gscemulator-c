#ifndef H1B3C_PERIPHERALS_GPIO_H
#define H1B3C_PERIPHERALS_GPIO_H

#include <common/peripherals/component.h>

#define REG_GPIO_BASE_ADDR 0x40200000
#define REG_GPIO_DATAIN_OFFSET 0x0

Component *GPIO_Initialize(void);

#endif /* H1B3C_PERIPHERALS_GPIO_H */
