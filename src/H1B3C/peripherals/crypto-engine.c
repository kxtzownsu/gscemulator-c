#include <H1B3C/peripherals/crypto-engine.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ENGINE_LANES 8u
#define ENGINE_DMEM_WIDE_WORDS (CRYPTO_ENGINE_DMEM_WORDS / ENGINE_LANES)
#define ENGINE_LAST_IMEM (CRYPTO_ENGINE_IMEM_WORDS - 1u)
#define ENGINE_NOP 0xFC000000u
#define ENGINE_LOOP_STACK 16u
#define ENGINE_CALL_STACK 16u
#define ENGINE_MAX_STEPS 33554432u

typedef struct {
  uint32_t word[ENGINE_LANES];
} Wide;

typedef struct {
  int64_t count;
  uint32_t end;
  uint32_t start;
} LoopFrame;

static Wide registers[32];
static Wide dmem[ENGINE_DMEM_WIDE_WORDS];
static Wide rfp;
static Wide dmp;
static Wide lc;
static Wide rnd;
static Wide modulus;

static uint32_t imem[CRYPTO_ENGINE_IMEM_WORDS];
static uint32_t decoded[CRYPTO_ENGINE_IMEM_WORDS];

static uint32_t pc;
static uint32_t call_stack[ENGINE_CALL_STACK];
static unsigned call_depth;
static LoopFrame loop_stack[ENGINE_LOOP_STACK];
static unsigned loop_depth;

static unsigned flag_c;
static unsigned flag_l;
static unsigned flag_m;
static unsigned flag_z;
static unsigned flag_xc;
static unsigned flag_xl;
static unsigned flag_xm;
static unsigned flag_xz;
static unsigned finished;

static void wide_zero(Wide *value) {
  memset(value, 0, sizeof(*value));
}

static void wide_copy(Wide *destination, const Wide *source) {
  memcpy(destination, source, sizeof(*destination));
}

static int wide_is_zero(const Wide *value) {
  uint32_t aggregate = 0;
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) aggregate |= value->word[i];

  return aggregate == 0;
}

static int wide_compare(const Wide *left, const Wide *right) {
  unsigned i = ENGINE_LANES;

  while (i != 0) {
    i--;
    if (left->word[i] < right->word[i]) return -1;
    if (left->word[i] > right->word[i]) return 1;
  }

  return 0;
}

static uint32_t wide_add(Wide *result, const Wide *left, const Wide *right, uint32_t carry) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) {
    uint64_t sum = (uint64_t)left->word[i] + (uint64_t)right->word[i] + carry;

    result->word[i] = (uint32_t)sum;
    carry = (uint32_t)(sum >> 32);
  }

  return carry;
}

static uint32_t wide_subtract(Wide *result, const Wide *left, const Wide *right,
                              uint32_t borrow) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) {
    uint64_t minuend = left->word[i];
    uint64_t subtrahend = (uint64_t)right->word[i] + borrow;

    result->word[i] = (uint32_t)(minuend - subtrahend);
    borrow = minuend < subtrahend;
  }

  return borrow;
}

static void wide_shift_left(Wide *result, const Wide *source, unsigned bits) {
  unsigned word_shift;
  unsigned bit_shift;
  unsigned i;

  wide_zero(result);
  if (bits >= 256u) return;

  word_shift = bits / 32u;
  bit_shift = bits % 32u;
  for (i = word_shift; i < ENGINE_LANES; i++) {
    uint64_t shifted = (uint64_t)source->word[i - word_shift] << bit_shift;

    result->word[i] |= (uint32_t)shifted;
    if (bit_shift != 0 && i + 1 < ENGINE_LANES) result->word[i + 1] |= (uint32_t)(shifted >> 32);
  }
}

static void wide_shift_right(Wide *result, const Wide *source, unsigned bits) {
  unsigned word_shift;
  unsigned bit_shift;
  unsigned i;

  wide_zero(result);
  if (bits >= 256u) return;

  word_shift = bits / 32u;
  bit_shift = bits % 32u;
  for (i = 0; i + word_shift < ENGINE_LANES; i++) {
    uint32_t shifted = source->word[i + word_shift];

    result->word[i] |= shifted >> bit_shift;
    if (bit_shift != 0 && i + word_shift + 1 < ENGINE_LANES) {
      result->word[i] |= source->word[i + word_shift + 1] << (32u - bit_shift);
    }
  }
}

