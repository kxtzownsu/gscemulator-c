#include <H1B3C/emulator.h>
#include <H1B3C/endorsement-cert.h>
#include <H1B3C/flash-persist.h>
#include <H1B3C/memory.h>
#include <H1B3C/rma.h>
#include <H1B3C/peripherals.h>
#include <H1B3C/proxies.h>

#include <string.h>

#ifdef UNICORN_SAFE_HOOKS
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#endif

#include <unicorn/unicorn.h>
#include <inttypes.h>

#include <common/common.h>
#include <common/peripherals/component.h>
#include <common/runtime/control.h>

#ifdef BUNDLED_FIRMWARE
#include <common/bundled-firmware.h>
#endif

#define EXC_RETURN_HANDLER 0xFFFFFFF1u
#define EXC_RETURN_MSP 0xFFFFFFF9u
#define EXC_RETURN_PSP 0xFFFFFFFDu

#define BOARD_GPIO_BATT_PRES_L 6
#define BOARD_GPIO_I2C_SCL 12
#define BOARD_GPIO_I2C_SDA 13

_Static_assert(ENDORSEMENT_SEED_OFFSET + ENDORSEMENT_SEED_SIZE <= FLASH_INFO1_SIZE,
               "endorsement seed must fit in INFO1");
_Static_assert(ENDORSEMENT_CERT_OFFSET + ENDORSEMENT_CERT_SIZE <= FLASH_PROG_SIZE,
               "endorsement certificates must fit in PROG");

static void exc_push(uc_engine *uc, uint32_t *sp, uint32_t value) {
  *sp -= 4;
  (void)uc_mem_write(uc, *sp, &value, sizeof(value));
}

static uint32_t exc_pop(uc_engine *uc, uint32_t *sp) {
  uint32_t value = 0;

  (void)uc_mem_read(uc, *sp, &value, sizeof(value));
  *sp += 4;
  return value;
}

