#include <common/memory-fault.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include <unicorn/unicorn.h>

#include <common/ctx.h>
#include <common/exit.h>
#include <common/logger.h>
#include <common/rc.h>

static _Atomic bool fault_occurred;

static void memory_fault_hook(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
                              int64_t value, void *user_data) {
  uint32_t pc = 0xFFFFFFFF;
  const char *access = "access";
  bool is_prot = type == UC_MEM_READ_PROT || type == UC_MEM_WRITE_PROT || type == UC_MEM_FETCH_PROT;
  const char *fault = is_prot ? "Protected" : "Unmapped";

  (void)user_data;

  switch (type) {
    case UC_MEM_READ:
    case UC_MEM_READ_UNMAPPED:
    case UC_MEM_READ_PROT:
      access = "read";
      break;
    case UC_MEM_WRITE:
    case UC_MEM_WRITE_UNMAPPED:
    case UC_MEM_WRITE_PROT:
      access = "write";
      break;
    case UC_MEM_FETCH:
    case UC_MEM_FETCH_UNMAPPED:
    case UC_MEM_FETCH_PROT:
      access = "fetch";
      break;
    default:
      break;
  }

  if (uc_reg_read(uc, UC_ARM_REG_PC, &pc) != UC_ERR_OK) pc = 0xFFFFFFFF;

  if (type == UC_MEM_WRITE || type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT) {
    LOG_ERR("%s memory write at 0x%llx (size=%d value=0x%llx) pc=0x%08x\n",
            fault, (unsigned long long)address, size, (unsigned long long)value, pc);
  } else {
    LOG_ERR("%s memory %s at 0x%llx (size=%d) pc=0x%08x\n",
            fault, access, (unsigned long long)address, size, pc);
  }

  atomic_store(&fault_occurred, true);
  (void)uc_emu_stop(uc);
}

bool memory_fault_occurred(void) {
  return atomic_load(&fault_occurred);
}

void install_memory_fault_hook(void) {
  uc_hook hook;
  uc_err err;

  if (ctx.uc == NULL) {
    LOG_ERR("Cannot install memory fault hook without an engine\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  atomic_store(&fault_occurred, false);

  err = uc_hook_add(ctx.uc, &hook,
                    UC_HOOK_MEM_READ_INVALID | UC_HOOK_MEM_WRITE_INVALID | UC_HOOK_MEM_FETCH_INVALID,
                    (void *)memory_fault_hook, NULL, 1, 0);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to install memory fault hook: %s\n", uc_strerror(err));
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Installed memory fault hook\n");
}
