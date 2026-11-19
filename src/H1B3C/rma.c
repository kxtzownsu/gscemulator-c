#include <H1B3C/rma.h>
#include <H1B3C/memory.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <common/common.h>
#include <common/logger.h>
#include <unicorn/unicorn.h>

#define RMA_CHALLENGE_MAX 128
#define RMA_AUTHCODE_CHARS 8
#define RMA_SCAN_WINDOW 0x200
#define RMA_AUTHCODE_ATTEMPTS 16

static const char rma_base32[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static pthread_mutex_t rma_lock = PTHREAD_MUTEX_INITIALIZER;
static char rma_challenge[RMA_CHALLENGE_MAX];
static size_t rma_challenge_len;
static _Atomic bool rma_pending;
static _Atomic unsigned rma_attempts;
static uint8_t rma_sram[SRAM_SIZE];

static bool rma_base32_byte(uint8_t byte);

bool RMA_ChallengeSeen(const char *challenge, size_t length) {
  size_t i;

  if (challenge == NULL || length < 64 || length >= sizeof(rma_challenge)) return false;

  for (i = 0; i < length; i++) {
    if (!rma_base32_byte((uint8_t)challenge[i])) return false;
  }

  pthread_mutex_lock(&rma_lock);
  memcpy(rma_challenge, challenge, length);
  rma_challenge_len = length;
  pthread_mutex_unlock(&rma_lock);

  atomic_store(&rma_attempts, 0);
  atomic_store(&rma_pending, true);
  return true;
}

static bool rma_base32_byte(uint8_t byte) {
  return byte != 0 && strchr(rma_base32, byte) != NULL;
}

static bool rma_authcode_at(size_t position, size_t size) {
  unsigned i;

  if (position + RMA_AUTHCODE_CHARS > size) return false;
  if (position > 0 && rma_base32_byte(rma_sram[position - 1])) return false;
  if (position + RMA_AUTHCODE_CHARS < size &&
      rma_base32_byte(rma_sram[position + RMA_AUTHCODE_CHARS])) {
    return false;
  }

  for (i = 0; i < RMA_AUTHCODE_CHARS; i++) {
    if (!rma_base32_byte(rma_sram[position + i])) return false;
  }

  return true;
}

static bool rma_find_authcode(size_t challenge_position, size_t challenge_length,
                              char code[RMA_AUTHCODE_CHARS + 1]) {
  size_t start;
  size_t end;
  size_t position;

  start = challenge_position + challenge_length;
  end = start + RMA_SCAN_WINDOW;
  if (end > SRAM_SIZE) end = SRAM_SIZE;

  for (position = start; position < end; position++) {
    if (!rma_authcode_at(position, SRAM_SIZE)) continue;

    memcpy(code, rma_sram + position, RMA_AUTHCODE_CHARS);
    code[RMA_AUTHCODE_CHARS] = '\0';
    return true;
  }

  start = challenge_position > RMA_SCAN_WINDOW ? challenge_position - RMA_SCAN_WINDOW : 0;
  end = challenge_position;

  for (position = start; position < end; position++) {
    if (!rma_authcode_at(position, SRAM_SIZE)) continue;

    memcpy(code, rma_sram + position, RMA_AUTHCODE_CHARS);
    code[RMA_AUTHCODE_CHARS] = '\0';
    return true;
  }

  return false;
}

void RMA_Poll(void) {
  char challenge[RMA_CHALLENGE_MAX];
  char code[RMA_AUTHCODE_CHARS + 1];
  size_t challenge_len;
  size_t position;

  if (!atomic_load(&rma_pending)) return;

  pthread_mutex_lock(&rma_lock);
  challenge_len = rma_challenge_len;
  memcpy(challenge, rma_challenge, challenge_len);
  pthread_mutex_unlock(&rma_lock);

  if (ctx.uc == NULL || challenge_len == 0 ||
      uc_mem_read(ctx.uc, SRAM_BASE, rma_sram, sizeof(rma_sram)) != UC_ERR_OK) {
    atomic_store(&rma_pending, false);
    return;
  }

  for (position = 0; position + challenge_len <= SRAM_SIZE; position++) {
    if (memcmp(rma_sram + position, challenge, challenge_len) != 0) continue;
    if (!rma_find_authcode(position, challenge_len, code)) continue;

    atomic_store(&rma_pending, false);
    LOG("RMA auth code: %s\n", code);
    return;
  }

  if (atomic_fetch_add(&rma_attempts, 1) >= RMA_AUTHCODE_ATTEMPTS) {
    atomic_store(&rma_pending, false);
    LOG_ERR("RMA: could not locate the auth code in SRAM\n");
  }
}