static void wide_shift_operand(Wide *result, const Wide *source, uint32_t immediate) {
  unsigned bits = (immediate & 0x7Fu) * 8u;

  if ((immediate & 0x80u) != 0) {
    wide_shift_right(result, source, bits);
  } else {
    wide_shift_left(result, source, bits);
  }
}

static void wide_and(Wide *result, const Wide *left, const Wide *right) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) result->word[i] = left->word[i] & right->word[i];
}

static void wide_or(Wide *result, const Wide *left, const Wide *right) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) result->word[i] = left->word[i] | right->word[i];
}

static void wide_xor(Wide *result, const Wide *left, const Wide *right) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) result->word[i] = left->word[i] ^ right->word[i];
}

static void wide_not(Wide *result, const Wide *source) {
  unsigned i;

  for (i = 0; i < ENGINE_LANES; i++) result->word[i] = ~source->word[i];
}

static void wide_multiply_halves(Wide *result, const Wide *left, unsigned left_upper,
                                 const Wide *right, unsigned right_upper) {
  uint32_t product[ENGINE_LANES] = {0};
  unsigned left_index;

  for (left_index = 0; left_index < 4u; left_index++) {
    uint64_t carry = 0;
    unsigned right_index;

    for (right_index = 0; right_index < 4u; right_index++) {
      unsigned position = left_index + right_index;
      uint64_t accumulator = (uint64_t)left->word[left_index + 4u * left_upper] *
                                 right->word[right_index + 4u * right_upper] +
                             product[position] + carry;

      product[position] = (uint32_t)accumulator;
      carry = accumulator >> 32;
    }
    product[left_index + 4u] = (uint32_t)carry;
  }

  memcpy(result->word, product, sizeof(product));
}

static int remainder_compare(const uint32_t remainder[9], const Wide *divisor) {
  unsigned i;

  if (remainder[8] != 0) return 1;

  i = ENGINE_LANES;
  while (i != 0) {
    i--;
    if (remainder[i] < divisor->word[i]) return -1;
    if (remainder[i] > divisor->word[i]) return 1;
  }

  return 0;
}

static void remainder_subtract(uint32_t remainder[9], const Wide *divisor) {
  uint32_t borrow = 0;
  unsigned i;

  for (i = 0; i < 9u; i++) {
    uint64_t minuend = remainder[i];
    uint64_t subtrahend = (i < ENGINE_LANES ? (uint64_t)divisor->word[i] : 0u) + borrow;

    remainder[i] = (uint32_t)(minuend - subtrahend);
    borrow = minuend < subtrahend;
  }
}

static void wide_mod(Wide *result, const uint32_t *numerator, unsigned numerator_words,
                     const Wide *divisor) {
  uint32_t remainder[9] = {0};
  unsigned bit = numerator_words * 32u;

  while (bit != 0) {
    uint32_t carry = 0;
    unsigned i;

    bit--;
    for (i = 0; i < 9u; i++) {
      uint32_t next = remainder[i] >> 31;

      remainder[i] = (remainder[i] << 1) | carry;
      carry = next;
    }

    remainder[0] |= (numerator[bit / 32u] >> (bit % 32u)) & 1u;
    if (remainder_compare(remainder, divisor) >= 0) remainder_subtract(remainder, divisor);
  }

  memcpy(result->word, remainder, sizeof(result->word));
}

static CryptoEngineStatus wide_add_mod(Wide *result, const Wide *left, const Wide *right,
                                       const Wide *divisor) {
  uint32_t numerator[9];
  Wide sum;

  if (wide_is_zero(divisor)) return CRYPTO_ENGINE_ERR_DIVIDE_BY_ZERO;

  numerator[8] = wide_add(&sum, left, right, 0);
  memcpy(numerator, sum.word, sizeof(sum.word));
  wide_mod(result, numerator, 9u, divisor);
  return CRYPTO_ENGINE_OK;
}