static void enter_exception(uc_engine *uc, uint32_t exc_num) {
  uint32_t pc = 0;
  uint32_t control = 0;
  uint32_t ipsr = 0;
  uint32_t xpsr = 0;
  uint32_t lr = 0;
  uint32_t sp = 0;
  uint32_t exc_return;
  uint32_t vector;
  uint32_t handler_pc;
  uint32_t regs[8];
  unsigned i;

  (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
  (void)uc_reg_read(uc, UC_ARM_REG_CONTROL, &control);
  (void)uc_reg_read(uc, UC_ARM_REG_IPSR, &ipsr);
  (void)uc_reg_read(uc, UC_ARM_REG_XPSR, &xpsr);
  (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);

  if ((control & 2u) != 0 && ipsr == 0) {
    (void)uc_reg_read(uc, UC_ARM_REG_PSP, &sp);
    exc_return = EXC_RETURN_PSP;
  } else {
    (void)uc_reg_read(uc, UC_ARM_REG_MSP, &sp);
    exc_return = ipsr != 0 ? EXC_RETURN_HANDLER : EXC_RETURN_MSP;
  }

  regs[0] = xpsr | (1u << 24);
  regs[1] = pc | 1u;
  regs[2] = lr;
  (void)uc_reg_read(uc, UC_ARM_REG_R12, &regs[3]);
  (void)uc_reg_read(uc, UC_ARM_REG_R3, &regs[4]);
  (void)uc_reg_read(uc, UC_ARM_REG_R2, &regs[5]);
  (void)uc_reg_read(uc, UC_ARM_REG_R1, &regs[6]);
  (void)uc_reg_read(uc, UC_ARM_REG_R0, &regs[7]);

  for (i = 0; i < 8; i++) exc_push(uc, &sp, regs[i]);

  if (exc_return == EXC_RETURN_PSP) {
    (void)uc_reg_write(uc, UC_ARM_REG_PSP, &sp);
  } else {
    (void)uc_reg_write(uc, UC_ARM_REG_MSP, &sp);
  }

  (void)uc_reg_write(uc, UC_ARM_REG_LR, &exc_return);
  (void)uc_reg_write(uc, UC_ARM_REG_IPSR, &exc_num);

  xpsr = 0;
  (void)uc_reg_read(uc, UC_ARM_REG_EPSR, &xpsr);
  xpsr &= ~0x600FC00u;
  (void)uc_reg_write(uc, UC_ARM_REG_EPSR, &xpsr);

  vector = M3_GetVtor() + exc_num * 4u;
  if (uc_mem_read(uc, vector, &handler_pc, sizeof(handler_pc)) != UC_ERR_OK) {
    LOG_ERR("Exception %u: failed to read vector at 0x%08x\n", exc_num, vector);
    return;
  }

  LOG_VBS("Exception %u: entering handler at 0x%08x\n", exc_num, handler_pc);
  (void)uc_reg_write(uc, UC_ARM_REG_PC, &handler_pc);
}

static void handle_svc(uc_engine *uc) {
  uint32_t pc = 0;
  uint32_t ipsr = 0;
  uint32_t ret_pc;
  uint16_t insn = 0;

  (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);

  ret_pc = pc;
  if (uc_mem_read(uc, pc, &insn, sizeof(insn)) == UC_ERR_OK &&
      (insn & 0xFF00u) == 0xDF00u) {
    ret_pc = pc + 2;
  }

  (void)uc_reg_write(uc, UC_ARM_REG_PC, &ret_pc);
  (void)uc_reg_read(uc, UC_ARM_REG_IPSR, &ipsr);

  if (ipsr != 0 && M3_ExceptionPriority(11) >= M3_ExceptionPriority(ipsr)) {
    unsigned exc = M3_EscalateFault(M3_FAULT_HARD, 0);

    LOG_VBS("SVC: priority blocked by exception %u, escalating to %u\n", ipsr, exc);
    enter_exception(uc, exc);
    return;
  }

  enter_exception(uc, 11);
}

static void handle_exc_return(uc_engine *uc) {
  uint32_t pc = 0;
  uint32_t sp = 0;
  uint32_t value;

  (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);

  if ((pc | 1u) == EXC_RETURN_PSP) {
    (void)uc_reg_read(uc, UC_ARM_REG_PSP, &sp);
  } else {
    (void)uc_reg_read(uc, UC_ARM_REG_MSP, &sp);
  }

  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_R0, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_R1, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_R2, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_R3, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_R12, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_LR, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_PC, &value);
  value = exc_pop(uc, &sp);
  (void)uc_reg_write(uc, UC_ARM_REG_XPSR, &value);

  if ((pc | 1u) == EXC_RETURN_PSP) {
    (void)uc_reg_write(uc, UC_ARM_REG_PSP, &sp);
  } else {
    (void)uc_reg_write(uc, UC_ARM_REG_MSP, &sp);
  }

  {
    uint32_t ipsr = value & 0x1FFu;
    uint32_t faultmask = 0;

    (void)uc_reg_write(uc, UC_ARM_REG_IPSR, &ipsr);
    if (ipsr != 2) (void)uc_reg_write(uc, UC_ARM_REG_FAULTMASK, &faultmask);
  }
}


#define M3_SLICE_INSTRUCTIONS 10000u

static void m3_poll(uc_engine *uc) {
  static uint64_t poll_count;
  uint32_t ipsr = 0;
  uint32_t primask = 0;
  uint32_t faultmask = 0;
  int exc;

  TIMELS_Poll();
  GLOBALSEC_Poll();
  GPIO_Poll();
  TIMEHS_Poll();
  RMA_Poll();
  if ((++poll_count & 0x3Fu) == 0) component_poll_all();

  if (!M3_IrqPending()) return;

  (void)uc_reg_read(uc, UC_ARM_REG_IPSR, &ipsr);
  (void)uc_reg_read(uc, UC_ARM_REG_PRIMASK, &primask);
  (void)uc_reg_read(uc, UC_ARM_REG_FAULTMASK, &faultmask);

  exc = M3_TakeException(ipsr, primask != 0, faultmask != 0);
  if (exc <= 0) return;

  if (ipsr != 0) {
    LOG_VBS("Exception %d: preempting exception %u\n", exc, ipsr);
  }
  enter_exception(uc, (unsigned)exc);
}

#ifdef UNICORN_SAFE_HOOKS
static _Atomic bool m3_safe_stop;
static _Atomic bool m3_safe_running;
static pthread_t m3_safe_thread;

static void m3_safe_hook(uc_engine *uc, void *user_data) {
  (void)user_data;
  m3_poll(uc);
}

static void *m3_safe_worker(void *arg) {
  (void)arg;

  while (!atomic_load(&m3_safe_stop)) {
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };

    nanosleep(&delay, NULL);

    if (atomic_load(&m3_safe_running)) (void)uc_trigger_safe_hook(ctx.uc);
  }

  return NULL;
}
#endif

