#include <common/sha256.h>

#include <string.h>

static const uint32_t sha256_k[64] = {
  0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
  0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
  0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
  0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
  0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
  0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
  0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
  0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};

static uint32_t rotr(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32 - bits));
}

static void transform(Sha256Context *ctx, const uint8_t block[64]) {
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  unsigned i;

  for (i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
           ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
  }

  for (i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);

    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64; i++) {
    uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + s1 + ch + sha256_k[i] + w[i];
    uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void sha256_init(Sha256Context *ctx) {
  ctx->state[0] = 0x6A09E667;
  ctx->state[1] = 0xBB67AE85;
  ctx->state[2] = 0x3C6EF372;
  ctx->state[3] = 0xA54FF53A;
  ctx->state[4] = 0x510E527F;
  ctx->state[5] = 0x9B05688C;
  ctx->state[6] = 0x1F83D9AB;
  ctx->state[7] = 0x5BE0CD19;
  ctx->bitlen = 0;
  ctx->datalen = 0;
}

void sha256_update(Sha256Context *ctx, const uint8_t *data, size_t len) {
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

void sha256_final(Sha256Context *ctx, uint8_t out[32]) {
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

  for (i = 0; i < 8; i++) {
    out[4 * i] = (uint8_t)(ctx->state[i] >> 24);
    out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
    out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
    out[4 * i + 3] = (uint8_t)ctx->state[i];
  }
}