static CryptoEngineStatus wide_sub_mod(Wide *result, const Wide *left, const Wide *right,
                                       const Wide *divisor) {
  Wide magnitude;
  Wide remainder;

  if (wide_is_zero(divisor)) return CRYPTO_ENGINE_ERR_DIVIDE_BY_ZERO;

  if (wide_compare(left, right) >= 0) {
    (void)wide_subtract(&magnitude, left, right, 0);
    wide_mod(result, magnitude.word, ENGINE_LANES, divisor);
    return CRYPTO_ENGINE_OK;
  }

  (void)wide_subtract(&magnitude, right, left, 0);
  wide_mod(&remainder, magnitude.word, ENGINE_LANES, divisor);
  if (wide_is_zero(&remainder)) {
    wide_zero(result);
  } else {
    (void)wide_subtract(result, divisor, &remainder, 0);
  }

  return CRYPTO_ENGINE_OK;
}

static void set_standard_flags(const Wide *value) {
  flag_z = wide_is_zero(value);
  flag_m = (value->word[ENGINE_LANES - 1] >> 31) & 1u;
  flag_l = value->word[0] & 1u;
}

static void set_extended_flags(const Wide *value) {
  flag_xz = wide_is_zero(value);
  flag_xm = (value->word[ENGINE_LANES - 1] >> 31) & 1u;
  flag_xl = value->word[0] & 1u;
}

static void set_half_limb(Wide *value, unsigned limb, unsigned upper, uint32_t half) {
  uint32_t shift = upper != 0 ? 16u : 0u;
  uint32_t mask = 0xFFFFu << shift;

  value->word[limb] = (value->word[limb] & ~mask) | ((half & 0xFFFFu) << shift);
}

static void set_special_limb(Wide *value, unsigned limb, uint32_t low) {
  value->word[limb] = (value->word[limb] & 0xFFFF0000u) | (low & 0xFFFFu);
}

static int instruction_decodable(uint32_t instruction) {
  uint32_t opcode = instruction >> 26;
  uint32_t function = (instruction >> 23) & 7u;

  switch (opcode) {
    case 1u:
      return function == 2u || function == 4u;
    case 38u:
      return function <= 3u;
    case 2u:
    case 3u:
    case 4u:
    case 16u:
    case 17u:
    case 18u:
    case 19u:
    case 20u:
    case 21u:
    case 22u:
    case 23u:
    case 25u:
    case 26u:
    case 31u:
    case 32u:
    case 33u:
    case 34u:
    case 35u:
    case 36u:
    case 37u:
    case 39u:
    case 40u:
    case 62u:
    case 63u:
      return 1;
    default:
      return 0;
  }
}

static uint32_t decode_instruction(uint32_t instruction) {
  return instruction_decodable(instruction) ? instruction : ENGINE_NOP;
}

static void reset_execution(void) {
  memset(registers, 0, sizeof(registers));
  wide_zero(&rfp);
  wide_zero(&dmp);
  wide_zero(&lc);
  wide_zero(&modulus);
  memset(&rnd, 0x99, sizeof(rnd));

  pc = 0;
  call_depth = 0;
  loop_depth = 0;
  flag_c = 0;
  flag_l = 0;
  flag_m = 0;
  flag_z = 0;
  flag_xc = 0;
  flag_xl = 0;
  flag_xm = 0;
  flag_xz = 0;
  finished = 0;
}

void crypto_engine_reset(bool clear_dmem, bool clear_imem) {
  unsigned i;

  if (clear_dmem) memset(dmem, 0, sizeof(dmem));

  if (clear_imem) {
    memset(imem, 0, sizeof(imem));
    for (i = 0; i < CRYPTO_ENGINE_IMEM_WORDS; i++) decoded[i] = ENGINE_NOP;
  }

  reset_execution();
}

void crypto_engine_set_pc(uint32_t value) {
  pc = value % CRYPTO_ENGINE_IMEM_WORDS;
  finished = 0;
}

void crypto_engine_set_dmem(uint32_t index, uint32_t value) {
  if (index >= CRYPTO_ENGINE_DMEM_WORDS) return;

  dmem[index / ENGINE_LANES].word[index % ENGINE_LANES] = value;
}

uint32_t crypto_engine_get_dmem(uint32_t index) {
  if (index >= CRYPTO_ENGINE_DMEM_WORDS) return 0;

  return dmem[index / ENGINE_LANES].word[index % ENGINE_LANES];
}

