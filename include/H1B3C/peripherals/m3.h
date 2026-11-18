#ifndef H1B3C_PERIPHERALS_M3_H
#define H1B3C_PERIPHERALS_M3_H

#include <common/peripherals/component.h>

#define REG_M3_BASE_ADDR 0xE0000000
#define REG_M3_DEMCR_OFFSET 0xEDFC
#define REG_M3_DWT_CTRL_OFFSET 0x1000

Component *M3_Initialize(void);

#endif /* H1B3C_PERIPHERALS_M3_H */
