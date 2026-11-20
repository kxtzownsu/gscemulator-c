#define _POSIX_C_SOURCE 200809L

#include <H1B3C/peripherals/flash.h>
#include <H1B3C/memory.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <unicorn/unicorn.h>

#include <common/ctx.h>
#include <common/logger.h>

#define FLASH_PE_EN_MAGIC 0xB11924E1u
#define FLASH_OP_ERASE 0x31415927u
#define FLASH_OP_WRITE 0x27182818u
#define FLASH_OP_READ 0x16021765u
#define FLASH_OP_BULK_ERASE 0x1D1E2BADu

#define FLASH_MAIN_BOUND 0x3FFFFu
#define FLASH_INFO_BOUND 0x7FFu
#define FLASH_PAGE_SIZE 0x800u
#define FLASH_ROW_WORDS 64u
#define FLASH_ERROR_HOLD_NS 5000000ULL

#define FLASH_ERR_PROG_NOT_ROW_ALIGNED (1u << 0)
#define FLASH_ERR_OUT_OF_MAIN_RANGE (1u << 1)
#define FLASH_ERR_OUT_OF_INFO_RANGE (1u << 2)
#define FLASH_ERR_ERASE_NOT_PAGE_ALIGNED (1u << 4)
#define FLASH_ERR_ERASE_FAILED (1u << 7)
#define FLASH_ERR_INFO1_ERASE_LOCKED (1u << 12)
#define FLASH_ERR_ACCESS_INVALID (1u << 13)

static const uint32_t flash_bases[2][2] = {
  { FLASH_PROG_BASE, FLASH_PROG_BASE + 0x40000u },
  { FLASH_INFO0_BASE, FLASH_INFO1_BASE },
};

static uint32_t trans_offset;
static uint32_t trans_mainb;
static uint32_t trans_size;
static uint32_t pe_en;
static uint32_t opcode;
static int pe_control = -1;
static uint32_t error;
static uint64_t error_ns;
static uint32_t dout_val[2];
static uint32_t protect_info1_erase;
static uint32_t prog_smart_algo;
static uint32_t erase_smart_algo;
static uint32_t bulkerase_smart_algo;
static uint32_t wr_data[REG_FLASH_WR_DATA_COUNT];
static uint32_t enable_info0_shadow_read;
static uint32_t fsh_ictrl;
static uint32_t fsh_istate;
static uint32_t ovrd0_unlock;
static uint32_t ovrd1_unlock;
static uint32_t ovrd_sigval_din;
static uint32_t ovrd_sigval_offset;
static uint32_t ovrd_sigval;
static uint32_t ovrd_sigen;
static uint32_t no_wait_on_bout_seq;
static uint32_t ovrd_sigval_tc1;

static uint64_t flash_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int wr_data_index(uint64_t offset) {
  uint64_t end =
      REG_FLASH_WR_DATA0_OFFSET + (uint64_t)REG_FLASH_WR_DATA_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_FLASH_WR_DATA0_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_FLASH_WR_DATA0_OFFSET) / sizeof(uint32_t));
}

static uint32_t flash_error_read(void) {
  if (error != 0 && flash_now_ns() - error_ns >= FLASH_ERROR_HOLD_NS) error = 0;

  return error;
}

static void flash_set_error(uint32_t bits) {
  error |= bits;
  error_ns = flash_now_ns();
  LOG_VBS("FLASH: error 0x%08x\n", error);
}

static uint32_t flash_range_error(void) {
  return trans_mainb == 0 ? FLASH_ERR_OUT_OF_MAIN_RANGE : FLASH_ERR_OUT_OF_INFO_RANGE;
}

static bool flash_bounds_ok(uint32_t offset, uint32_t bytes) {
  uint32_t bound = trans_mainb == 0 ? FLASH_MAIN_BOUND : FLASH_INFO_BOUND;

  return offset + bytes - 1u <= bound;
}

static void flash_erase(void) {
  uint32_t base = flash_bases[trans_mainb][pe_control];
  uint32_t addr = base + trans_offset * 4u;
  uint8_t erased[FLASH_PAGE_SIZE];

  if (protect_info1_erase != 0 && trans_mainb == 1 && pe_control == 1) {
    flash_set_error(FLASH_ERR_ERASE_FAILED | FLASH_ERR_INFO1_ERASE_LOCKED);
    return;
  }

  if ((trans_offset % (FLASH_PAGE_SIZE / 4u)) != 0) {
    flash_set_error(FLASH_ERR_ERASE_FAILED | FLASH_ERR_ERASE_NOT_PAGE_ALIGNED);
    return;
  }

  if (!flash_bounds_ok(trans_offset * 4u, FLASH_PAGE_SIZE)) {
    flash_set_error(flash_range_error());
    return;
  }

  memset(erased, 0xFF, sizeof(erased));
  if (uc_mem_write(ctx.uc, addr, erased, sizeof(erased)) != UC_ERR_OK) {
    flash_set_error(FLASH_ERR_ERASE_FAILED);
    return;
  }

  LOG_VBS("FLASH: erased 0x%08x..0x%08x\n", addr, addr + FLASH_PAGE_SIZE - 1u);
}

