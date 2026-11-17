#ifndef COMMON_ESSENTIAL_MEMORY_H
#define COMMON_ESSENTIAL_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define MAX_ESSENTIAL_MEMORY_ENTRIES 128

typedef enum {
  ESSENTIAL_MEMORY_OTHER = 0,
  ESSENTIAL_MEMORY_ROM,
  ESSENTIAL_MEMORY_FIRMWARE,
} EssentialMemoryRole;

typedef struct {
  char NAME[16];
  uint32_t base_addr;
  uint32_t size;
  uint32_t perms;
  EssentialMemoryRole role;
} EssentialMemoryEntry;

void map_essential_memory(void);

void load_firmware(const uint8_t *bundled_rom, size_t bundled_rom_len,
                   const uint8_t *bundled_fw, size_t bundled_fw_len);

int load_bundled_binary(const uint8_t *src, size_t src_len, uint8_t *dest, size_t dest_len);

#endif /* COMMON_ESSENTIAL_MEMORY_H */
