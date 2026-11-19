#include <H1B3C/peripherals/spi.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(ctrl)
DEFINE_BASIC_REGISTER(xact)
DEFINE_BASIC_REGISTER(ictrl)
DEFINE_BASIC_REGISTER(istate)
DEFINE_BASIC_REGISTER(istate_clr)
DEFINE_BASIC_REGISTER(ovrd)
DEFINE_BASIC_REGISTER(val)
DEFINE_BASIC_REGISTER(itcr)
DEFINE_BASIC_REGISTER(itop)
DEFINE_BASIC_REGISTER(tx_data)
DEFINE_BASIC_REGISTER(rx_data)

static const BasicRegister spi_registers[] = {
  BASIC_REGISTER_ENTRY(REG_SPI_CTRL_OFFSET, ctrl),
  BASIC_REGISTER_ENTRY(REG_SPI_XACT_OFFSET, xact),
  BASIC_REGISTER_ENTRY(REG_SPI_ICTRL_OFFSET, ictrl),
  BASIC_REGISTER_ENTRY(REG_SPI_ISTATE_OFFSET, istate),
  BASIC_REGISTER_ENTRY(REG_SPI_ISTATE_CLR_OFFSET, istate_clr),
  BASIC_REGISTER_ENTRY(REG_SPI_OVRD_OFFSET, ovrd),
  BASIC_REGISTER_ENTRY(REG_SPI_VAL_OFFSET, val),
  BASIC_REGISTER_ENTRY(REG_SPI_ITCR_OFFSET, itcr),
  BASIC_REGISTER_ENTRY(REG_SPI_ITOP_OFFSET, itop),
  BASIC_REGISTER_ENTRY(REG_SPI_TX_DATA_OFFSET, tx_data),
  BASIC_REGISTER_ENTRY(REG_SPI_RX_DATA_OFFSET, rx_data),
};

static uint64_t SPI_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(spi_registers, ARRAY_LEN(spi_registers), offset);

  if (reg != NULL) {
    LOG_VBS("SPI: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void SPI_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(spi_registers, ARRAY_LEN(spi_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("SPI: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *SPI_Initialize(void) {
  ctrl = 0;
  xact = 0;
  ictrl = 0;
  istate = 0;
  istate_clr = 0;
  ovrd = 0;
  val = 0;
  itcr = 0;
  itop = 0;
  tx_data = 0;
  rx_data = 0;

  return component_register("SPI", NULL, SPI_Read, SPI_Write);
}
