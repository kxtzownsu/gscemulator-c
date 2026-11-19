#ifndef H1B3C_PERIPHERALS_SPS_H
#define H1B3C_PERIPHERALS_SPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/peripherals/component.h>

#define REG_SPS_BASE_ADDR 0x40510000
#define REG_SPS_CTRL_OFFSET 0x0
#define REG_SPS_DUMMY_WORD_OFFSET 0x4
#define REG_SPS_STATUS01_OFFSET 0x8
#define REG_SPS_STATUS23_OFFSET 0xC
#define REG_SPS_STATUS45_OFFSET 0x10
#define REG_SPS_STATUS67_OFFSET 0x14
#define REG_SPS_CTRL01_OFFSET 0x18
#define REG_SPS_CTRL23_OFFSET 0x1C
#define REG_SPS_CTRL45_OFFSET 0x20
#define REG_SPS_CTRL67_OFFSET 0x24
#define REG_SPS_FIFO_CTRL_OFFSET 0x28
#define REG_SPS_TXFIFO_SIZE_OFFSET 0x2C
#define REG_SPS_TXFIFO_RPTR_OFFSET 0x30
#define REG_SPS_TXFIFO_WPTR_OFFSET 0x34
#define REG_SPS_TXFIFO_THRESHOLD_OFFSET 0x38
#define REG_SPS_RXFIFO_SIZE_OFFSET 0x3C
#define REG_SPS_RXFIFO_RPTR_OFFSET 0x40
#define REG_SPS_RXFIFO_WPTR_OFFSET 0x44
#define REG_SPS_RXFIFO_THRESHOLD_OFFSET 0x48
#define REG_SPS_OVRD_OFFSET 0x4C
#define REG_SPS_VAL_OFFSET 0x50
#define REG_SPS_ISTATE_OFFSET 0x54
#define REG_SPS_ISTATE_CLR_OFFSET 0x58
#define REG_SPS_ITCR_OFFSET 0x5C
#define REG_SPS_ITOP_OFFSET 0x60
#define REG_SPS_ICTRL_OFFSET 0x64
#define REG_SPS_TXFIFO_OFFSET 0x1000u
#define REG_SPS_FIFO_SIZE 0x400u
#define REG_SPS_RXFIFO_OFFSET (REG_SPS_TXFIFO_OFFSET + REG_SPS_FIFO_SIZE)
#define REG_SPS_FIFO_END_OFFSET (REG_SPS_RXFIFO_OFFSET + REG_SPS_FIFO_SIZE)

_Static_assert(REG_SPS_RXFIFO_OFFSET == REG_SPS_TXFIFO_OFFSET + REG_SPS_FIFO_SIZE,
               "SPS TX/RX FIFOs must be adjacent");
_Static_assert(REG_SPS_FIFO_END_OFFSET <= 0x10000, "SPS FIFOs must fit in the MMIO region");

Component *SPS_Initialize(void);

bool SPS_HostSetChipSelect(bool asserted);
bool SPS_HostTransfer(const uint8_t *input, uint8_t *output, size_t len);
bool SPS_HostWaitIdle(uint32_t timeout_ms);
bool SPS_HostWaitDeassert(uint32_t timeout_ms);

#endif /* H1B3C_PERIPHERALS_SPS_H */
