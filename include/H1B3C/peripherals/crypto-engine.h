#ifndef H1B3C_PERIPHERALS_CRYPTO_ENGINE_H
#define H1B3C_PERIPHERALS_CRYPTO_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define CRYPTO_ENGINE_IMEM_WORDS 1024u
#define CRYPTO_ENGINE_DMEM_WORDS 1024u

typedef enum {
  CRYPTO_ENGINE_OK = 0,
  CRYPTO_ENGINE_ERR_INVALID_OPCODE,
  CRYPTO_ENGINE_ERR_INVALID_JUMP,
  CRYPTO_ENGINE_ERR_DMEM_INDEX,
  CRYPTO_ENGINE_ERR_IMEM_INDEX,
  CRYPTO_ENGINE_ERR_LOOP_STACK_OVERFLOW,
  CRYPTO_ENGINE_ERR_CALL_STACK_OVERFLOW,
  CRYPTO_ENGINE_ERR_DIVIDE_BY_ZERO,
  CRYPTO_ENGINE_ERR_INVALID_SPECIAL_REGISTER,
  CRYPTO_ENGINE_ERR_STEP_LIMIT
} CryptoEngineStatus;

void crypto_engine_reset(bool clear_dmem, bool clear_imem);
void crypto_engine_set_pc(uint32_t pc);
void crypto_engine_set_dmem(uint32_t index, uint32_t value);
uint32_t crypto_engine_get_dmem(uint32_t index);
void crypto_engine_set_imem(uint32_t index, uint32_t value);
uint32_t crypto_engine_get_imem(uint32_t index);
CryptoEngineStatus crypto_engine_run(void);
CryptoEngineStatus crypto_engine_run_batch(uint32_t max_steps, bool *done);
const char *crypto_engine_status_string(CryptoEngineStatus status);

#endif /* H1B3C_PERIPHERALS_CRYPTO_ENGINE_H */
