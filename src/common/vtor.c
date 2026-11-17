#include <common/vtor.h>

#include <unicorn/unicorn.h>

#include <common/exit.h>
#include <common/logger.h>
#include <common/rc.h>

#define SP_VTOR_OFFSET 0x0
#define PC_VTOR_OFFSET 0x4

void set_sp_pc(uc_engine *uc, uint32_t vtor_base) {
  uint32_t sp = 0xFFFFFFFF;
  uint32_t pc = 0xFFFFFFFF;
  uc_err err;

  if (uc == NULL) {
    LOG_ERR("Cannot set SP/PC without an engine\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  err = uc_mem_read(uc, vtor_base + SP_VTOR_OFFSET, &sp, sizeof(sp));
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to read SP from VTOR: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_VTOR_SP_READ);
  }

  err = uc_mem_read(uc, vtor_base + PC_VTOR_OFFSET, &pc, sizeof(pc));
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to read PC from VTOR: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_VTOR_PC_READ);
  }

  LOG_VBS("Read VTOR (pc=0x%08x sp=0x%08x)\n", pc, sp);

  err = uc_reg_write(uc, UC_ARM_REG_SP, &sp);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to set SP (0x%08x): %s\n", sp, uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  err = uc_reg_write(uc, UC_ARM_REG_PC, &pc);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to set PC (0x%08x): %s\n", pc, uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
}
