#include <H1B3C/peripherals/gpio.h>

#include <stdint.h>
#include <string.h>

#include <common/logger.h>

typedef struct {
  uint32_t dataout;
  uint32_t douten;
  uint32_t inten;
  uint32_t inttype;
  uint32_t intpol;
  uint32_t intstat;
  uint32_t reserved0;
  uint32_t reserved1;
} GpioState;

static GpioState gpio_states[2];

static int mask_index(uint64_t offset, uint32_t base) {
  uint64_t end = (uint64_t)base + (uint64_t)REG_GPIO_MASK_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < base || offset >= end) return -1;

  return (int)((offset - base) / sizeof(uint32_t));
}

static uint64_t GPIO_Read(Component *self, uint64_t offset, unsigned size) {
  GpioState *state = self->state;
  int index;

  switch (offset) {
    case REG_GPIO_DATAIN_OFFSET:
      LOG_VBS("GPIO: read DATAIN (size=%u) -> 0\n", size);
      return 0;
    case REG_GPIO_DOUT_OFFSET:
      LOG_VBS("GPIO: read DOUT (size=%u) -> 0x%04x\n", size, state->dataout);
      return state->dataout;
    case REG_GPIO_SETDOUTEN_OFFSET:
    case REG_GPIO_CLRDOUTEN_OFFSET:
      LOG_VBS("GPIO: read DOUTEN (size=%u) -> 0x%04x\n", size, state->douten);
      return state->douten;
    case REG_GPIO_RESERVED0_OFFSET:
      return state->reserved0;
    case REG_GPIO_RESERVED1_OFFSET:
      return state->reserved1;
    case REG_GPIO_SETINTEN_OFFSET:
    case REG_GPIO_CLRINTEN_OFFSET:
      LOG_VBS("GPIO: read INTEN (size=%u) -> 0x%04x\n", size, state->inten);
      return state->inten;
    case REG_GPIO_SETINTTYPE_OFFSET:
    case REG_GPIO_CLRINTTYPE_OFFSET:
      LOG_VBS("GPIO: read INTTYPE (size=%u) -> 0x%04x\n", size, state->inttype);
      return state->inttype;
    case REG_GPIO_SETINTPOL_OFFSET:
    case REG_GPIO_CLRINTPOL_OFFSET:
      LOG_VBS("GPIO: read INTPOL (size=%u) -> 0x%04x\n", size, state->intpol);
      return state->intpol;
    case REG_GPIO_CLRINTSTAT_OFFSET:
      LOG_VBS("GPIO: read INTSTAT (size=%u) -> 0x%04x\n", size, state->intstat);
      return state->intstat;
    default:
      break;
  }

  index = mask_index(offset, REG_GPIO_MASKLOWBYTE_OFFSET);
  if (index >= 0) {
    LOG_VBS("GPIO: read MASKLOWBYTE%d (size=%u) -> 0x%04x\n", index, size,
            state->dataout & (uint32_t)index);
    return state->dataout & (uint32_t)index;
  }

  index = mask_index(offset, REG_GPIO_MASKHIGHBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index << 8;

    LOG_VBS("GPIO: read MASKHIGHBYTE%d (size=%u) -> 0x%04x\n", index, size,
            state->dataout & mask);
    return state->dataout & mask;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void GPIO_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  GpioState *state = self->state;
  uint32_t bits = (uint32_t)value & 0xFFFFu;
  int index;

  switch (offset) {
    case REG_GPIO_DATAIN_OFFSET:
      LOG_VBS("GPIO: ignored write to DATAIN\n");
      return;
    case REG_GPIO_DOUT_OFFSET:
      state->dataout = bits;
      LOG_VBS("GPIO: wrote DOUT (size=%u) <- 0x%04x\n", size, state->dataout);
      return;
    case REG_GPIO_SETDOUTEN_OFFSET:
      state->douten |= bits;
      LOG_VBS("GPIO: wrote SETDOUTEN (size=%u) <- 0x%04x\n", size, state->douten);
      return;
    case REG_GPIO_CLRDOUTEN_OFFSET:
      state->douten &= ~bits;
      LOG_VBS("GPIO: wrote CLRDOUTEN (size=%u) <- 0x%04x\n", size, state->douten);
      return;
    case REG_GPIO_RESERVED0_OFFSET:
      state->reserved0 = bits;
      return;
    case REG_GPIO_RESERVED1_OFFSET:
      state->reserved1 = bits;
      return;
    case REG_GPIO_SETINTEN_OFFSET:
      state->inten |= bits;
      LOG_VBS("GPIO: wrote SETINTEN (size=%u) <- 0x%04x\n", size, state->inten);
      return;
    case REG_GPIO_CLRINTEN_OFFSET:
      state->inten &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTEN (size=%u) <- 0x%04x\n", size, state->inten);
      return;
    case REG_GPIO_SETINTTYPE_OFFSET:
      state->inttype |= bits;
      LOG_VBS("GPIO: wrote SETINTTYPE (size=%u) <- 0x%04x\n", size, state->inttype);
      return;
    case REG_GPIO_CLRINTTYPE_OFFSET:
      state->inttype &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTTYPE (size=%u) <- 0x%04x\n", size, state->inttype);
      return;
    case REG_GPIO_SETINTPOL_OFFSET:
      state->intpol |= bits;
      LOG_VBS("GPIO: wrote SETINTPOL (size=%u) <- 0x%04x\n", size, state->intpol);
      return;
    case REG_GPIO_CLRINTPOL_OFFSET:
      state->intpol &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTPOL (size=%u) <- 0x%04x\n", size, state->intpol);
      return;
    case REG_GPIO_CLRINTSTAT_OFFSET:
      state->intstat &= ~bits;
      LOG_VBS("GPIO: wrote CLRINTSTAT (size=%u) <- 0x%04x\n", size, state->intstat);
      return;
    default:
      break;
  }

  index = mask_index(offset, REG_GPIO_MASKLOWBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index;

    state->dataout = (state->dataout & ~mask) | (bits & mask);
    LOG_VBS("GPIO: wrote MASKLOWBYTE%d (size=%u) <- 0x%04x\n", index, size, state->dataout);
    return;
  }

  index = mask_index(offset, REG_GPIO_MASKHIGHBYTE_OFFSET);
  if (index >= 0) {
    uint32_t mask = (uint32_t)index << 8;

    state->dataout = (state->dataout & ~mask) | (bits & mask);
    LOG_VBS("GPIO: wrote MASKHIGHBYTE%d (size=%u) <- 0x%04x\n", index, size, state->dataout);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *GPIO_Initialize(unsigned bank) {
  const char *name;

  if (bank > 1) return NULL;

  name = bank == 0 ? "GPIO0" : "GPIO1";
  memset(&gpio_states[bank], 0, sizeof(gpio_states[bank]));

  return component_register(name, &gpio_states[bank], GPIO_Read, GPIO_Write);
}