static bool escalate_unicorn_error(uc_engine *uc, uc_err err) {
  unsigned kind;
  uint32_t status;
  uint32_t ipsr = 0;
  unsigned exc;

  switch (err) {
    case UC_ERR_INSN_INVALID:
    case UC_ERR_EXCEPTION:
      kind = M3_FAULT_USAGE;
      status = 1u << 16;
      break;
    case UC_ERR_READ_UNALIGNED:
    case UC_ERR_WRITE_UNALIGNED:
    case UC_ERR_FETCH_UNALIGNED:
      kind = M3_FAULT_USAGE;
      status = 1u << 20;
      break;
    default:
      return false;
  }

  (void)uc_reg_read(uc, UC_ARM_REG_IPSR, &ipsr);
  if (ipsr == 2 || ipsr == 3) return false;

  exc = M3_EscalateFault(kind, status);
  LOG_VBS("Escalated %s to exception %u\n", uc_strerror(err), exc);
  enter_exception(uc, exc);
  return true;
}

static void intr_hook(uc_engine *uc, uint32_t intno, void *user_data) {
  (void)user_data;

  if (intno == 2) {
    handle_svc(uc);
  } else if (intno == 8) {
    handle_exc_return(uc);
  } else {
    LOG_VBS("Unhandled interrupt %u\n", intno);
  }
}

void init_unicorn(void){
  uc_err err;
  uc_hook hook;
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

  err = uc_hook_add(ctx.uc, &hook, UC_HOOK_INTR, (void *)intr_hook, NULL, 1, 0);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to install interrupt hook: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

#ifdef UNICORN_SAFE_HOOKS
  err = uc_hook_add(ctx.uc, &hook, UC_HOOK_SAFE, (void *)m3_safe_hook, NULL, 1, 0);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to install safe hook: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
#endif

}

