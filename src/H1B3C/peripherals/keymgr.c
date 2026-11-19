#include <H1B3C/peripherals/keymgr.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <common/logger.h>
#include <common/peripherals/define.h>
#include <common/sha256.h>

#define KEYMGR_SHA_INPUT_MAX 16384

DEFINE_BASIC_REGISTER(rwr_vld)
DEFINE_BASIC_REGISTER(rwr_lock)
DEFINE_BASIC_REGISTER(hkey_err_flags)
DEFINE_BASIC_REGISTER(cert_revoke_ctrl)
DEFINE_BASIC_REGISTER(sha_sts)
DEFINE_BASIC_REGISTER(sha_itcr)
DEFINE_BASIC_REGISTER(sha_use_hidden_key)
DEFINE_BASIC_REGISTER(sha_execute_count_state)
DEFINE_BASIC_REGISTER(sha_execute_count_max)

static const BasicRegister keymgr_registers[] = {
  BASIC_REGISTER_ENTRY(REG_KEYMGR_RWR_VLD_OFFSET, rwr_vld),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_RWR_LOCK_OFFSET, rwr_lock),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_HKEY_ERR_FLAGS_OFFSET, hkey_err_flags),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_STS_OFFSET, sha_sts),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_ITCR_OFFSET, sha_itcr),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_USE_HIDDEN_KEY_OFFSET, sha_use_hidden_key),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_EXECUTE_COUNT_STATE_OFFSET, sha_execute_count_state),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_EXECUTE_COUNT_MAX_OFFSET, sha_execute_count_max),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_CERT_REVOKES_CTRL_OFFSET, cert_revoke_ctrl),
};

static uint32_t hkey_rwr[REG_KEYMGR_HKEY_RWR_COUNT];
static uint32_t sha_msglen_lo;
static uint32_t sha_msglen_hi;
static uint64_t sha_msglen;
static uint32_t sha_en;
static uint32_t sha_wr_en;
static bool sha_receiving;
static uint32_t sha_itop;
static uint32_t sha_sts_h[8];
static uint32_t sha_key_w[REG_KEYMGR_SHA_KEY_W_COUNT];
static uint32_t sha_use_cert;
static uint32_t sha_cert_override;
static uint32_t sha_rand_stall_ctl;
static uint8_t sha_input[KEYMGR_SHA_INPUT_MAX];
static size_t sha_input_len;

static int rwr_index(uint64_t offset) {
  uint64_t end =
      REG_KEYMGR_HKEY_RWR0_OFFSET + (uint64_t)REG_KEYMGR_HKEY_RWR_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_KEYMGR_HKEY_RWR0_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_KEYMGR_HKEY_RWR0_OFFSET) / sizeof(uint32_t));
}

static int sts_h_index(uint64_t offset) {
  uint64_t end = REG_KEYMGR_SHA_STS_H0_OFFSET + 8u * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_KEYMGR_SHA_STS_H0_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_KEYMGR_SHA_STS_H0_OFFSET) / sizeof(uint32_t));
}

static int key_w_index(uint64_t offset) {
  uint64_t end =
      REG_KEYMGR_SHA_KEY_W0_OFFSET + (uint64_t)REG_KEYMGR_SHA_KEY_W_COUNT * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < REG_KEYMGR_SHA_KEY_W0_OFFSET || offset >= end) return -1;

  return (int)((offset - REG_KEYMGR_SHA_KEY_W0_OFFSET) / sizeof(uint32_t));
}

static void sha_hash(void) {
  Sha256Context context;
  uint8_t digest[32];
  unsigned i;

  if (sha_msglen > KEYMGR_SHA_INPUT_MAX) {
    LOG_ERR("KEYMGR: SHA message length %llu is too large\n", (unsigned long long)sha_msglen);
    sha_receiving = false;
    sha_itop = 1;
    return;
  }

  sha256_init(&context);
  sha256_update(&context, sha_input, (size_t)sha_msglen);
  sha256_final(&context, digest);

  for (i = 0; i < 8; i++) {
    sha_sts_h[i] = (uint32_t)digest[4 * i] | ((uint32_t)digest[4 * i + 1] << 8) |
                   ((uint32_t)digest[4 * i + 2] << 16) |
                   ((uint32_t)digest[4 * i + 3] << 24);
  }

  sha_itop = 1;
  sha_receiving = false;
  sha_input_len = 0;

  LOG_VBS("KEYMGR: SHA complete\n");
}

static void sha_input_write(uint64_t value, unsigned size) {
  unsigned i;

  if (!sha_receiving) return;

  if (size == 4) {
    for (i = 0; i < 4; i++) {
      if (sha_input_len >= KEYMGR_SHA_INPUT_MAX) break;
      sha_input[sha_input_len++] = (uint8_t)(value >> (8 * i));
    }
  } else if (size == 1) {
    if (sha_input_len < KEYMGR_SHA_INPUT_MAX) sha_input[sha_input_len++] = (uint8_t)value;
  }

  if (sha_msglen != 0 && sha_input_len >= sha_msglen) {
    sha_hash();
  } else if (sha_msglen > KEYMGR_SHA_INPUT_MAX && sha_input_len == KEYMGR_SHA_INPUT_MAX) {
    sha_hash();
  }
}

