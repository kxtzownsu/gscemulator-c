#ifndef H1B3C_PERIPHERALS_UART_H
#define H1B3C_PERIPHERALS_UART_H

#include <common/peripherals/component.h>

#define REG_UART_BASE_ADDR 0x40600000
#define REG_UART_RDATA_OFFSET 0x0
#define REG_UART_WDATA_OFFSET 0x4
#define REG_UART_NCO_OFFSET 0x8
#define REG_UART_CTRL_OFFSET 0xC
#define REG_UART_STATE_OFFSET 0x14

Component *UART_Initialize(void);

#endif /* H1B3C_PERIPHERALS_UART_H */