static void flash_write(void) {
  uint32_t base = flash_bases[trans_mainb][pe_control];
  uint32_t addr = base + trans_offset * 4u;
  uint32_t words = trans_size + 1u;
  uint32_t index;

  if (!flash_bounds_ok(trans_offset * 4u, words * 4u)) {
    flash_set_error(flash_range_error());
    return;
  }

  if (trans_offset / FLASH_ROW_WORDS != (trans_offset + trans_size) / FLASH_ROW_WORDS) {
    flash_set_error(FLASH_ERR_PROG_NOT_ROW_ALIGNED);
    return;
  }

  for (index = 0; index < words; index++) {
    uint32_t address = addr + index * 4u;
    uint32_t value = 0xFFFFFFFFu;

    if (uc_mem_read(ctx.uc, address, &value, sizeof(value)) != UC_ERR_OK) {
      flash_set_error(flash_range_error());
      return;
    }

    value &= wr_data[index];
    if (uc_mem_write(ctx.uc, address, &value, sizeof(value)) != UC_ERR_OK) {
      flash_set_error(flash_range_error());
      return;
    }
  }

  LOG_VBS("FLASH: programmed %u word(s) at 0x%08x\n", words, addr);
}

static void flash_read(void) {
  uint32_t base = flash_bases[trans_mainb][pe_control];
  uint32_t address = base + trans_offset * 4u;
  uint32_t value;

  if (trans_size != 1) {
    flash_set_error(FLASH_ERR_ACCESS_INVALID);
    return;
  }

  if (!flash_bounds_ok(trans_offset * 4u, 4u)) {
    flash_set_error(flash_range_error());
    return;
  }

  if (uc_mem_read(ctx.uc, address, &value, sizeof(value)) != UC_ERR_OK) {
    flash_set_error(flash_range_error());
    return;
  }

  dout_val[pe_control] = value;
  LOG_VBS("FLASH: read 0x%08x -> 0x%08x\n", address, value);
}

static void flash_bulk_erase(void) {
  uint32_t base = flash_bases[trans_mainb][pe_control];
  uint32_t size = trans_mainb == 0 ? FLASH_PROG_SIZE / 2u : FLASH_PAGE_SIZE;
  uint8_t erased[FLASH_PAGE_SIZE];
  uint32_t done = 0;

  if (trans_mainb == 1 && protect_info1_erase != 0) {
    flash_set_error(FLASH_ERR_ERASE_FAILED);
    return;
  }

  memset(erased, 0xFF, sizeof(erased));
  while (done < size) {
    uint32_t chunk = size - done;

    if (chunk > sizeof(erased)) chunk = sizeof(erased);
    if (uc_mem_write(ctx.uc, base + done, erased, chunk) != UC_ERR_OK) {
      flash_set_error(FLASH_ERR_ERASE_FAILED);
      return;
    }
    done += chunk;
  }

  LOG_VBS("FLASH: bulk erased 0x%08x..0x%08x\n", base, base + size - 1u);
}

static void flash_run_op(uint32_t control) {
  uint32_t op = opcode;

  opcode = 0;
  pe_control = (int)control;

  switch (op) {
    case FLASH_OP_ERASE:
      flash_erase();
      break;
    case FLASH_OP_WRITE:
      flash_write();
      break;
    case FLASH_OP_READ:
      flash_read();
      break;
    case FLASH_OP_BULK_ERASE:
      flash_bulk_erase();
      break;
    default:
      flash_set_error(FLASH_ERR_ACCESS_INVALID);
      break;
  }

  pe_control = -1;
}