void Havn_Initialize(void) {
  uc_err err;
  uint8_t INFO0BUF[FLASH_INFO0_SIZE];
  uint8_t INFO1BUF[FLASH_INFO1_SIZE];

  /* blank flash is 0xFF */
  memset(INFO0BUF, 0xFF, sizeof(INFO0BUF));
  memset(INFO1BUF, 0xFF, sizeof(INFO1BUF));
  memcpy(INFO1BUF + ENDORSEMENT_SEED_OFFSET, PROD_ENDORSEMENT_SEED,
         ENDORSEMENT_SEED_SIZE);

  init_unicorn();

  /* Maps ROM, SRAM, and FLASH (PROG & Info1) */
  ctx.essential_memory = H1B3C_EssentialMemoryArray;
  map_essential_memory();

  err = uc_mem_write(ctx.uc, FLASH_INFO0_BASE, INFO0BUF, sizeof(INFO0BUF));
  if (err != UC_ERR_OK) {
    LOG_ERR("unable to write INFO0BUF to FLASH_INFO0: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

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

  err = uc_mem_write(ctx.uc, FLASH_PROG_BASE + ENDORSEMENT_CERT_OFFSET,
                     PROD_RO_CERT_REGION, ENDORSEMENT_CERT_SIZE);
  if (err != UC_ERR_OK) {
    LOG_ERR("unable to write endorsement certificates: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Wrote endorsement certificates to the RO region\n");

  /* Write INFO1BUF to FLASH_INFO1 */
  err = uc_mem_write(ctx.uc, FLASH_INFO1_BASE, INFO1BUF, sizeof(INFO1BUF));
  if (err != UC_ERR_OK){
    LOG_ERR("unable to write INFO1BUF to FLASH_INFO1: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Wrote INFO1BUF to FLASH_INFO1\n");

  FlashPersist_Apply();

  err = uc_mem_write(ctx.uc, FLASH_INFO1_BASE + ENDORSEMENT_SEED_OFFSET,
                     PROD_ENDORSEMENT_SEED, ENDORSEMENT_SEED_SIZE);
  if (err != UC_ERR_OK) {
    LOG_ERR("unable to patch the endorsement seed: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  err = uc_mem_write(ctx.uc, FLASH_PROG_BASE + ENDORSEMENT_CERT_OFFSET,
                     PROD_RO_CERT_REGION, ENDORSEMENT_CERT_SIZE);
  if (err != UC_ERR_OK) {
    LOG_ERR("unable to patch the endorsement certificates: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  endorsement_patch_ca(ctx.uc);

  set_sp_pc(ctx.uc, FLASH_ROM_BASE);

  initialize_peripherals();
  GPIO_SetInputOverride(0, BOARD_GPIO_BATT_PRES_L, true);
  GPIO_SetInputOverride(0, BOARD_GPIO_I2C_SCL, true);
  GPIO_SetInputOverride(0, BOARD_GPIO_I2C_SDA, true);

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

#ifdef UNICORN_SAFE_HOOKS
  atomic_store(&m3_safe_stop, false);
  atomic_store(&m3_safe_running, false);

  if (pthread_create(&m3_safe_thread, NULL, m3_safe_worker, NULL) != 0) {
    runtime_fault(RC_CHIPEMU_FAILED_INIT, "failed to create the safe poll thread");
    return;
  }

  for (;;) {
    atomic_store(&m3_safe_running, true);
    err = uc_emu_start(ctx.uc, pc, UINT32_MAX, 0, 0);
    atomic_store(&m3_safe_running, false);
    if (memory_fault_occurred()) break;
    if (runtime_is_shutting_down()) break;
    if (err != UC_ERR_OK && !escalate_unicorn_error(ctx.uc, err)) break;

    if (uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc) != UC_ERR_OK) break;
    pc |= 1u;
  }

  atomic_store(&m3_safe_stop, true);
  pthread_join(m3_safe_thread, NULL);
#else
  for (;;) {
    err = uc_emu_start(ctx.uc, pc, UINT32_MAX, 0, M3_SLICE_INSTRUCTIONS);
    if (memory_fault_occurred()) break;
    if (runtime_is_shutting_down()) break;

    if (err == UC_ERR_OK) {
      m3_poll(ctx.uc);
      if (uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc) != UC_ERR_OK) break;
      pc |= 1u;
      continue;
    }

    if (!escalate_unicorn_error(ctx.uc, err)) break;

    if (uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc) != UC_ERR_OK) break;
    pc |= 1u;
  }
#endif

  {
    uint32_t stop_pc = 0;

    if (uc_reg_read(ctx.uc, UC_ARM_REG_PC, &stop_pc) == UC_ERR_OK) {
      LOG_VBS("Emulation stopped at PC=0x%08x\n", stop_pc);
    }
  }

  if (memory_fault_occurred()) {
    LOG_ERR("Emulation stopped due to a memory fault\n");
    runtime_fault(RC_CHIPEMU_MEMORY_FAULT, "emulation stopped due to a memory fault");
    return;
  }

  if (err != UC_ERR_OK) {
    uint32_t fault_pc = 0;
    uint32_t fault_lr = 0;
    uint32_t fault_sp = 0;

    (void)uc_reg_read(ctx.uc, UC_ARM_REG_PC, &fault_pc);
    (void)uc_reg_read(ctx.uc, UC_ARM_REG_LR, &fault_lr);
    (void)uc_reg_read(ctx.uc, UC_ARM_REG_SP, &fault_sp);
    LOG_ERR("Emulation stopped with error: %s (pc=0x%08x lr=0x%08x sp=0x%08x)\n",
            uc_strerror(err), fault_pc, fault_lr, fault_sp);
    runtime_fault(RC_CHIPEMU_EMULATION_ERROR, "%s", uc_strerror(err));
    return;
  }

  LOG_VBS("Emulation finished.\n");
}

void Havn_Reboot(void) {
  LOG("Rebooting the emulated chip\n");

  H1B3C_SuspendUSBProxy();
  FlashPersist_Save();
  component_stop_all();

  if (ctx.uc != NULL) {
    uc_err err = uc_close(ctx.uc);

    ctx.uc = NULL;
    if (err != UC_ERR_OK) LOG_ERR("Failed to close unicorn engine: %s\n", uc_strerror(err));
  }

  ctx.component_count = 0;
  ctx.chipemu.ready = false;

  Havn_Initialize();
  H1B3C_ResumeUSBProxy();
}

void Havn_EndEmulation(void) {
  uc_err err;

  LOG_VBS("EndEmulation called\n");

  H1B3C_StopProxies();

  FlashPersist_Save();

  if (ctx.uc == NULL) return;

  err = uc_close(ctx.uc);
  ctx.uc = NULL;
  if (err != UC_ERR_OK) LOG_ERR("Failed to close unicorn engine: %s\n", uc_strerror(err));
  LOG_VBS("Closed ctx.uc\n");
}
