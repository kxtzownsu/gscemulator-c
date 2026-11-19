#include <H1B3C/peripherals/sps.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(ctrl)
DEFINE_BASIC_REGISTER(dummy_word)
DEFINE_BASIC_REGISTER(status01)
DEFINE_BASIC_REGISTER(status23)
DEFINE_BASIC_REGISTER(status45)
DEFINE_BASIC_REGISTER(status67)
DEFINE_BASIC_REGISTER(ctrl01)
DEFINE_BASIC_REGISTER(ctrl23)
DEFINE_BASIC_REGISTER(ctrl45)
DEFINE_BASIC_REGISTER(ctrl67)
DEFINE_BASIC_REGISTER(fifo_ctrl)
DEFINE_BASIC_REGISTER(txfifo_size)
DEFINE_BASIC_REGISTER(txfifo_rptr)
DEFINE_BASIC_REGISTER(txfifo_wptr)
DEFINE_BASIC_REGISTER(txfifo_threshold)
DEFINE_BASIC_REGISTER(rxfifo_size)
DEFINE_BASIC_REGISTER(rxfifo_rptr)
DEFINE_BASIC_REGISTER(rxfifo_wptr)
DEFINE_BASIC_REGISTER(rxfifo_threshold)
DEFINE_BASIC_REGISTER(ovrd)
DEFINE_BASIC_REGISTER(val)
DEFINE_BASIC_REGISTER(istate)
DEFINE_BASIC_REGISTER(istate_clr)
DEFINE_BASIC_REGISTER(itcr)
DEFINE_BASIC_REGISTER(itop)
DEFINE_BASIC_REGISTER(ictrl)

static const BasicRegister sps_registers[] = {
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL_OFFSET, ctrl),
  BASIC_REGISTER_ENTRY(REG_SPS_DUMMY_WORD_OFFSET, dummy_word),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS01_OFFSET, status01),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS23_OFFSET, status23),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS45_OFFSET, status45),
  BASIC_REGISTER_ENTRY(REG_SPS_STATUS67_OFFSET, status67),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL01_OFFSET, ctrl01),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL23_OFFSET, ctrl23),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL45_OFFSET, ctrl45),
  BASIC_REGISTER_ENTRY(REG_SPS_CTRL67_OFFSET, ctrl67),
  BASIC_REGISTER_ENTRY(REG_SPS_FIFO_CTRL_OFFSET, fifo_ctrl),
  BASIC_REGISTER_ENTRY(REG_SPS_TXFIFO_SIZE_OFFSET, txfifo_size),
  BASIC_REGISTER_ENTRY(REG_SPS_TXFIFO_RPTR_OFFSET, txfifo_rptr),
  BASIC_REGISTER_ENTRY(REG_SPS_TXFIFO_WPTR_OFFSET, txfifo_wptr),
  BASIC_REGISTER_ENTRY(REG_SPS_TXFIFO_THRESHOLD_OFFSET, txfifo_threshold),
  BASIC_REGISTER_ENTRY(REG_SPS_RXFIFO_SIZE_OFFSET, rxfifo_size),
  BASIC_REGISTER_ENTRY(REG_SPS_RXFIFO_RPTR_OFFSET, rxfifo_rptr),
  BASIC_REGISTER_ENTRY(REG_SPS_RXFIFO_WPTR_OFFSET, rxfifo_wptr),
  BASIC_REGISTER_ENTRY(REG_SPS_RXFIFO_THRESHOLD_OFFSET, rxfifo_threshold),
  BASIC_REGISTER_ENTRY(REG_SPS_OVRD_OFFSET, ovrd),
  BASIC_REGISTER_ENTRY(REG_SPS_VAL_OFFSET, val),
  BASIC_REGISTER_ENTRY(REG_SPS_ISTATE_OFFSET, istate),
  BASIC_REGISTER_ENTRY(REG_SPS_ISTATE_CLR_OFFSET, istate_clr),
  BASIC_REGISTER_ENTRY(REG_SPS_ITCR_OFFSET, itcr),
  BASIC_REGISTER_ENTRY(REG_SPS_ITOP_OFFSET, itop),
  BASIC_REGISTER_ENTRY(REG_SPS_ICTRL_OFFSET, ictrl),
};

static uint64_t SPS_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(sps_registers, ARRAY_LEN(sps_registers), offset);

  if (reg != NULL) {
    LOG_VBS("SPS: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void SPS_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(sps_registers, ARRAY_LEN(sps_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("SPS: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *SPS_Initialize(void) {
  ctrl = 0;
  dummy_word = 0;
  status01 = 0;
  status23 = 0;
  status45 = 0;
  status67 = 0;
  ctrl01 = 0;
  ctrl23 = 0;
  ctrl45 = 0;
  ctrl67 = 0;
  fifo_ctrl = 0;
  txfifo_size = 0;
  txfifo_rptr = 0;
  txfifo_wptr = 0;
  txfifo_threshold = 0;
  rxfifo_size = 0;
  rxfifo_rptr = 0;
  rxfifo_wptr = 0;
  rxfifo_threshold = 0;
  ovrd = 0;
  val = 0;
  istate = 0;
  istate_clr = 0;
  itcr = 0;
  itop = 0;
  ictrl = 0;

  return component_register("SPS", NULL, SPS_Read, SPS_Write);
}
