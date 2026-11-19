#include <H1B3C/rma.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/common.h>
#include <common/logger.h>
#include <unicorn/unicorn.h>

#define RMA_AUTHCODE_ADDRESS 0x1D199u
#define RMA_AUTHCODE_CHARS 8
#define RMA_AUTHCODE_ATTEMPTS 1000

static const char rma_base32[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static _Atomic bool rma_pending;
static _Atomic unsigned rma_attempts;

void RMA_ChallengeSeen(void) {
  atomic_store(&rma_attempts, 0);
  atomic_store(&rma_pending, true);
}

static bool rma_authcode_valid(const uint8_t *code) {
  unsigned i;

  for (i = 0; i < RMA_AUTHCODE_CHARS; i++) {
    if (code[i] == 0 || strchr(rma_base32, code[i]) == NULL) return false;
  }

  return true;
}

void RMA_Poll(void) {
  uint8_t code[RMA_AUTHCODE_CHARS];
  char text[RMA_AUTHCODE_CHARS + 1];

  if (!atomic_load(&rma_pending)) return;

  if (ctx.uc == NULL ||
      uc_mem_read(ctx.uc, RMA_AUTHCODE_ADDRESS, code, sizeof(code)) != UC_ERR_OK) {
    atomic_store(&rma_pending, false);
    return;
  }

  if (!rma_authcode_valid(code)) {
    if (atomic_fetch_add(&rma_attempts, 1) >= RMA_AUTHCODE_ATTEMPTS) {
      atomic_store(&rma_pending, false);
    }
    return;
  }

  memcpy(text, code, sizeof(code));
  text[sizeof(code)] = '\0';
  atomic_store(&rma_pending, false);
  LOG("RMA auth code: %s\n", text);
}
