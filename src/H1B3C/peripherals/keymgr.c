#include <H1B3C/peripherals/keymgr.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <common/aes.h>
#include <common/logger.h>
#include <common/peripherals/define.h>
#include <common/sha256.h>

#define KEYMGR_AES_CTRL_RESET (1u << 0)
#define KEYMGR_AES_CTRL_ENABLE (1u << 7)
#define KEYMGR_AES_CTRL_CTR_BIG_ENDIAN (1u << 6)
#define KEYMGR_AES_CTRL_ENC_MODE (1u << 5)
#define KEYMGR_AES_MODE_ECB 0u
#define KEYMGR_AES_MODE_CTR 1u
#define KEYMGR_AES_MODE_CBC 2u
#define KEYMGR_AES_MODE_GCM 3u
#define KEYMGR_AES_KEYSIZE(word) (((word) >> 1) & 3u)
#define KEYMGR_AES_MODE(word) (((word) >> 3) & 3u)
#define KEYMGR_AES_USE_HIDDEN_KEY_ENABLE (1u << 10)

#define KEYMGR_AES_INT_WFIFO_OVERFLOW (1u << 0)
#define KEYMGR_AES_INT_RFIFO_OVERFLOW (1u << 1)
#define KEYMGR_AES_INT_RFIFO_UNDERFLOW (1u << 2)
#define KEYMGR_AES_INT_DONE_CIPHER (1u << 3)
#define KEYMGR_AES_INT_DONE_KEYEXPANSION (1u << 4)
#define KEYMGR_AES_INT_DONE_WIPE_SECRETS (1u << 5)

#define KEYMGR_AES_WFIFO_DEPTH 16u
#define KEYMGR_AES_RFIFO_DEPTH 32u

#define KEYMGR_SHA_EN_SHA1 (1u << 1)
#define KEYMGR_SHA_EN_LIVESTREAM (1u << 4)
#define KEYMGR_SHA_EN_HMAC (1u << 5)
#define KEYMGR_SHA_BLOCK_SIZE 64u
#define KEYMGR_SHA_EN_INT_DONE (1u << 16)
#define KEYMGR_SHA_TRIG_GO (1u << 0)
#define KEYMGR_SHA_TRIG_RESET (1u << 1)
#define KEYMGR_SHA_TRIG_STOP (1u << 3)
#define KEYMGR_SHA_USE_CERT_ENABLE (1u << 6)

DEFINE_BASIC_REGISTER(rwr_vld)
DEFINE_BASIC_REGISTER(rwr_lock)
DEFINE_BASIC_REGISTER(fwr_vld)
DEFINE_BASIC_REGISTER(fw_major_version)
DEFINE_BASIC_REGISTER(fwr_lock)
DEFINE_BASIC_REGISTER(hkey_err_flags)
DEFINE_BASIC_REGISTER(cert_revoke_ctrl0)
DEFINE_BASIC_REGISTER(cert_revoke_ctrl1)
DEFINE_BASIC_REGISTER(cert_revoke_ctrl2)
DEFINE_BASIC_REGISTER(sha_sts)
DEFINE_BASIC_REGISTER(sha_itcr)
DEFINE_BASIC_REGISTER(sha_use_hidden_key)
DEFINE_BASIC_REGISTER(sha_execute_count_state)
DEFINE_BASIC_REGISTER(sha_execute_count_max)

static const BasicRegister keymgr_registers[] = {
  BASIC_REGISTER_ENTRY(REG_KEYMGR_RWR_VLD_OFFSET, rwr_vld),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_RWR_LOCK_OFFSET, rwr_lock),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_FWR_VLD_OFFSET, fwr_vld),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_FW_MAJOR_VERSION_OFFSET, fw_major_version),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_FWR_LOCK_OFFSET, fwr_lock),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_HKEY_ERR_FLAGS_OFFSET, hkey_err_flags),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_STS_OFFSET, sha_sts),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_ITCR_OFFSET, sha_itcr),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_USE_HIDDEN_KEY_OFFSET, sha_use_hidden_key),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_EXECUTE_COUNT_STATE_OFFSET, sha_execute_count_state),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_SHA_EXECUTE_COUNT_MAX_OFFSET, sha_execute_count_max),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_CERT_REVOKE_CTRL0_OFFSET, cert_revoke_ctrl0),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_CERT_REVOKE_CTRL1_OFFSET, cert_revoke_ctrl1),
  BASIC_REGISTER_ENTRY(REG_KEYMGR_CERT_REVOKE_CTRL2_OFFSET, cert_revoke_ctrl2),
};

