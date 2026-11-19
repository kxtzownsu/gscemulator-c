#ifndef H1B3C_PERIPHERALS_RTC_H
#define H1B3C_PERIPHERALS_RTC_H

#include <common/peripherals/component.h>

#define REG_RTC_BASE_ADDR 0x400A0000
#define REG_RTC_CTRL_OFFSET 0x0
#define REG_RTC_PINMUX_EN_OFFSET 0x4
#define REG_RTC_PULSE_STRETCH_OFFSET 0x8
#define REG_RTC_SW_TRIM_EN_OFFSET 0xC
#define REG_RTC_SW_TRIM_COUNTER_OFFSET 0x10

Component *RTC_Initialize(void);

#endif /* H1B3C_PERIPHERALS_RTC_H */