void crypto_engine_set_imem(uint32_t index, uint32_t value) {
  if (index >= CRYPTO_ENGINE_IMEM_WORDS) return;

  imem[index] = value;
  decoded[index] = decode_instruction(value);
}

uint32_t crypto_engine_get_imem(uint32_t index) {
  if (index >= CRYPTO_ENGINE_IMEM_WORDS) return 0;

  return imem[index];
}

static CryptoEngineStatus execute(const uint32_t instruction, int *has_jump,
                                  uint32_t *jump_address) {
  uint32_t opcode = instruction >> 26;
  uint32_t function = (instruction >> 23) & 7u;
  uint32_t rd = (instruction >> 18) & 31u;
  uint32_t rs2 = (instruction >> 13) & 31u;
  uint32_t rs1 = (instruction >> 8) & 31u;
  uint32_t immediate = instruction & 0xFFu;
  Wide operand;
  Wide result;
  Wide small;
  uint32_t carry;

  *has_jump = 0;
  *jump_address = 0;

  switch (opcode) {
    case 20u:
      wide_zero(&small);
      if (function != 2u) wide_shift_operand(&operand, &registers[rs2], immediate);

      switch (function) {
        case 0u:
          carry = wide_add(&result, &registers[rs1], &operand, 0);
          flag_c = carry;
          set_standard_flags(&result);
          break;
        case 1u:
          carry = wide_add(&result, &registers[rs1], &operand, flag_c);
          flag_c = carry;
          set_standard_flags(&result);
          break;
        case 2u:
          small.word[0] = immediate;
          carry = wide_add(&result, &registers[rs1], &small, 0);
          flag_c = carry;
          set_standard_flags(&result);
          break;
        case 4u:
          carry = wide_add(&result, &registers[rs1], &operand, flag_c);
          flag_xc = carry;
          set_extended_flags(&result);
          break;
        case 5u:
          carry = wide_add(&result, &registers[rs1], &operand, flag_xc);
          flag_xc = carry;
          set_extended_flags(&result);
          break;
        default:
          return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }

      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 39u: {
      CryptoEngineStatus status;

      wide_shift_operand(&operand, &registers[rs2], immediate);
      status = wide_add_mod(&result, &registers[rs1], &operand, &modulus);
      if (status != CRYPTO_ENGINE_OK) return status;
      set_standard_flags(&result);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;
    }

    case 21u: {
      int comparison = wide_compare(&registers[rs2], &registers[rs1]);
      uint32_t borrow = comparison > 0 ? 1u : 0u;

      wide_zero(&small);
      if (function != 2u) wide_shift_operand(&operand, &registers[rs2], immediate);

      switch (function) {
        case 0u:
          (void)wide_subtract(&result, &registers[rs1], &operand, 0);
          flag_c = borrow;
          set_standard_flags(&result);
          break;
        case 1u:
          (void)wide_subtract(&result, &registers[rs1], &operand, flag_c);
          flag_c = borrow;
          set_standard_flags(&result);
          break;
        case 2u:
          small.word[0] = immediate;
          (void)wide_subtract(&result, &registers[rs1], &small, 0);
          flag_c = borrow;
          set_standard_flags(&result);
          break;
        case 4u:
          (void)wide_subtract(&result, &registers[rs1], &operand, 0);
          flag_xc = borrow;
          set_extended_flags(&result);
          break;
        case 5u:
          (void)wide_subtract(&result, &registers[rs1], &operand, flag_xc);
          flag_xc = borrow;
          set_extended_flags(&result);
          break;
        default:
          return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }

      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;
    }

    case 40u: {
      CryptoEngineStatus status;

      wide_shift_operand(&operand, &registers[rs2], immediate);
      status = wide_sub_mod(&result, &registers[rs1], &operand, &modulus);
      if (status != CRYPTO_ENGINE_OK) return status;
      set_standard_flags(&result);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;
    }

    case 22u:
      wide_multiply_halves(&result, &registers[rs1], function & 1u, &registers[rs2],
                           (function >> 1) & 1u);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 16u:
      wide_shift_operand(&operand, &registers[rs2], immediate);
      wide_and(&result, &registers[rs1], &operand);
      set_standard_flags(&result);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 17u:
      wide_shift_operand(&operand, &registers[rs2], immediate);
      wide_or(&result, &registers[rs1], &operand);
      set_standard_flags(&result);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 18u:
      wide_shift_operand(&operand, &registers[rs2], immediate);
      wide_not(&result, &operand);
      if (function == 4u) {
        set_extended_flags(&result);
      } else {
        set_standard_flags(&result);
      }
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 19u:
      wide_shift_operand(&operand, &registers[rs2], immediate);
      wide_xor(&result, &registers[rs1], &operand);
      set_standard_flags(&result);
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 25u: {
      int select;

      if (function == 0u) {
        switch (immediate) {
          case 1u:
            select = (int)flag_l;
            break;
          case 2u:
            select = (int)flag_m;
            break;
          case 4u:
            select = (int)flag_z;
            break;
          case 8u:
            select = (int)flag_c;
            break;
          default:
            return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
        }
      } else if (function == 4u) {
        switch (immediate) {
          case 1u:
            select = (int)flag_xl;
            break;
          case 2u:
            select = (int)flag_xm;
            break;
          case 4u:
            select = (int)flag_xz;
            break;
          case 8u:
            select = (int)flag_xc;
            break;
          default:
            return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
        }
      } else {
        return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }

      wide_copy(&registers[rd], select != 0 ? &registers[rs1] : &registers[rs2]);
      return CRYPTO_ENGINE_OK;
    }

    case 26u:
      if (immediate == 0) {
        wide_copy(&result, &registers[rs1]);
      } else {
        Wide upper;

        wide_shift_right(&result, &registers[rs1], immediate);
        wide_shift_left(&upper, &registers[rs2], 256u - immediate);
        wide_or(&result, &result, &upper);
      }
      wide_copy(&registers[rd], &result);
      return CRYPTO_ENGINE_OK;

    case 23u:
      if (function == 0u) {
        int comparison = wide_compare(&registers[rs2], &registers[rs1]);

        flag_z = comparison == 0;
        flag_c = comparison > 0;
      } else if (function == 5u) {
        int comparison = wide_compare(&registers[rs2], &registers[rs1]);

        if (comparison > 0) {
          flag_xc = 1;
        } else if (comparison < 0) {
          flag_xc = 0;
        }
      } else {
        return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }
      return CRYPTO_ENGINE_OK;

    case 37u:
      switch (function) {
        case 7u:
          wide_copy(&rfp, &registers[rs1]);
          break;
        case 1u:
          wide_copy(&lc, &registers[rs1]);
          break;
        case 2u:
          wide_copy(&registers[rd], &dmp);
          break;
        case 3u:
          wide_copy(&dmp, &registers[rs1]);
          break;
        case 5u:
          return CRYPTO_ENGINE_ERR_INVALID_SPECIAL_REGISTER;
        default:
          return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }
      return CRYPTO_ENGINE_OK;

    case 38u:
      switch (function) {
        case 0u:
          wide_copy(&registers[rd], &modulus);
          break;
        case 1u:
          wide_copy(&modulus, &registers[rs1]);
          break;
        case 2u:
          wide_copy(&registers[rd], &rnd);
          break;
        case 3u:
          wide_copy(&rnd, &registers[rs1]);
          break;
        default:
          return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }
      return CRYPTO_ENGINE_OK;

    case 33u: {
      uint32_t wide_immediate = instruction & 0xFFFFu;
      uint32_t index = (wide_immediate >> 5) & 0xFFu;

      if (index >= ENGINE_DMEM_WIDE_WORDS) return CRYPTO_ENGINE_ERR_DMEM_INDEX;
      wide_copy(&registers[rd], &dmem[index]);
      return CRYPTO_ENGINE_OK;
    }

    case 34u: {
      uint32_t wide_immediate = instruction & 0xFFFFu;
      uint32_t index = (wide_immediate >> 5) & 0xFFu;

      if (index >= ENGINE_DMEM_WIDE_WORDS) return CRYPTO_ENGINE_ERR_DMEM_INDEX;
      wide_copy(&dmem[index], &registers[rd]);
      return CRYPTO_ENGINE_OK;
    }

    case 31u:
      if (function == 0u) {
        wide_copy(&registers[rd], &registers[rs1]);
      } else if (function == 1u) {
        uint32_t source_limb = rs1 & 7u;
        uint32_t destination_limb = rd & 7u;
        uint32_t source_pointer = rfp.word[source_limb] & 31u;
        uint32_t destination_pointer = rfp.word[destination_limb] & 31u;

        wide_copy(&registers[destination_pointer], &registers[source_pointer]);
        if ((rs1 & 8u) != 0) set_special_limb(&rfp, source_limb, source_pointer + 1u);
        if ((rd & 8u) != 0) set_special_limb(&rfp, destination_limb, destination_pointer + 1u);
      } else {
        return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }
      return CRYPTO_ENGINE_OK;

    case 32u: {
      uint32_t half = instruction & 0xFFFFu;
      uint32_t upper = (instruction >> 16) & 1u;

      set_half_limb(&registers[rd], function, upper, half);
      return CRYPTO_ENGINE_OK;
    }

    case 36u: {
      uint32_t source_limb = rs1 & 7u;
      uint32_t destination_limb = rd & 7u;
      uint32_t source_pointer = rfp.word[source_limb] & 31u;
      uint32_t destination_pointer = dmp.word[destination_limb] & 127u;

      wide_copy(&dmem[destination_pointer], &registers[source_pointer]);
      if ((rs1 & 8u) != 0) set_special_limb(&rfp, source_limb, source_pointer + 1u);
      if ((rd & 8u) != 0) set_special_limb(&dmp, destination_limb, destination_pointer + 1u);
      return CRYPTO_ENGINE_OK;
    }

    case 35u: {
      uint32_t source_limb = rs1 & 7u;
      uint32_t destination_limb = rd & 7u;
      uint32_t source_pointer = dmp.word[source_limb] & 127u;
      uint32_t destination_pointer = rfp.word[destination_limb] & 31u;

      wide_copy(&registers[destination_pointer], &dmem[source_pointer]);
      if ((rs1 & 8u) != 0) set_special_limb(&dmp, source_limb, source_pointer + 1u);
      if ((rd & 8u) != 0) set_special_limb(&rfp, destination_limb, destination_pointer + 1u);
      return CRYPTO_ENGINE_OK;
    }

    case 63u:
      return CRYPTO_ENGINE_OK;

    case 62u:
      return function <= 3u ? CRYPTO_ENGINE_OK : CRYPTO_ENGINE_ERR_INVALID_OPCODE;

    case 3u:
      if (call_depth != 0) {
        *jump_address = call_stack[--call_depth];
      } else {
        *jump_address = pc;
        if (pc != ENGINE_LAST_IMEM) finished = 1;
      }
      *has_jump = 1;
      return CRYPTO_ENGINE_OK;

    case 2u: {
      uint32_t destination = instruction & 0xFFFu;
      uint32_t return_address = pc + 1u;

      if (return_address >= CRYPTO_ENGINE_IMEM_WORDS) return CRYPTO_ENGINE_ERR_IMEM_INDEX;
      if (call_depth == ENGINE_CALL_STACK) return CRYPTO_ENGINE_ERR_CALL_STACK_OVERFLOW;

      call_stack[call_depth++] = return_address;
      *has_jump = 1;
      *jump_address = destination;
      return CRYPTO_ENGINE_OK;
    }

    case 4u: {
      uint32_t branch = (instruction >> 12) & 0x7FFu;
      int take;

      switch (branch) {
        case 0x001u:
          take = (int)flag_l;
          break;
        case 0x002u:
          take = (int)flag_m;
          break;
        case 0x088u:
          take = flag_c == 0;
          break;
        case 0x080u:
          take = 1;
          break;
        case 0x084u:
          take = flag_z == 0;
          break;
        case 0x004u:
          take = (int)flag_z;
          break;
        default:
          return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }

      if (take) {
        *has_jump = 1;
        *jump_address = instruction & 0xFFFu;
      }
      return CRYPTO_ENGINE_OK;
    }

    case 1u: {
      uint32_t length = instruction & 0xFFFu;
      uint32_t count;
      uint32_t start_address = pc + 1u;
      uint32_t end_address = pc + length;

      if (function == 2u) {
        count = (instruction >> 12) & 0x7FFu;
      } else if (function == 4u) {
        uint32_t encoded = (instruction >> 12) & 0x7FFu;
        uint32_t encoded_register = (encoded >> 1) & 31u;
        uint32_t limb = encoded_register & 7u;

        count = lc.word[limb];
      } else {
        return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
      }

      if (start_address >= CRYPTO_ENGINE_IMEM_WORDS || end_address >= CRYPTO_ENGINE_IMEM_WORDS) {
        return CRYPTO_ENGINE_ERR_IMEM_INDEX;
      }
      if (loop_depth == ENGINE_LOOP_STACK) return CRYPTO_ENGINE_ERR_LOOP_STACK_OVERFLOW;

      loop_stack[loop_depth].count = (int64_t)count - 1;
      loop_stack[loop_depth].end = end_address;
      loop_stack[loop_depth].start = start_address;
      loop_depth++;
      return CRYPTO_ENGINE_OK;
    }

    default:
      return CRYPTO_ENGINE_ERR_INVALID_OPCODE;
  }
}