static uint32_t hkey_rwr[REG_KEYMGR_HKEY_RWR_COUNT];
static uint32_t hkey_fwr[REG_KEYMGR_HKEY_FWR_COUNT];
static uint32_t hkey_frr[REG_KEYMGR_HKEY_FRR_COUNT];
static uint32_t sha_msglen_lo;
static uint32_t sha_msglen_hi;
static uint64_t sha_msglen;
static uint32_t sha_en;
static uint32_t sha_wr_en;
static bool sha_receiving;
static uint32_t sha_itop;
static uint32_t sha_sts_h[REG_KEYMGR_SHA_STS_H_COUNT];
static uint32_t sha_key_w[REG_KEYMGR_SHA_KEY_W_COUNT];
static uint32_t sha_use_cert;
static uint32_t sha_cert_override;
static uint32_t sha_rand_stall_ctl;
static Sha256Context sha_context;
static uint64_t sha_input_len;

static uint32_t aes_ctrl;
static uint32_t aes_key[REG_KEYMGR_AES_KEY_COUNT];
static uint32_t aes_ctr[REG_KEYMGR_AES_CTR_COUNT];
static uint32_t aes_rand_stall_ctl;
static uint32_t aes_execute_count_state;
static uint32_t aes_execute_count_max;
static uint32_t aes_int_enable;
static uint32_t aes_int_state;
static uint32_t aes_int_test;
static uint32_t aes_use_hidden_key;
static uint32_t gcm_h[REG_KEYMGR_GCM_H_COUNT];
static uint32_t gcm_mac[REG_KEYMGR_GCM_MAC_COUNT];
static uint32_t gcm_hash_in[REG_KEYMGR_GCM_HASH_IN_COUNT];
static uint32_t aes_wfifo[KEYMGR_AES_WFIFO_DEPTH];
static unsigned aes_wfifo_count;
static uint32_t aes_rfifo[KEYMGR_AES_RFIFO_DEPTH];
static unsigned aes_rfifo_head;
static unsigned aes_rfifo_count;
static bool aes_key_ready;
static AesContext aes_context;

static int hkey_index(uint64_t offset, uint32_t base, unsigned count) {
  uint64_t end = (uint64_t)base + (uint64_t)count * sizeof(uint32_t);

  if ((offset & 3u) != 0) return -1;
  if (offset < base || offset >= end) return -1;

  return (int)((offset - base) / sizeof(uint32_t));
}

