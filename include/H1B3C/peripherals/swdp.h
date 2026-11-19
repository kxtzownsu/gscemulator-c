#ifndef H1B3C_PERIPHERALS_SWDP_H
#define H1B3C_PERIPHERALS_SWDP_H

#include <common/peripherals/component.h>

#define REG_SWDP_BASE_ADDR 0x40520000
#define REG_SWDP_P4_LAST_SYNC_OFFSET 0x2C
#define REG_SWDP_BUILD_DATE_OFFSET 0x30
#define REG_SWDP_BUILD_TIME_OFFSET 0x34

Component *SWDP_Initialize(void);

#endif /* H1B3C_PERIPHERALS_SWDP_H */
