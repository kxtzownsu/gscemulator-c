#include <H1B3C/peripherals/flash.h>

#include <stdint.h>

#include <common/logger.h>

#define FLASH_PE_EN_MAGIC 0xB11924E1u
#define FLASH_OP_READ 0x16021765u

static uint32_t trans_offset;
static uint32_t trans_mainb;
static uint32_t trans_size;
static uint32_t pe_en;
static uint32_t opcode;
static int pe_control = -1;
static uint32_t error;
static uint32_t dout_val[2];
static uint32_t protect_info1_erase;
static uint32_t wr_data[REG_FLASH_WR_DATA_COUNT];

static int wr_data_index(uint64_t offset) {
  uint64_t end =
      REG_FLASH_WR_DATA0_OFFSET + (uint64_t)REG_FLASH_WR_DATA_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_FLASH_WR_DATA0_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_FLASH_WR_DATA0_OFFSET) / sizeof(uint32_t));
}

static uint64_t FLASH_Read(Component *self, uint64_t offset, unsigned size) {
  int index = wr_data_index(offset);

  (void)size;

  if (index >= 0) return wr_data[index];

  switch (offset) {
    case REG_FLASH_PE_CONTROL0_OFFSET:
      return pe_control == 0 ? opcode : 0;
    case REG_FLASH_PE_CONTROL1_OFFSET:
      return pe_control == 1 ? opcode : 0;
    case REG_FLASH_TRANS_OFFSET:
      return trans_offset | (trans_mainb << 16) | (trans_size << 17);
    case REG_FLASH_PROTECT_INFO1_ERASE_OFFSET:
      return protect_info1_erase;
    case REG_FLASH_DOUT_VAL0_OFFSET:
      return dout_val[0];
    case REG_FLASH_DOUT_VAL1_OFFSET:
      return dout_val[1];
    case REG_FLASH_PE_EN_OFFSET:
      return pe_en;
    case REG_FLASH_ERROR_OFFSET:
      return error;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void FLASH_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  int index = wr_data_index(offset);

  (void)size;

  if (index >= 0) {
    wr_data[index] = (uint32_t)value;
    return;
  }

  switch (offset) {
    case REG_FLASH_PE_CONTROL0_OFFSET:
    case REG_FLASH_PE_CONTROL1_OFFSET:
      if (pe_en != FLASH_PE_EN_MAGIC) return;
      opcode = (uint32_t)value;
      pe_control = offset == REG_FLASH_PE_CONTROL0_OFFSET ? 0 : 1;
      pe_en = 0;

      if (opcode == FLASH_OP_READ && pe_control == 1) {
        dout_val[1] = 0xFFFFFFFFu;
        error = 0;
      }

      opcode = 0;
      pe_control = -1;
      return;
    case REG_FLASH_TRANS_OFFSET:
      trans_offset = (uint32_t)value & 0xFFFFu;
      trans_mainb = ((uint32_t)value >> 16) & 1u;
      trans_size = ((uint32_t)value >> 17) & 0x1Fu;
      return;
    case REG_FLASH_PROTECT_INFO1_ERASE_OFFSET:
      protect_info1_erase = (uint32_t)value & 1u;
      return;
    case REG_FLASH_DOUT_VAL0_OFFSET:
    case REG_FLASH_DOUT_VAL1_OFFSET:
      return;
    case REG_FLASH_PE_EN_OFFSET:
      pe_en = (uint32_t)value;
      return;
    case REG_FLASH_ERROR_OFFSET:
      return;
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *FLASH_Initialize(void) {
  trans_offset = 0;
  trans_mainb = 0;
  trans_size = 0;
  pe_en = 0;
  opcode = 0;
  pe_control = -1;
  error = 0;
  dout_val[0] = 0;
  dout_val[1] = 0;
  protect_info1_erase = 0;

  for (unsigned i = 0; i < REG_FLASH_WR_DATA_COUNT; i++) wr_data[i] = 0;

  return component_register("FLASH", NULL, FLASH_Read, FLASH_Write);
}
