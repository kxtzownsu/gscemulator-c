#ifndef H1B3C_PERIPHERALS_RBOX_H
#define H1B3C_PERIPHERALS_RBOX_H

#include <common/peripherals/component.h>

#define REG_RBOX_BASE_ADDR 0x40550000
#define REG_RBOX_OFFSET 0x0
#define REG_RBOX_COUNT 41
#define REG_RBOX_INT_STATE_OFFSET 0x4
#define REG_RBOX_ASSERT_EC_RST_OFFSET 0x10

Component *RBOX_Initialize(void);

#endif /* H1B3C_PERIPHERALS_RBOX_H */