static uint64_t FLASH_Read(Component *self, uint64_t offset, unsigned size) {
  int index = wr_data_index(offset);

  (void)size;

  if (index >= 0) return wr_data[index];

  switch (offset) {
    case REG_FLASH_PE_CONTROL0_OFFSET:
      return 0;
    case REG_FLASH_PE_CONTROL1_OFFSET:
      return 0;
    case REG_FLASH_TRANS_OFFSET:
      return trans_offset | (trans_mainb << 16) | (trans_size << 17);
    case REG_FLASH_PROTECT_INFO1_ERASE_OFFSET:
      return protect_info1_erase;
    case REG_FLASH_TIMING_PROG_SMART_ALGO_ON_OFFSET:
      return prog_smart_algo;
    case REG_FLASH_TIMING_ERASE_SMART_ALGO_ON_OFFSET:
      return erase_smart_algo;
    case REG_FLASH_TIMING_BULKERASE_SMART_ALGO_ON_OFFSET:
      return bulkerase_smart_algo;
    case REG_FLASH_DOUT_VAL0_OFFSET:
      return dout_val[0];
    case REG_FLASH_DOUT_VAL1_OFFSET:
      return dout_val[1];
    case REG_FLASH_FSH_ENABLE_INFO0_SHADOW_READ_OFFSET:
      return enable_info0_shadow_read;
    case REG_FLASH_FSH_ICTRL_OFFSET:
      return fsh_ictrl;
    case REG_FLASH_FSH_ISTATE_OFFSET:
      return fsh_istate;
    case REG_FLASH_FSH_OVRD0_UNLOCK_OFFSET:
      return ovrd0_unlock;
    case REG_FLASH_FSH_OVRD1_UNLOCK_OFFSET:
      return ovrd1_unlock;
    case REG_FLASH_FSH_OVRD_SIGVAL_DIN_OFFSET:
      return ovrd_sigval_din;
    case REG_FLASH_FSH_OVRD_SIGVAL_OFFSET_OFFSET:
      return ovrd_sigval_offset;
    case REG_FLASH_FSH_OVRD_SIGVAL_OFFSET:
      return ovrd_sigval;
    case REG_FLASH_FSH_OVRD_SIGEN_OFFSET:
      return ovrd_sigen;
    case REG_FLASH_FSH_NO_WAIT_ON_BOUT_SEQ_OFFSET:
      return no_wait_on_bout_seq;
    case REG_FLASH_FSH_OVRD_SIGVAL_TC1_OFFSET:
      return ovrd_sigval_tc1;
    case REG_FLASH_PE_EN_OFFSET:
      return pe_en;
    case REG_FLASH_ERROR_OFFSET:
      return flash_error_read();
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
      if (pe_en != FLASH_PE_EN_MAGIC) {
        LOG_VBS("FLASH: ignored opcode write without PE_EN magic\n");
        return;
      }

      pe_en = 0;
      opcode = (uint32_t)value;
      if (opcode != 0) {
        flash_run_op(offset == REG_FLASH_PE_CONTROL0_OFFSET ? 0u : 1u);
      }
      return;
    case REG_FLASH_TRANS_OFFSET:
      trans_offset = (uint32_t)value & 0xFFFFu;
      trans_mainb = ((uint32_t)value >> 16) & 1u;
      trans_size = ((uint32_t)value >> 17) & 0x1Fu;
      return;
    case REG_FLASH_PROTECT_INFO1_ERASE_OFFSET:
      protect_info1_erase = (uint32_t)value & 1u;
      return;
    case REG_FLASH_TIMING_PROG_SMART_ALGO_ON_OFFSET:
    case REG_FLASH_TIMING_ERASE_SMART_ALGO_ON_OFFSET:
    case REG_FLASH_TIMING_BULKERASE_SMART_ALGO_ON_OFFSET:
      LOG_VBS("FLASH: ignored write to smart algorithm enable\n");
      return;
    case REG_FLASH_DOUT_VAL0_OFFSET:
    case REG_FLASH_DOUT_VAL1_OFFSET:
      LOG_VBS("FLASH: ignored write to DOUT_VAL\n");
      return;
    case REG_FLASH_FSH_ENABLE_INFO0_SHADOW_READ_OFFSET:
      enable_info0_shadow_read = (uint32_t)value;
      return;
    case REG_FLASH_FSH_ICTRL_OFFSET:
      fsh_ictrl = (uint32_t)value;
      return;
    case REG_FLASH_FSH_ISTATE_OFFSET:
      fsh_istate &= ~(uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD0_UNLOCK_OFFSET:
      ovrd0_unlock = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD1_UNLOCK_OFFSET:
      ovrd1_unlock = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD_SIGVAL_DIN_OFFSET:
      ovrd_sigval_din = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD_SIGVAL_OFFSET_OFFSET:
      ovrd_sigval_offset = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD_SIGVAL_OFFSET:
      ovrd_sigval = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD_SIGEN_OFFSET:
      ovrd_sigen = (uint32_t)value;
      return;
    case REG_FLASH_FSH_NO_WAIT_ON_BOUT_SEQ_OFFSET:
      no_wait_on_bout_seq = (uint32_t)value;
      return;
    case REG_FLASH_FSH_OVRD_SIGVAL_TC1_OFFSET:
      ovrd_sigval_tc1 = (uint32_t)value;
      return;
    case REG_FLASH_PE_EN_OFFSET:
      pe_en = (uint32_t)value;
      return;
    case REG_FLASH_ERROR_OFFSET:
      LOG_VBS("FLASH: ignored write to ERROR\n");
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
  error_ns = 0;
  dout_val[0] = 0;
  dout_val[1] = 0;
  protect_info1_erase = 0;
  prog_smart_algo = 1;
  erase_smart_algo = 1;
  bulkerase_smart_algo = 1;
  enable_info0_shadow_read = 0;
  fsh_ictrl = 0;
  fsh_istate = 0;
  ovrd0_unlock = 0;
  ovrd1_unlock = 0;
  ovrd_sigval_din = 0;
  ovrd_sigval_offset = 0;
  ovrd_sigval = 0;
  ovrd_sigen = 0;
  no_wait_on_bout_seq = 0;
  ovrd_sigval_tc1 = 0;

  for (unsigned i = 0; i < REG_FLASH_WR_DATA_COUNT; i++) wr_data[i] = 0;

  return component_register("FLASH", NULL, FLASH_Read, FLASH_Write);
}