static CryptoEngineStatus step(int *keep_running) {
  uint32_t jump_address;
  int has_jump;
  int halt = pc == ENGINE_LAST_IMEM || finished != 0;
  int can_continue;
  CryptoEngineStatus status;

  status = execute(decoded[pc], &has_jump, &jump_address);
  if (status != CRYPTO_ENGINE_OK) return status;

  if (loop_depth != 0) {
    LoopFrame *frame = &loop_stack[loop_depth - 1u];

    if (pc == frame->end) {
      if (frame->count != 0) {
        frame->count--;
        has_jump = 1;
        jump_address = frame->start;
      } else {
        loop_depth--;
      }
    }
  }

  if (has_jump) {
    if (jump_address >= CRYPTO_ENGINE_IMEM_WORDS) return CRYPTO_ENGINE_ERR_INVALID_JUMP;

    pc = jump_address;
    can_continue = 1;
  } else if (pc + 1u >= CRYPTO_ENGINE_IMEM_WORDS) {
    can_continue = 0;
  } else {
    pc++;
    can_continue = 1;
  }

  *keep_running = halt ? 0 : can_continue;
  return CRYPTO_ENGINE_OK;
}

CryptoEngineStatus crypto_engine_run_batch(uint32_t max_steps, bool *done) {
  uint32_t i;

  *done = false;
  for (i = 0; i < max_steps; i++) {
    int keep_running;
    CryptoEngineStatus status = step(&keep_running);

    if (status != CRYPTO_ENGINE_OK) return status;
    if (!keep_running) {
      *done = true;
      return CRYPTO_ENGINE_OK;
    }
  }

  return CRYPTO_ENGINE_OK;
}