static uint64_t KEYMGR_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg =
      basic_register_find(keymgr_registers, ARRAY_LEN(keymgr_registers), offset);
  int index;

  if (reg != NULL) {
    LOG_VBS("KEYMGR: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  index = rwr_index(offset);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read HKEY_RWR%d (size=%u) -> 0x%08x\n", index, size, hkey_rwr[index]);
    return hkey_rwr[index];
  }

  index = sts_h_index(offset);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read SHA_STS_H%d (size=%u) -> 0x%08x\n", index, size, sha_sts_h[index]);
    return sha_sts_h[index];
  }

  index = key_w_index(offset);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read SHA_KEY_W%d (size=%u) -> 0x%08x\n", index, size, sha_key_w[index]);
    return sha_key_w[index];
  }

  switch (offset) {
    case REG_KEYMGR_SHA_CFG_MSGLEN_LO_OFFSET:
      return sha_msglen_lo;
    case REG_KEYMGR_SHA_CFG_MSGLEN_HI_OFFSET:
      return sha_msglen_hi;
    case REG_KEYMGR_SHA_CFG_EN_OFFSET:
      return sha_en;
    case REG_KEYMGR_SHA_CFG_WR_EN_OFFSET:
      return sha_wr_en;
    case REG_KEYMGR_SHA_TRIG_OFFSET:
      return 0;
    case REG_KEYMGR_SHA_INPUT_FIFO_OFFSET:
      LOG_VBS("KEYMGR: ignored read of SHA_INPUT_FIFO\n");
      return 0;
    case REG_KEYMGR_SHA_ITOP_OFFSET:
      LOG_VBS("KEYMGR: read SHA_ITOP (size=%u) -> %u\n", size, sha_itop);
      return sha_itop;
    case REG_KEYMGR_SHA_USE_CERT_OFFSET:
      return sha_use_cert;
    case REG_KEYMGR_SHA_CERT_OVERRIDE_OFFSET:
      return sha_cert_override;
    case REG_KEYMGR_SHA_RAND_STALL_CTL_OFFSET:
      return sha_rand_stall_ctl;
    default:
      component_log_unhandled(self, "read", offset);
      return 0;
  }
}

static void KEYMGR_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg =
      basic_register_find(keymgr_registers, ARRAY_LEN(keymgr_registers), offset);
  int index;

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  index = rwr_index(offset);
  if (index >= 0) {
    hkey_rwr[index] = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote HKEY_RWR%d (size=%u) <- 0x%08x\n", index, size, hkey_rwr[index]);
    return;
  }

  index = sts_h_index(offset);
  if (index >= 0) {
    LOG_VBS("KEYMGR: ignored write to SHA_STS_H%d\n", index);
    return;
  }

  index = key_w_index(offset);
  if (index >= 0) {
    sha_key_w[index] = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote SHA_KEY_W%d (size=%u) <- 0x%08x\n", index, size, sha_key_w[index]);
    return;
  }

  switch (offset) {
    case REG_KEYMGR_SHA_CFG_MSGLEN_LO_OFFSET:
      if (sha_receiving) return;
      sha_msglen_lo = (uint32_t)value;
      sha_msglen = ((uint64_t)sha_msglen_hi << 32) | sha_msglen_lo;
      return;
    case REG_KEYMGR_SHA_CFG_MSGLEN_HI_OFFSET:
      if (sha_receiving) return;
      sha_msglen_hi = (uint32_t)value;
      sha_msglen = ((uint64_t)sha_msglen_hi << 32) | sha_msglen_lo;
      return;
    case REG_KEYMGR_SHA_CFG_EN_OFFSET:
      if (sha_receiving) return;
      sha_en = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_CFG_WR_EN_OFFSET:
      sha_wr_en = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_TRIG_OFFSET:
      if (((uint32_t)value & 1u) != 0) {
        sha_input_len = 0;
        sha_itop = 0;
        sha_receiving = true;
      } else if (((uint32_t)value & 2u) != 0) {
        sha_receiving = false;
        sha_itop = 0;
        sha_input_len = 0;
      }
      return;
    case REG_KEYMGR_SHA_INPUT_FIFO_OFFSET:
      sha_input_write(value, size);
      return;
    case REG_KEYMGR_SHA_ITOP_OFFSET:
      sha_itop = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_USE_CERT_OFFSET:
      sha_use_cert = (uint32_t)value & 0xFFu;
      return;
    case REG_KEYMGR_SHA_CERT_OVERRIDE_OFFSET:
      sha_cert_override =
          ((uint32_t)value & 0x3Fu) | ((uint32_t)value & 0x3F0000u);
      return;
    case REG_KEYMGR_SHA_RAND_STALL_CTL_OFFSET: {
      uint32_t old = sha_rand_stall_ctl;
      uint32_t enable = (uint32_t)value & 1u;
      uint32_t freq = ((uint32_t)value >> 1) & 3u;

      sha_rand_stall_ctl = enable != 0 ? ((old & ~1u) | 1u) : (freq << 1);
      return;
    }
    default:
      component_log_unhandled(self, "write", offset);
      return;
  }
}

Component *KEYMGR_Initialize(void) {
  memset(hkey_rwr, 0, sizeof(hkey_rwr));
  memset(sha_sts_h, 0, sizeof(sha_sts_h));
  memset(sha_key_w, 0, sizeof(sha_key_w));

  rwr_vld = 0;
  rwr_lock = 0;
  hkey_err_flags = 0;
  sha_sts = 0;
  sha_itcr = 0;
  sha_use_hidden_key = 0;
  sha_execute_count_state = 0;
  sha_execute_count_max = 0;
  cert_revoke_ctrl = 0;
  sha_msglen_lo = 0;
  sha_msglen_hi = 0;
  sha_msglen = 0;
  sha_en = 0;
  sha_wr_en = 0;
  sha_receiving = false;
  sha_itop = 0;
  sha_use_cert = 0;
  sha_cert_override = 0;
  sha_rand_stall_ctl = 0;
  sha_input_len = 0;

  return component_register_threaded("KEYMGR", NULL, KEYMGR_Read, KEYMGR_Write);
}
