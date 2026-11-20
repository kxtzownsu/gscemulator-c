#include <common/sha1.h>

static uint32_t rol(uint32_t value, unsigned bits) {
  return (value << bits) | (value >> (32 - bits));
}

static void transform(Sha1Context *ctx, const uint8_t block[64]) {
  uint32_t w[80];
  uint32_t a, b, c, d, e;
  unsigned i;

  for (i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
           ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
  }

  for (i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];

  for (i = 0; i < 80; i++) {
    uint32_t f;
    uint32_t k;
    uint32_t temp;

    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }

    temp = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = temp;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

void sha1_init(Sha1Context *ctx) {
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
  ctx->bitlen = 0;
  ctx->datalen = 0;
}

void sha1_update(Sha1Context *ctx, const uint8_t *data, size_t len) {
  size_t i;

  for (i = 0; i < len; i++) {
    ctx->data[ctx->datalen++] = data[i];

    if (ctx->datalen == 64) {
      transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

void sha1_final(Sha1Context *ctx, uint8_t out[20]) {
  uint64_t bitlen = ctx->bitlen + (uint64_t)ctx->datalen * 8;
  size_t i = ctx->datalen;

  ctx->data[i++] = 0x80;

  if (i > 56) {
    while (i < 64) ctx->data[i++] = 0;
    transform(ctx, ctx->data);
    i = 0;
  }

  while (i < 56) ctx->data[i++] = 0;

  for (i = 0; i < 8; i++) {
    ctx->data[56 + i] = (uint8_t)(bitlen >> (56 - 8 * i));
  }

  transform(ctx, ctx->data);

  for (i = 0; i < 5; i++) {
    out[4 * i] = (uint8_t)(ctx->state[i] >> 24);
    out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
    out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
    out[4 * i + 3] = (uint8_t)ctx->state[i];
  }
}
