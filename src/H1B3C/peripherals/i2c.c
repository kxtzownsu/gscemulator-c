#include <H1B3C/peripherals/i2c.h>

#include <stdint.h>
#include <string.h>

#define I2C_INST_START (1u << 1)
#define I2C_STATUS_FINALSTOP (1u << 23)
#define I2C_STATUS_CA_NACK (1u << 25)

typedef struct {
  uint32_t regs[REG_I2C_REGION_COUNT];
  uint32_t itcr;
  uint32_t itop;
} I2cState;

typedef struct {
  uint32_t regs[REG_I2CS_REGION_COUNT];
} I2csState;

static I2cState i2c_states[2];
static I2csState i2cs_state;

static const uint32_t i2c_defaults[REG_I2C_REGION_COUNT] = {
  [REG_I2C_CTRL_MODE_OFFSET / 4] = 0x0,
  [REG_I2C_CTRL_CLKDIV_OFFSET / 4] = 0xA,
  [REG_I2C_CTRL_PHASESTEPS_OFFSET / 4] = 0x188186,
  [REG_I2C_CTRL_SDA_VAL_OFFSET / 4] = 0x1897F0F,
  [REG_I2C_CTRL_SDA_OVRD_OFFSET / 4] = 0x300,
  [REG_I2C_CTRL_SCL_VAL_OFFSET / 4] = 0x67666E,
  [REG_I2C_CTRL_SCL_OVRD_OFFSET / 4] = 0x600,
  [REG_I2C_CTRL_INT_EN_OFFSET / 4] = 0x1,
  [REG_I2C_CTRL_AL_OFFSET / 4] = 0x1F,
  [REG_I2C_CTRL_CS_OFFSET / 4] = 0x13883,
  [REG_I2C_CTRL_MSR_OFFSET / 4] = 0xA,
};

static const uint32_t i2cs_defaults[REG_I2CS_REGION_COUNT] = {
  [0x00 / 4] = 0x101424A,
  [0x10 / 4] = 0x3D,
  [0x30 / 4] = 0xA,
};

static uint64_t I2C_Read(Component *self, uint64_t offset, unsigned size) {
  I2cState *state = self->state;

  (void)size;

  if (offset < REG_I2C_REGION_COUNT * 4) return state->regs[offset / 4];
  if (offset == REG_I2C_ITCR_OFFSET) return state->itcr;
  if (offset == REG_I2C_ITOP_OFFSET) return state->itop;

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void I2C_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  I2cState *state = self->state;
  uint32_t written = (uint32_t)value;

  (void)size;

  if (offset < REG_I2C_REGION_COUNT * 4) {
    if (offset == REG_I2C_INST_OFFSET) {
      if ((written & I2C_INST_START) != 0) {
        state->regs[REG_I2C_STATUS_OFFSET / 4] = I2C_STATUS_CA_NACK | I2C_STATUS_FINALSTOP;
      } else {
        state->regs[REG_I2C_STATUS_OFFSET / 4] = 0;
      }
    }

    state->regs[offset / 4] = written;
    return;
  }

  if (offset == REG_I2C_ITCR_OFFSET) {
    state->itcr = written;
    return;
  }

  if (offset == REG_I2C_ITOP_OFFSET) {
    state->itop = written;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

static uint64_t I2CS_Read(Component *self, uint64_t offset, unsigned size) {
  I2csState *state = self->state;

  (void)size;

  if (offset < REG_I2CS_REGION_COUNT * 4) return state->regs[offset / 4];

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void I2CS_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  I2csState *state = self->state;

  (void)size;

  if (offset < REG_I2CS_REGION_COUNT * 4) {
    state->regs[offset / 4] = (uint32_t)value;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *I2C_Initialize(unsigned port) {
  const char *name;

  if (port > 1) return NULL;

  name = port == 0 ? "I2C0" : "I2C1";
  memcpy(i2c_states[port].regs, i2c_defaults, sizeof(i2c_defaults));
  i2c_states[port].itcr = 0;
  i2c_states[port].itop = 0;

  return component_register(name, &i2c_states[port], I2C_Read, I2C_Write);
}

Component *I2CS_Initialize(void) {
  memcpy(i2cs_state.regs, i2cs_defaults, sizeof(i2cs_defaults));

  return component_register("I2CS", &i2cs_state, I2CS_Read, I2CS_Write);
}
