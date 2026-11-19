#include <H1B3C/emulator.h>
#include <H1B3C/memory.h>
#include <H1B3C/peripherals.h>

#include <string.h>

#include <unicorn/unicorn.h>
#include <inttypes.h>

#include <common/common.h>
#include <common/runtime/control.h>

#ifdef BUNDLED_FIRMWARE
#include <common/bundled-firmware.h>
#endif

void init_unicorn(void){
  uc_err err;
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

  install_memory_fault_hook();
}

void Havn_Initialize(void) {
  uc_err err;
  uint8_t INFO1BUF[FLASH_INFO1_SIZE];

  /* blank flash is 0xFF */
  memset(INFO1BUF, 0xFF, sizeof(INFO1BUF));

  init_unicorn();

  /* Maps ROM, SRAM, and FLASH (PROG & Info1) */
  ctx.essential_memory = H1B3C_EssentialMemoryArray;
  map_essential_memory();

#ifdef BUNDLED_H1B3C
  if (ctx.firmware.bootrom[0] == '\0') ctx.firmware.useBundledROM = true;
  if (ctx.firmware.progfw[0] == '\0') ctx.firmware.useBundledFW = true;
#endif

#ifdef BUNDLED_H1B3C
  load_firmware(BUNDLED_H1B3C_BOOTROM_BIN, sizeof(BUNDLED_H1B3C_BOOTROM_BIN),
                BUNDLED_H1B3C_FIRMWARE_BIN, sizeof(BUNDLED_H1B3C_FIRMWARE_BIN));
#else
  load_firmware(NULL, 0, NULL, 0);
#endif

  /* Write INFO1BUF to FLASH_INFO1 */
  err = uc_mem_write(ctx.uc, FLASH_INFO1_BASE, INFO1BUF, sizeof(INFO1BUF));
  if (err != UC_ERR_OK){
    LOG_ERR("unable to write INFO1BUF to FLASH_INFO1: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Wrote INFO1BUF to FLASH_INFO1\n");

  set_sp_pc(ctx.uc, FLASH_ROM_BASE);

  initialize_peripherals();

  ctx.chipemu.ready = true;
}

void Havn_StartEmulation(void) {
  uc_err err;

  if (!ctx.chipemu.ready){
    LOG_ERR("Emulator not initialized!");
    runtime_fault(RC_CHIPEMU_FAILED_INIT, "emulator not initialized");
    return;
  }

  if (ctx.uc == NULL){
    LOG_ERR("ctx.uc not set!");
    runtime_fault(RC_CHIPEMU_FAILED_INIT, "unicorn engine not set");
    return;
  }

  /* Default is 0xFFFFFFFF so we can easily halt execution if it isn't mapped correctly. */
  uint32_t sp = 0xFFFFFFFF;
  uint32_t pc = 0xFFFFFFFF;  

  err = uc_reg_read(ctx.uc, UC_ARM_REG_SP, &sp);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read SP: %s\n", uc_strerror(err));
    runtime_fault(RC_CHIPEMU_FAILED_VTOR_SP_READ, "failed to read SP");
    return;
  }

  err = uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc);
  if (err != UC_ERR_OK){
    LOG_ERR("Failed to read PC: %s\n", uc_strerror(err));
    runtime_fault(RC_CHIPEMU_FAILED_VTOR_PC_READ, "failed to read PC");
    return;
  }

  /* Set thumb bit */
  pc |= 1;

  LOG_VBS("Starting emulation at PC=0x%08x and SP=0x%08x\n", pc, sp);

  M3_StartCycleCount();

  err = uc_emu_start(ctx.uc, pc, UINT32_MAX, 0, 0);

  if (memory_fault_occurred()) {
    LOG_ERR("Emulation stopped due to a memory fault\n");
    runtime_fault(RC_CHIPEMU_MEMORY_FAULT, "emulation stopped due to a memory fault");
    return;
  }

  if (err != UC_ERR_OK) {
    LOG_ERR("Emulation stopped with error: %s\n", uc_strerror(err));
    runtime_fault(RC_CHIPEMU_EMULATION_ERROR, "%s", uc_strerror(err));
    return;
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
