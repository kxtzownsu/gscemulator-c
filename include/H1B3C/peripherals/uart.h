#ifndef H1B3C_PERIPHERALS_UART_H
#define H1B3C_PERIPHERALS_UART_H

#include <common/peripherals/component.h>

#define REG_UART_BASE_ADDR 0x40600000
#define REG_UART_NCO_OFFSET 0x8
#define REG_UART_CTRL_OFFSET 0xC

Component *UART_Initialize(void);

#endif /* H1B3C_PERIPHERALS_UART_H */
