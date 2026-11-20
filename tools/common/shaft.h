#ifndef TOOLS_COMMON_SHAFT_H
#define TOOLS_COMMON_SHAFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHAFT_PACKET_SIZE 1024
#define SHAFT_HASH_SIZE 32
#define SHAFT_HEADER_SIZE 40
#define SHAFT_DATA_SIZE (SHAFT_PACKET_SIZE - SHAFT_HEADER_SIZE)

#define SHAFT_PROGRAM_BASE 0x40000u
#define SHAFT_RO_SIZE 0x4000u
#define SHAFT_RW_MEM_OFF 0x4000u
#define SHAFT_TOP_SIZE 0x3000u
#define SHAFT_FLASH_HALF (512u * 1024u / 2u)
#define SHAFT_RW_SIZE (SHAFT_FLASH_HALF - SHAFT_RW_MEM_OFF - SHAFT_TOP_SIZE)

typedef struct {
  uint32_t magic;
  uint32_t signature[96];
  uint32_t img_chk;
  uint32_t tag[7];
  uint32_t keyid;
  uint32_t key[96];
  uint32_t image_size;
  uint32_t ro_base;
  uint32_t ro_max;
  uint32_t rx_base;
  uint32_t rx_max;
} ShaftSignedHeader;

typedef struct {
  uint8_t *data;
  size_t size;
  size_t rw_offset;
  uint32_t image_base;
  size_t image_size;
} ShaftImage;

int shaft_open_tty(const char *path);
int shaft_engage(int fd, unsigned timeout_ms, int verbose);
int shaft_sync(int fd, unsigned timeout_ms);
int shaft_transfer(int fd, const uint8_t *data, size_t size, uint32_t flash_offset,
                   unsigned timeout_ms, unsigned retries, int verbose);
int shaft_send_end(int fd, unsigned timeout_ms, int verbose);
bool shaft_load_image(const char *path, ShaftImage *image);
void shaft_free_image(ShaftImage *image);
int shaft_rescue(const char *tty, const char *firmware, unsigned timeout_ms,
                 unsigned retries, int verbose);
int shaft_spiflash(const char *tty, const char *firmware, uint32_t offset,
                   unsigned timeout_ms, unsigned retries, int verbose);

#endif /* TOOLS_COMMON_SHAFT_H */
