#include <H1B3C/emulator.h>
#include <H1B3C/memory.h>

#include <string.h>

#include <unicorn/unicorn.h>
#include <inttypes.h>

#include <common/common.h>

#define PC_VTOR_OFFSET 0x4
#define SP_VTOR_OFFSET 0x0

void Havn_Initialize(void) {
  uc_err err;
  FileRC frc;
  uint8_t ROMBUF[FLASH_ROM_SIZE];

  memset(ROMBUF, 0xFF, sizeof(ROMBUF));

  err = uc_open(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, &ctx.uc);
  if (err != UC_ERR_OK) {
    ctx.uc = NULL;
    LOG_ERR("Failed to open unicorn engine: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Opened unicorn engine to ctx.uc\n");
  
  err = uc_ctl_set_cpu_model(ctx.uc, UC_CPU_ARM_CORTEX_M3);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to set CPU as Cortex-M3: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Set CPU to Cortex-M3\n");

  /* Map FLASH_ROM (RX) and SRAM (RW) */
  err = uc_mem_map(ctx.uc, FLASH_ROM_BASE, FLASH_ROM_SIZE, UC_PROT_READ | UC_PROT_EXEC);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to map FLASH_ROM: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Mapped FLASH_ROM (base=0x%08x size=0x%08x)\n", FLASH_ROM_BASE, FLASH_ROM_SIZE);

  err = uc_mem_map(ctx.uc, SRAM_BASE, SRAM_SIZE, UC_PROT_READ | UC_PROT_WRITE);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to map SRAM: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Mapped SRAM (base=0x%08x size=0x%08x)\n", SRAM_BASE, SRAM_SIZE);

  /* Read from ctx.firmware */
  frc = file_readbuf(ctx.firmware.bootrom, ROMBUF, sizeof(ROMBUF));
  if (frc != FILE_SUCCESS) {
    LOG_ERR("Failed to load BootROM %s\n", ctx.firmware.bootrom);
    emu_exit(RC_CHIPEMU_FAILED_BOOTROM_READ);
  }

  /* Write ROMBUF to FLASH_ROM */
  err = uc_mem_write(ctx.uc, FLASH_ROM_BASE, ROMBUF, sizeof(ROMBUF));
  if (err != UC_ERR_OK){
    LOG_ERR("unable to write ROMBUF to ROM_BASE: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Wrote ROMBUF to FLASH_ROM\n");

  /* Read ROM VTOR and set PC and SRAM. Default is 0xFFFFFFFF so we can easily halt execution if it isn't mapped correctly. */
  uint32_t sp = 0xFFFFFFFF;
  uint32_t pc = 0xFFFFFFFF;
  
  err = uc_mem_read(ctx.uc, FLASH_ROM_BASE + PC_VTOR_OFFSET, &pc, sizeof(pc));
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read PC from ROM: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  err = uc_mem_read(ctx.uc, FLASH_ROM_BASE + SP_VTOR_OFFSET, &sp, sizeof(sp));
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read SP from ROM: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
  
  LOG_VBS("Read VTOR (pc=0x%08x sp=0x%08x)\n", pc, sp);
  
  err = uc_reg_write(ctx.uc, UC_ARM_REG_SP, &sp);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to set SP (0x%08x): %s\n", sp, uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  err = uc_reg_write(ctx.uc, UC_ARM_REG_PC, &pc);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to set PC (0x%08x): %s\n", pc, uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  ctx.chipemu.ready = true;
}

void Havn_StartEmulation(void) {
  uc_err err;

  if (!ctx.chipemu.ready){
    LOG_ERR("Emulator not initialized!");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (ctx.uc == NULL){
    LOG_ERR("ctx.uc not set!");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  /* Default is 0xFFFFFFFF so we can easily halt execution if it isn't mapped correctly. */
  uint32_t sp = 0xFFFFFFFF;
  uint32_t pc = 0xFFFFFFFF;  

  err = uc_reg_read(ctx.uc, UC_ARM_REG_SP, &sp);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read SP: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_VTOR_SP_READ);
  }

  err = uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read PC: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_VTOR_PC_READ);
  }

  /* Set thumb bit */
  pc |= 1;

  LOG_VBS("Starting emulation at PC=0x%08x and SP=0x%08x\n", pc, sp);

  err = uc_emu_start(ctx.uc, pc, UINT32_MAX, 0, 0);
  if (err != UC_ERR_OK) {
    LOG_ERR("Emulation stopped with error: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_EMULATION_ERROR);
  }

  LOG_VBS("Emulation finished.\n");
}

void Havn_EndEmulation(void) {
  uc_err err;

  LOG_VBS("EndEmulation called\n");

  if (ctx.uc == NULL) return;

  err = uc_close(ctx.uc);
  ctx.uc = NULL;
  if (err != UC_ERR_OK) LOG_ERR("Failed to close unicorn engine: %s\n", uc_strerror(err));
  LOG_VBS("Closed ctx.uc\n");
}