static int sts_h_index(uint64_t offset) {
  uint64_t end = REG_KEYMGR_SHA_STS_H0_OFFSET +
                 (uint64_t)REG_KEYMGR_SHA_STS_H_COUNT * sizeof(uint32_t);

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

static void sha_hmac_key_block(uint8_t block[KEYMGR_SHA_BLOCK_SIZE], uint8_t pad) {
  unsigned i;

  for (i = 0; i < REG_KEYMGR_SHA_KEY_W_COUNT; i++) {
    block[i * 4u + 0] = (uint8_t)sha_key_w[i];
    block[i * 4u + 1] = (uint8_t)(sha_key_w[i] >> 8);
    block[i * 4u + 2] = (uint8_t)(sha_key_w[i] >> 16);
    block[i * 4u + 3] = (uint8_t)(sha_key_w[i] >> 24);
  }
  memset(&block[REG_KEYMGR_SHA_KEY_W_COUNT * 4u], 0,
         KEYMGR_SHA_BLOCK_SIZE - REG_KEYMGR_SHA_KEY_W_COUNT * 4u);
  for (i = 0; i < KEYMGR_SHA_BLOCK_SIZE; i++) block[i] ^= pad;
}

static void sha_hash(void) {
  uint8_t digest[32];
  unsigned i;

  if ((sha_en & (KEYMGR_SHA_EN_HMAC | KEYMGR_SHA_EN_SHA1)) == KEYMGR_SHA_EN_HMAC) {
    uint8_t inner[32];
    uint8_t block[KEYMGR_SHA_BLOCK_SIZE];
    Sha256Context outer;

    sha256_final(&sha_context, inner);
    sha256_init(&outer);
    sha_hmac_key_block(block, 0x5C);
    sha256_update(&outer, block, sizeof(block));
    sha256_update(&outer, inner, sizeof(inner));
    sha256_final(&outer, digest);
  } else {
    sha256_final(&sha_context, digest);
  }

  for (i = 0; i < REG_KEYMGR_SHA_STS_H_COUNT; i++) {
    sha_sts_h[i] = (uint32_t)digest[4 * i] | ((uint32_t)digest[4 * i + 1] << 8) |
                   ((uint32_t)digest[4 * i + 2] << 16) |
                   ((uint32_t)digest[4 * i + 3] << 24);
  }

  if ((sha_en & KEYMGR_SHA_EN_INT_DONE) != 0) sha_itop = 1;
  sha_receiving = false;

  LOG_VBS("KEYMGR: SHA complete (%llu bytes)\n", (unsigned long long)sha_input_len);
  sha_input_len = 0;
}

static void sha_input_write(uint64_t value, unsigned size) {
  uint8_t bytes[4];
  bool oneshot = (sha_en & KEYMGR_SHA_EN_LIVESTREAM) == 0;
  unsigned i, count;

  if (!sha_receiving) return;
  if (size != 1 && size != 4) return;

  count = size == 4 ? 4 : 1;
  for (i = 0; i < count; i++) bytes[i] = (uint8_t)(value >> (8 * i));

  if (oneshot && sha_msglen != 0 && sha_input_len < sha_msglen) {
    uint64_t remaining = sha_msglen - sha_input_len;

    if (remaining < count) count = (unsigned)remaining;
  }

  if (count == 0) return;

  sha256_update(&sha_context, bytes, count);
  sha_input_len += count;

  if (oneshot && sha_msglen != 0 && sha_input_len >= sha_msglen) sha_hash();
}


static void keymgr_aes_put_word(uint8_t *out, uint32_t word) {
  out[0] = (uint8_t)word;
  out[1] = (uint8_t)(word >> 8);
  out[2] = (uint8_t)(word >> 16);
  out[3] = (uint8_t)(word >> 24);
}

static uint32_t keymgr_aes_get_word(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

static unsigned keymgr_aes_key_bytes(void) {
  switch (KEYMGR_AES_KEYSIZE(aes_ctrl)) {
    case 0:
      return 16;
    case 1:
      return 24;
    case 2:
      return 32;
    default:
      return 0;
  }
}

static void keymgr_aes_wipe(void) {
  memset(aes_key, 0, sizeof(aes_key));
  memset(aes_ctr, 0, sizeof(aes_ctr));
  memset(gcm_h, 0, sizeof(gcm_h));
  memset(gcm_mac, 0, sizeof(gcm_mac));
  memset(gcm_hash_in, 0, sizeof(gcm_hash_in));
  memset(aes_wfifo, 0, sizeof(aes_wfifo));
  memset(aes_rfifo, 0, sizeof(aes_rfifo));
  memset(&aes_context, 0, sizeof(aes_context));
  aes_wfifo_count = 0;
  aes_rfifo_head = 0;
  aes_rfifo_count = 0;
  aes_key_ready = false;
}

static void keymgr_aes_key_expand(void) {
  uint8_t raw[AES_MAX_KEY_SIZE];
  unsigned key_bytes = keymgr_aes_key_bytes();
  unsigned i;

  memset(raw, 0, sizeof(raw));
  aes_key_ready = false;

  if (key_bytes == 0) {
    aes_int_state |= KEYMGR_AES_INT_DONE_KEYEXPANSION;
    return;
  }

  if ((aes_use_hidden_key & KEYMGR_AES_USE_HIDDEN_KEY_ENABLE) == 0) {
    for (i = 0; i < key_bytes / 4u; i++) keymgr_aes_put_word(&raw[i * 4u], aes_key[i]);
  }

  aes_key_ready = aes_init(&aes_context, raw, key_bytes);
  aes_int_state |= KEYMGR_AES_INT_DONE_KEYEXPANSION;
  LOG_VBS("KEYMGR: AES key expansion (%u bytes) ready=%u\n", key_bytes, aes_key_ready);
}

static void keymgr_aes_ctr_increment(void) {
  unsigned i;

  if ((aes_ctrl & KEYMGR_AES_CTRL_CTR_BIG_ENDIAN) == 0) {
    aes_ctr[3]++;
    return;
  }

  for (i = 4; i > 0; i--) {
    uint32_t value = aes_ctr[i - 1];
    uint32_t swapped = __builtin_bswap32(value) + 1u;

    aes_ctr[i - 1] = __builtin_bswap32(swapped);
    if (swapped != 0) break;
  }
}

static void keymgr_aes_push_block(const uint8_t block[AES_BLOCK_SIZE]) {
  unsigned i;

  if (aes_rfifo_count + 4u > KEYMGR_AES_RFIFO_DEPTH) {
    aes_int_state |= KEYMGR_AES_INT_RFIFO_OVERFLOW;
    return;
  }

  for (i = 0; i < 4; i++) {
    unsigned slot = (aes_rfifo_head + aes_rfifo_count) % KEYMGR_AES_RFIFO_DEPTH;

    aes_rfifo[slot] = keymgr_aes_get_word(&block[i * 4u]);
    aes_rfifo_count++;
  }
}

static void keymgr_aes_cipher_block(const uint8_t in[AES_BLOCK_SIZE],
                                    uint8_t out[AES_BLOCK_SIZE]) {
  unsigned mode = KEYMGR_AES_MODE(aes_ctrl);
  bool encrypt = (aes_ctrl & KEYMGR_AES_CTRL_ENC_MODE) != 0;
  uint8_t iv[AES_BLOCK_SIZE];
  uint8_t buffer[AES_BLOCK_SIZE];
  unsigned i;

  switch (mode) {
    case KEYMGR_AES_MODE_ECB:
      if (encrypt) {
        aes_encrypt_block(&aes_context, in, out);
      } else {
        aes_decrypt_block(&aes_context, in, out);
      }
      return;
    case KEYMGR_AES_MODE_CTR:
      for (i = 0; i < 4; i++) keymgr_aes_put_word(&iv[i * 4u], aes_ctr[i]);
      aes_encrypt_block(&aes_context, iv, buffer);
      for (i = 0; i < AES_BLOCK_SIZE; i++) out[i] = in[i] ^ buffer[i];
      keymgr_aes_ctr_increment();
      return;
    case KEYMGR_AES_MODE_CBC:
      for (i = 0; i < 4; i++) keymgr_aes_put_word(&iv[i * 4u], aes_ctr[i]);
      if (encrypt) {
        for (i = 0; i < AES_BLOCK_SIZE; i++) buffer[i] = in[i] ^ iv[i];
        aes_encrypt_block(&aes_context, buffer, out);
      } else {
        aes_decrypt_block(&aes_context, in, out);
        for (i = 0; i < AES_BLOCK_SIZE; i++) out[i] ^= iv[i];
      }
      for (i = 0; i < 4; i++) {
        aes_ctr[i] = keymgr_aes_get_word(encrypt ? &out[i * 4u] : &in[i * 4u]);
      }
      return;
    default:
      return;
  }
}

static void keymgr_aes_process(void) {
  uint8_t block[AES_BLOCK_SIZE];
  uint8_t result[AES_BLOCK_SIZE];

  if ((aes_ctrl & KEYMGR_AES_CTRL_ENABLE) == 0) return;
  if (!aes_key_ready) return;
  if (KEYMGR_AES_MODE(aes_ctrl) == KEYMGR_AES_MODE_GCM) return;

  while (aes_wfifo_count >= 4u) {
    unsigned i;
    unsigned head = 0;

    for (i = 0; i < 4; i++) keymgr_aes_put_word(&block[i * 4u], aes_wfifo[head + i]);
    aes_wfifo_count -= 4u;
    for (i = 0; i < aes_wfifo_count; i++) aes_wfifo[i] = aes_wfifo[i + 4u];

    keymgr_aes_cipher_block(block, result);
    keymgr_aes_push_block(result);
    aes_int_state |= KEYMGR_AES_INT_DONE_CIPHER;
    aes_execute_count_state++;
  }
}

static void keymgr_gcm_multiply(void) {
  uint8_t mac[AES_BLOCK_SIZE];
  uint8_t h[AES_BLOCK_SIZE];
  uint8_t result[AES_BLOCK_SIZE];
  unsigned i;
  unsigned bit;

  for (i = 0; i < 4; i++) {
    keymgr_aes_put_word(&h[i * 4u], __builtin_bswap32(gcm_h[i]));
    keymgr_aes_put_word(&mac[i * 4u], __builtin_bswap32(gcm_mac[i] ^ gcm_hash_in[i]));
  }
  memset(result, 0, sizeof(result));

  for (bit = 0; bit < 128; bit++) {
    uint8_t carry = 0;

    if ((mac[bit / 8u] & (0x80u >> (bit % 8u))) != 0) {
      for (i = 0; i < AES_BLOCK_SIZE; i++) result[i] ^= h[i];
    }

    for (i = 0; i < AES_BLOCK_SIZE; i++) {
      uint8_t next = h[i] & 1u;

      h[i] = (uint8_t)((h[i] >> 1) | (carry << 7));
      carry = next;
    }
    if (carry != 0) h[0] ^= 0xE1u;
  }

  for (i = 0; i < 4; i++) gcm_mac[i] = __builtin_bswap32(keymgr_aes_get_word(&result[i * 4u]));
}

static bool keymgr_aes_read(uint64_t offset, unsigned size, uint64_t *value) {
  int index;

  index = hkey_index(offset, REG_KEYMGR_AES_KEY0_OFFSET, REG_KEYMGR_AES_KEY_COUNT);
  if (index >= 0) {
    *value = aes_key[index];
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_AES_CTR0_OFFSET, REG_KEYMGR_AES_CTR_COUNT);
  if (index >= 0) {
    *value = aes_ctr[index];
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_H0_OFFSET, REG_KEYMGR_GCM_H_COUNT);
  if (index >= 0) {
    *value = gcm_h[index];
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_MAC0_OFFSET, REG_KEYMGR_GCM_MAC_COUNT);
  if (index >= 0) {
    *value = gcm_mac[index];
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_HASH_IN0_OFFSET, REG_KEYMGR_GCM_HASH_IN_COUNT);
  if (index >= 0) {
    *value = gcm_hash_in[index];
    return true;
  }

  switch (offset) {
    case REG_KEYMGR_AES_CTRL_OFFSET:
      *value = aes_ctrl;
      return true;
    case REG_KEYMGR_AES_WFIFO_DATA_OFFSET:
      *value = 0;
      return true;
    case REG_KEYMGR_AES_RFIFO_DATA_OFFSET:
      if (aes_rfifo_count == 0) {
        aes_int_state |= KEYMGR_AES_INT_RFIFO_UNDERFLOW;
        *value = 0;
        return true;
      }
      *value = aes_rfifo[aes_rfifo_head];
      aes_rfifo_head = (aes_rfifo_head + 1u) % KEYMGR_AES_RFIFO_DEPTH;
      aes_rfifo_count--;
      return true;
    case REG_KEYMGR_AES_KEY_START_OFFSET:
      *value = 0;
      return true;
    case REG_KEYMGR_AES_RAND_STALL_CTL_OFFSET:
      *value = aes_rand_stall_ctl;
      return true;
    case REG_KEYMGR_AES_WFIFO_LEVEL_OFFSET:
      *value = aes_wfifo_count;
      return true;
    case REG_KEYMGR_AES_WFIFO_FULL_OFFSET:
      *value = aes_wfifo_count >= KEYMGR_AES_WFIFO_DEPTH ? 1u : 0u;
      return true;
    case REG_KEYMGR_AES_RFIFO_LEVEL_OFFSET:
      *value = aes_rfifo_count;
      return true;
    case REG_KEYMGR_AES_RFIFO_EMPTY_OFFSET:
      *value = aes_rfifo_count == 0 ? 1u : 0u;
      return true;
    case REG_KEYMGR_AES_EXECUTE_COUNT_STATE_OFFSET:
      *value = aes_execute_count_state;
      return true;
    case REG_KEYMGR_AES_EXECUTE_COUNT_MAX_OFFSET:
      *value = aes_execute_count_max;
      return true;
    case REG_KEYMGR_GCM_DO_ACC_OFFSET:
      *value = 0;
      return true;
    case REG_KEYMGR_AES_WIPE_SECRETS_OFFSET:
      *value = 0;
      return true;
    case REG_KEYMGR_AES_INT_ENABLE_OFFSET:
      *value = aes_int_enable;
      return true;
    case REG_KEYMGR_AES_INT_STATE_OFFSET:
      *value = aes_int_state;
      return true;
    case REG_KEYMGR_AES_INT_TEST_OFFSET:
      *value = aes_int_test;
      return true;
    case REG_KEYMGR_AES_USE_HIDDEN_KEY_OFFSET:
      *value = aes_use_hidden_key;
      return true;
    default:
      (void)size;
      return false;
  }
}

static bool keymgr_aes_write(uint64_t offset, unsigned size, uint64_t value) {
  uint32_t word = (uint32_t)value;
  int index;

  index = hkey_index(offset, REG_KEYMGR_AES_KEY0_OFFSET, REG_KEYMGR_AES_KEY_COUNT);
  if (index >= 0) {
    aes_key[index] = word;
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_AES_CTR0_OFFSET, REG_KEYMGR_AES_CTR_COUNT);
  if (index >= 0) {
    aes_ctr[index] = word;
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_H0_OFFSET, REG_KEYMGR_GCM_H_COUNT);
  if (index >= 0) {
    gcm_h[index] = word;
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_MAC0_OFFSET, REG_KEYMGR_GCM_MAC_COUNT);
  if (index >= 0) {
    gcm_mac[index] = word;
    return true;
  }

  index = hkey_index(offset, REG_KEYMGR_GCM_HASH_IN0_OFFSET, REG_KEYMGR_GCM_HASH_IN_COUNT);
  if (index >= 0) {
    gcm_hash_in[index] = word;
    return true;
  }

  switch (offset) {
    case REG_KEYMGR_AES_CTRL_OFFSET:
      if ((word & KEYMGR_AES_CTRL_RESET) != 0) {
        keymgr_aes_wipe();
        aes_ctrl = 0;
        return true;
      }
      aes_ctrl = word & 0xFFu;
      keymgr_aes_process();
      return true;
    case REG_KEYMGR_AES_WFIFO_DATA_OFFSET:
      if (aes_wfifo_count >= KEYMGR_AES_WFIFO_DEPTH) {
        aes_int_state |= KEYMGR_AES_INT_WFIFO_OVERFLOW;
        return true;
      }
      aes_wfifo[aes_wfifo_count++] = word;
      keymgr_aes_process();
      return true;
    case REG_KEYMGR_AES_RFIFO_DATA_OFFSET:
      return true;
    case REG_KEYMGR_AES_KEY_START_OFFSET:
      if (word != 0) keymgr_aes_key_expand();
      return true;
    case REG_KEYMGR_AES_RAND_STALL_CTL_OFFSET:
      aes_rand_stall_ctl = word;
      return true;
    case REG_KEYMGR_AES_WFIFO_LEVEL_OFFSET:
    case REG_KEYMGR_AES_WFIFO_FULL_OFFSET:
    case REG_KEYMGR_AES_RFIFO_LEVEL_OFFSET:
    case REG_KEYMGR_AES_RFIFO_EMPTY_OFFSET:
      return true;
    case REG_KEYMGR_AES_EXECUTE_COUNT_STATE_OFFSET:
      aes_execute_count_state = word;
      return true;
    case REG_KEYMGR_AES_EXECUTE_COUNT_MAX_OFFSET:
      aes_execute_count_max = word;
      return true;
    case REG_KEYMGR_GCM_DO_ACC_OFFSET:
      if ((word & 1u) != 0) keymgr_gcm_multiply();
      return true;
    case REG_KEYMGR_AES_WIPE_SECRETS_OFFSET:
      if (word != 0) {
        keymgr_aes_wipe();
        aes_int_state |= KEYMGR_AES_INT_DONE_WIPE_SECRETS;
      }
      return true;
    case REG_KEYMGR_AES_INT_ENABLE_OFFSET:
      aes_int_enable = word;
      return true;
    case REG_KEYMGR_AES_INT_STATE_OFFSET:
      aes_int_state &= ~word;
      return true;
    case REG_KEYMGR_AES_INT_TEST_OFFSET:
      aes_int_test = word;
      aes_int_state |= word;
      return true;
    case REG_KEYMGR_AES_USE_HIDDEN_KEY_OFFSET:
      aes_use_hidden_key = word;
      return true;
    default:
      (void)size;
      return false;
  }
}

static uint64_t KEYMGR_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg =
      basic_register_find(keymgr_registers, ARRAY_LEN(keymgr_registers), offset);
  uint64_t aes_value;
  int index;

  if (keymgr_aes_read(offset, size, &aes_value)) return aes_value;

  if (reg != NULL) {
    LOG_VBS("KEYMGR: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_RWR0_OFFSET, REG_KEYMGR_HKEY_RWR_COUNT);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read HKEY_RWR%d (size=%u) -> 0x%08x\n", index, size, hkey_rwr[index]);
    return hkey_rwr[index];
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_FWR0_OFFSET, REG_KEYMGR_HKEY_FWR_COUNT);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read HKEY_FWR%d (size=%u) -> 0x%08x\n", index, size, hkey_fwr[index]);
    return hkey_fwr[index];
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_FRR0_OFFSET, REG_KEYMGR_HKEY_FRR_COUNT);
  if (index >= 0) {
    LOG_VBS("KEYMGR: read HKEY_FRR%d (size=%u) -> 0x%08x\n", index, size, hkey_frr[index]);
    return hkey_frr[index];
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

  if (keymgr_aes_write(offset, size, value)) return;

  if (offset == REG_KEYMGR_CERT_REVOKE_CTRL0_OFFSET ||
      offset == REG_KEYMGR_CERT_REVOKE_CTRL1_OFFSET ||
      offset == REG_KEYMGR_CERT_REVOKE_CTRL2_OFFSET) {
    LOG_VBS("KEYMGR: ignored write to cert revoke control\n");
    return;
  }

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_RWR0_OFFSET, REG_KEYMGR_HKEY_RWR_COUNT);
  if (index >= 0) {
    hkey_rwr[index] = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote HKEY_RWR%d (size=%u) <- 0x%08x\n", index, size, hkey_rwr[index]);
    return;
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_FWR0_OFFSET, REG_KEYMGR_HKEY_FWR_COUNT);
  if (index >= 0) {
    hkey_fwr[index] = (uint32_t)value;
    LOG_VBS("KEYMGR: wrote HKEY_FWR%d (size=%u) <- 0x%08x\n", index, size, hkey_fwr[index]);
    return;
  }

  index = hkey_index(offset, REG_KEYMGR_HKEY_FRR0_OFFSET, REG_KEYMGR_HKEY_FRR_COUNT);
  if (index >= 0) {
    LOG_VBS("KEYMGR: ignored write to HKEY_FRR%d\n", index);
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
      LOG_VBS("KEYMGR: wrote SHA_CFG_MSGLEN_LO <- 0x%08x (receiving=%u)\n", (uint32_t)value,
              sha_receiving);
      if (sha_receiving) return;
      sha_msglen_lo = (uint32_t)value;
      sha_msglen = ((uint64_t)sha_msglen_hi << 32) | sha_msglen_lo;
      return;
    case REG_KEYMGR_SHA_CFG_MSGLEN_HI_OFFSET:
      LOG_VBS("KEYMGR: wrote SHA_CFG_MSGLEN_HI <- 0x%08x (receiving=%u)\n", (uint32_t)value,
              sha_receiving);
      if (sha_receiving) return;
      sha_msglen_hi = (uint32_t)value;
      sha_msglen = ((uint64_t)sha_msglen_hi << 32) | sha_msglen_lo;
      return;
    case REG_KEYMGR_SHA_CFG_EN_OFFSET:
      LOG_VBS("KEYMGR: wrote SHA_CFG_EN <- 0x%08x (receiving=%u)\n", (uint32_t)value,
              sha_receiving);
      if (sha_receiving) return;
      sha_en = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_CFG_WR_EN_OFFSET:
      sha_wr_en = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_TRIG_OFFSET:
      LOG_VBS("KEYMGR: wrote SHA_TRIG <- 0x%08x (receiving=%u msglen=%llu)\n", (uint32_t)value,
              sha_receiving, (unsigned long long)sha_msglen);
      if (((uint32_t)value & KEYMGR_SHA_TRIG_GO) != 0) {
        if ((sha_use_cert & KEYMGR_SHA_USE_CERT_ENABLE) != 0) {
          sha_use_cert &= ~KEYMGR_SHA_USE_CERT_ENABLE;
          sha_receiving = false;
          sha_itop = 1;
          LOG_VBS("KEYMGR: SHA cert use unsupported, completing\n");
          return;
        }

        sha256_init(&sha_context);
        sha_input_len = 0;
        sha_itop = 0;
        sha_receiving = true;
        if ((sha_en & (KEYMGR_SHA_EN_HMAC | KEYMGR_SHA_EN_SHA1)) == KEYMGR_SHA_EN_HMAC) {
          uint8_t block[KEYMGR_SHA_BLOCK_SIZE];

          sha_hmac_key_block(block, 0x36);
          sha256_update(&sha_context, block, sizeof(block));
        }
      } else if (((uint32_t)value & KEYMGR_SHA_TRIG_RESET) != 0) {
        sha_receiving = false;
        sha_itop = 0;
        sha_input_len = 0;
      } else if (((uint32_t)value & KEYMGR_SHA_TRIG_STOP) != 0) {
        if ((sha_en & KEYMGR_SHA_EN_LIVESTREAM) != 0 && sha_receiving) sha_hash();
      }
      return;
    case REG_KEYMGR_SHA_INPUT_FIFO_OFFSET:
      sha_input_write(value, size);
      return;
    case REG_KEYMGR_SHA_ITOP_OFFSET:
      sha_itop = (uint32_t)value;
      return;
    case REG_KEYMGR_SHA_USE_CERT_OFFSET:
      LOG_VBS("KEYMGR: wrote SHA_USE_CERT <- 0x%08x\n", (uint32_t)value);
      sha_use_cert = (uint32_t)value & 0x7Fu;
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
  memset(hkey_fwr, 0, sizeof(hkey_fwr));
  memset(hkey_frr, 0, sizeof(hkey_frr));
  memset(sha_sts_h, 0, sizeof(sha_sts_h));
  memset(sha_key_w, 0, sizeof(sha_key_w));

  rwr_vld = 0;
  rwr_lock = 0;
  fwr_vld = 0;
  fw_major_version = 0;
  fwr_lock = 0;
  hkey_err_flags = 0;
  sha_sts = 0;
  sha_itcr = 0;
  sha_use_hidden_key = 0;
  sha_execute_count_state = 0;
  sha_execute_count_max = 0;
  cert_revoke_ctrl0 = 0xA8028A82;
  cert_revoke_ctrl1 = 0xAAAAAAAA;
  cert_revoke_ctrl2 = 0xAAAA;
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

  memset(aes_key, 0, sizeof(aes_key));
  memset(aes_ctr, 0, sizeof(aes_ctr));
  memset(gcm_h, 0, sizeof(gcm_h));
  memset(gcm_mac, 0, sizeof(gcm_mac));
  memset(gcm_hash_in, 0, sizeof(gcm_hash_in));
  memset(aes_wfifo, 0, sizeof(aes_wfifo));
  memset(aes_rfifo, 0, sizeof(aes_rfifo));
  memset(&aes_context, 0, sizeof(aes_context));
  aes_ctrl = 0;
  aes_rand_stall_ctl = 0;
  aes_execute_count_state = 0;
  aes_execute_count_max = 0;
  aes_int_enable = 0;
  aes_int_state = 0;
  aes_int_test = 0;
  aes_use_hidden_key = 0;
  aes_wfifo_count = 0;
  aes_rfifo_head = 0;
  aes_rfifo_count = 0;
  aes_key_ready = false;

  return component_register_threaded("KEYMGR", NULL, KEYMGR_Read, KEYMGR_Write);
}
