#ifndef H1B3C_PERIPHERALS_XO_H
#define H1B3C_PERIPHERALS_XO_H

#include <common/peripherals/component.h>

#define REG_XO_BASE_ADDR 0x400B0000
#define REG_XO_CLK_JTR_SYNC_CONTENTS_OFFSET 0x1C

Component *XO_Initialize(void);

#endif /* H1B3C_PERIPHERALS_XO_H */
