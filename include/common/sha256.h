#ifndef COMMON_SHA256_H
#define COMMON_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t data[64];
  size_t datalen;
} Sha256Context;

void sha256_init(Sha256Context *ctx);
void sha256_update(Sha256Context *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Context *ctx, uint8_t out[32]);

#endif /* COMMON_SHA256_H */
