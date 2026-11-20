#ifndef COMMON_SHA1_H
#define COMMON_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t state[5];
  uint64_t bitlen;
  uint8_t data[64];
  size_t datalen;
} Sha1Context;

void sha1_init(Sha1Context *ctx);
void sha1_update(Sha1Context *ctx, const uint8_t *data, size_t len);
void sha1_final(Sha1Context *ctx, uint8_t out[20]);

#endif /* COMMON_SHA1_H */
