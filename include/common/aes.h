#ifndef COMMON_AES_H
#define COMMON_AES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AES_BLOCK_SIZE 16
#define AES_MAX_KEY_SIZE 32

typedef struct {
  uint8_t round_keys[240];
  unsigned rounds;
  unsigned key_words;
} AesContext;

bool aes_init(AesContext *ctx, const uint8_t *key, size_t key_len);
void aes_encrypt_block(const AesContext *ctx, const uint8_t in[AES_BLOCK_SIZE],
                       uint8_t out[AES_BLOCK_SIZE]);
void aes_decrypt_block(const AesContext *ctx, const uint8_t in[AES_BLOCK_SIZE],
                       uint8_t out[AES_BLOCK_SIZE]);

#endif /* COMMON_AES_H */
