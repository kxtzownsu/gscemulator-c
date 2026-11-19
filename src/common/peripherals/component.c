#include <common/peripherals/component.h>

#include <stddef.h>
#include <string.h>

#include <common/common.h>
#include <common/runtime/control.h>
#include <common/runtime/thread.h>
#include <common/runtime/worker.h>

static uint64_t component_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *user_data) {
  Component *component = user_data;

  if (component->worker != NULL) {
    uint32_t pc = 0xFFFFFFFF;

    (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    return worker_read(component->worker, component, offset, size, pc);
  }

  (void)uc;
  return component->read(component, offset, size);
}

static void component_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
  Component *component = user_data;

  if (component->worker != NULL) {
    uint32_t pc = 0xFFFFFFFF;

    (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    worker_write(component->worker, component, offset, size, value, pc, false);
    return;
  }

  (void)uc;
  component->write(component, offset, size, value);
}

Component *component_find(const char *name) {
  size_t i;

  if (name == NULL) return NULL;

  for (i = 0; i < ctx.component_count; i++) {
    if (strcmp(ctx.components[i].name, name) == 0) return &ctx.components[i];
  }

  return NULL;
}

Component *component_register(const char *name, void *state, ComponentRead read, ComponentWrite write) {
  Component *component;

  if (name == NULL || name[0] == '\0' || read == NULL || write == NULL) {
    LOG_ERR("Invalid component registration\n");
    return NULL;
  }

  if (component_find(name) != NULL) {
    LOG_ERR("Component %s is already registered\n", name);
    return NULL;
  }

  if (ctx.component_count >= COMPONENT_MAX) {
    LOG_ERR("Too many components registered\n");
    return NULL;
  }

  component = &ctx.components[ctx.component_count++];
  component->name = name;
  component->state = state;
  component->read = read;
  component->write = write;
  component->poll = NULL;
  component->worker = NULL;

  return component;
}

Component *component_register_threaded(const char *name, void *state, ComponentRead read, ComponentWrite write) {
#ifdef SINGLE_THREAD
  return component_register(name, state, read, write);
#else
  Component *component = component_register(name, state, read, write);

  if (component == NULL) return NULL;

  component->worker = worker_start(component->name);
  if (component->worker == NULL) {
    LOG_ERR("Failed to start worker for %s\n", component->name);
    return NULL;
  }

  return component;
#endif
}

void component_set_poll(Component *component, ComponentPoll poll) {
  if (component == NULL) return;

  component->poll = poll;
}

void component_poll_all(void) {
  size_t i;

  for (i = 0; i < ctx.component_count; i++) {
    Component *component = &ctx.components[i];

    if (component->poll == NULL) continue;
    if (component->worker != NULL) {
      worker_poll(component->worker, component);
    } else {
      component->poll(component);
    }
  }
}

void component_stop_all(void) {
  size_t i;

  for (i = 0; i < ctx.component_count; i++) {
    Component *component = &ctx.components[i];

    if (component->worker != NULL) {
      worker_stop(component->worker);
      component->worker = NULL;
    }
  }
}

bool component_map_mmio(uc_engine *uc, const MmioRegion *regions, size_t count) {
  size_t i;

  if (uc == NULL || regions == NULL) {
    LOG_ERR("Invalid MMIO mapping request\n");
    return false;
  }

  for (i = 0; i < count; i++) {
    const MmioRegion *region = &regions[i];
    uc_err err;

    if (region->component == NULL) {
      LOG_ERR("Cannot map an unregistered MMIO region\n");
      return false;
    }

    err = uc_mmio_map(uc, region->base, region->size,
                      component_mmio_read, region->component,
                      component_mmio_write, region->component);
    if (err != UC_ERR_OK) {
      LOG_ERR("Failed to map %s MMIO: %s\n", region->component->name, uc_strerror(err));
      return false;
    }

    LOG_VBS("Mapped %s MMIO (base=0x%08x size=0x%08x)\n", region->component->name, region->base, region->size);
  }

  return true;
}

void component_log_unhandled(const Component *component, const char *access, uint64_t offset) {
  uint32_t pc = 0xFFFFFFFF;

  if (component == NULL) return;

  if (tctx.mmio_pc_valid) {
    pc = tctx.mmio_pc;
  } else if (ctx.uc != NULL && uc_reg_read(ctx.uc, UC_ARM_REG_PC, &pc) != UC_ERR_OK) {
    pc = 0xFFFFFFFF;
  }

  LOG_ERR("Unhandled %s register 0x%llx in component %s at pc=0x%08x\n",
          access, (unsigned long long)offset, component->name, pc);

  runtime_fault(RC_CHIPEMU_MEMORY_FAULT, "unhandled register in %s", component->name);
}