CryptoEngineStatus crypto_engine_run(void) {
  uint32_t steps = 0;

  while (steps < ENGINE_MAX_STEPS) {
    bool done;
    CryptoEngineStatus status = crypto_engine_run_batch(1, &done);

    if (status != CRYPTO_ENGINE_OK) return status;
    if (done) return CRYPTO_ENGINE_OK;
    steps++;
  }

  return CRYPTO_ENGINE_ERR_STEP_LIMIT;
}


const char *crypto_engine_status_string(CryptoEngineStatus status) {
  switch (status) {
    case CRYPTO_ENGINE_OK:
      return "success";
    case CRYPTO_ENGINE_ERR_INVALID_OPCODE:
      return "invalid opcode";
    case CRYPTO_ENGINE_ERR_INVALID_JUMP:
      return "invalid jump address";
    case CRYPTO_ENGINE_ERR_DMEM_INDEX:
      return "DMEM index out of range";
    case CRYPTO_ENGINE_ERR_IMEM_INDEX:
      return "IMEM index out of range";
    case CRYPTO_ENGINE_ERR_LOOP_STACK_OVERFLOW:
      return "loop stack overflow";
    case CRYPTO_ENGINE_ERR_CALL_STACK_OVERFLOW:
      return "call stack overflow";
    case CRYPTO_ENGINE_ERR_DIVIDE_BY_ZERO:
      return "divide by zero";
    case CRYPTO_ENGINE_ERR_INVALID_SPECIAL_REGISTER:
      return "invalid special register";
    case CRYPTO_ENGINE_ERR_STEP_LIMIT:
      return "step limit reached";
    default:
      return "unknown engine error";
  }
}
