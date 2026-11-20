#define _GNU_SOURCE

#include "shaft.h"

#include <common/sha256.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static const char *shaft_tool = "shaft";

static uint64_t shaft_now_ms(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int shaft_write_all(int fd, const uint8_t *data, size_t len, unsigned timeout_ms) {
  size_t offset = 0;
  uint64_t deadline = shaft_now_ms() + timeout_ms;

  while (offset < len) {
    ssize_t count = write(fd, data + offset, len - offset);

    if (count > 0) {
      offset += (size_t)count;
      continue;
    }

    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;

    {
      uint64_t now = shaft_now_ms();
      struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };

      if (now >= deadline) return -1;
      if (poll(&pfd, 1, (int)(deadline - now)) < 0 && errno != EINTR) return -1;
    }
  }

  return 0;
}

static int shaft_read_exact(int fd, uint8_t *data, size_t len, unsigned timeout_ms) {
  size_t offset = 0;
  uint64_t deadline = shaft_now_ms() + timeout_ms;

  while (offset < len) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    uint64_t now = shaft_now_ms();
    int ready;

    if (now >= deadline) return -1;

    ready = poll(&pfd, 1, (int)(deadline - now));
    if (ready < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (ready == 0 || (pfd.revents & POLLIN) == 0) return -1;

    {
      ssize_t count = read(fd, data + offset, len - offset);

      if (count > 0) {
        offset += (size_t)count;
        continue;
      }

      if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      return -1;
    }
  }

  return 0;
}

int shaft_open_tty(const char *path) {
  struct termios termios;
  int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd < 0) {
    fprintf(stderr, "%s: cannot open %s: %s\n", shaft_tool, path, strerror(errno));
    return -1;
  }

  if (tcgetattr(fd, &termios) == 0) {
    cfmakeraw(&termios);
    termios.c_cc[VMIN] = 0;
    termios.c_cc[VTIME] = 0;
    (void)tcsetattr(fd, TCSANOW, &termios);
  }

  (void)tcflush(fd, TCIOFLUSH);
  return fd;
}

int shaft_engage(int fd, unsigned timeout_ms, int verbose) {
  char received[4096];
  size_t received_len = 0;
  uint64_t start = shaft_now_ms();
  uint64_t deadline = start + timeout_ms;
  uint64_t prompt_at = 0;
  bool key_sent = false;

  memset(received, 0, sizeof(received));

  while (shaft_now_ms() < deadline) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int ready = poll(&pfd, 1, 500);

    if (ready < 0) {
      if (errno == EINTR) continue;
      return -1;
    }

    if (ready > 0) {
      for (;;) {
        char byte;
        ssize_t count = read(fd, &byte, 1);

        if (count == 1) {
          if (verbose) putchar(byte);
          if (received_len < sizeof(received) - 1) received[received_len++] = byte;
          else {
            memmove(received, received + sizeof(received) / 2, sizeof(received) / 2);
            received_len = sizeof(received) / 2;
            received[received_len++] = byte;
          }
          received[received_len] = '\0';
          continue;
        }

        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (count <= 0) break;
      }
    }

    if (!key_sent) {
      const char *marker = strstr(received, "oops?|");
      bool incomplete = false;

      if (marker != NULL) {
        if (marker[6] == 'n' && marker[7] == 'o') {
          fprintf(stderr, "%s: guest did not enter rescue\n", shaft_tool);
          return -1;
        }

        incomplete = marker[6] == '\0' || (marker[6] == 'n' && marker[7] == '\0');

        if (!incomplete) {
          if (shaft_write_all(fd, (const uint8_t *)"r", 1, timeout_ms) != 0) return -1;
          key_sent = true;
        }
      }

      if (incomplete) {
        if (prompt_at == 0) prompt_at = shaft_now_ms();
        else if (shaft_now_ms() - prompt_at >= 250) {
          if (shaft_write_all(fd, (const uint8_t *)"r", 1, timeout_ms) != 0) return -1;
          key_sent = true;
        }
      }
    }

    if (strstr(received, "escue") != NULL) {
      if (verbose) putchar('\n');
      (void)tcflush(fd, TCOFLUSH);
      return 0;
    }
  }

  return -1;
}

static void shaft_packet_build(uint8_t packet[SHAFT_PACKET_SIZE], uint32_t frame,
                               uint32_t flash_offset, const uint8_t *data, size_t len) {
  Sha256Context sha;

  memset(packet, 0xFF, SHAFT_PACKET_SIZE);
  memset(packet, 0, SHAFT_HEADER_SIZE);

  if (len > SHAFT_DATA_SIZE) len = SHAFT_DATA_SIZE;
  if (len != 0) memcpy(packet + SHAFT_HEADER_SIZE, data, len);

  packet[32] = (uint8_t)frame;
  packet[33] = (uint8_t)(frame >> 8);
  packet[34] = (uint8_t)(frame >> 16);
  packet[35] = (uint8_t)(frame >> 24);
  packet[36] = (uint8_t)flash_offset;
  packet[37] = (uint8_t)(flash_offset >> 8);
  packet[38] = (uint8_t)(flash_offset >> 16);
  packet[39] = (uint8_t)(flash_offset >> 24);

  sha256_init(&sha);
  sha256_update(&sha, packet + 32, SHAFT_PACKET_SIZE - 32);
  sha256_final(&sha, packet);
}

static void shaft_packet_digest(const uint8_t packet[SHAFT_PACKET_SIZE],
                                uint8_t digest[SHAFT_HASH_SIZE]) {
  Sha256Context sha;
  unsigned i;

  sha256_init(&sha);
  sha256_update(&sha, packet, SHAFT_PACKET_SIZE);
  sha256_final(&sha, digest);

  for (i = 0; i < SHAFT_HASH_SIZE; i++) {
    if (digest[i] == 0) digest[i] = 1;
  }
}

int shaft_sync(int fd, unsigned timeout_ms) {
  uint8_t packet[SHAFT_PACKET_SIZE];
  uint8_t response[SHAFT_HASH_SIZE];
  unsigned i;

  memset(packet, 0, sizeof(packet));

  for (i = 0; i < 2; i++) {
    if (shaft_write_all(fd, packet, sizeof(packet), timeout_ms) != 0) return -1;
    if (shaft_read_exact(fd, response, sizeof(response), timeout_ms) != 0) return -1;
  }

  return 0;
}

static int shaft_send_packet(int fd, const uint8_t packet[SHAFT_PACKET_SIZE],
                             uint32_t frame, unsigned timeout_ms, unsigned retries,
                             int verbose) {
  uint8_t expected[SHAFT_HASH_SIZE];
  unsigned attempt;
  static bool debug_printed;

  shaft_packet_digest(packet, expected);

  for (attempt = 0; attempt <= retries; attempt++) {
    uint8_t response[SHAFT_HASH_SIZE];

    if (shaft_write_all(fd, packet, SHAFT_PACKET_SIZE, timeout_ms) != 0) {
      fprintf(stderr, "%s: packet %u write failed\n", shaft_tool, frame);
      return -1;
    }

    if (shaft_read_exact(fd, response, sizeof(response), timeout_ms) != 0) {
      fprintf(stderr, "%s: packet %u response timed out\n", shaft_tool, frame);
      continue;
    }

    if (memcmp(response, expected, sizeof(expected)) == 0) {
      if (verbose) printf("%s: packet %u acknowledged\n", shaft_tool, frame);
      return 0;
    }

    if (verbose && !debug_printed) {
      unsigned i;

      fprintf(stderr, "%s: packet %u got ", shaft_tool, frame);
      for (i = 0; i < SHAFT_HASH_SIZE; i++) fprintf(stderr, "%02x", response[i]);
      fprintf(stderr, " expected ");
      for (i = 0; i < SHAFT_HASH_SIZE; i++) fprintf(stderr, "%02x", expected[i]);
      fprintf(stderr, "\n");
      debug_printed = true;
    }

    fprintf(stderr, "%s: packet %u digest mismatch (attempt %u/%u)\n", shaft_tool, frame,
            attempt + 1, retries + 1);
  }

  return -1;
}

int shaft_transfer(int fd, const uint8_t *data, size_t size, uint32_t flash_offset,
                   unsigned timeout_ms, unsigned retries, int verbose) {
  uint8_t packet[SHAFT_PACKET_SIZE];
  size_t offset = 0;
  uint32_t frame = 0;

  printf("%s: sending %zu bytes at 0x%05x\n", shaft_tool, size, flash_offset);

  while (offset < size) {
    size_t chunk = size - offset;

    if (chunk > SHAFT_DATA_SIZE) chunk = SHAFT_DATA_SIZE;

    shaft_packet_build(packet, frame, flash_offset + (uint32_t)offset, data + offset, chunk);

    if (shaft_send_packet(fd, packet, frame, timeout_ms, retries, verbose) != 0) return -1;

    offset += chunk;
    frame++;

    if (!verbose && (frame % 32u) == 0) {
      printf("%s: %u packets (%zu/%zu bytes)\n", shaft_tool, frame, offset, size);
      fflush(stdout);
    }
  }

  return 0;
}

int shaft_send_end(int fd, unsigned timeout_ms, int verbose) {
  uint8_t packet[SHAFT_PACKET_SIZE];
  uint8_t response[SHAFT_HASH_SIZE];

  shaft_packet_build(packet, 0xFFFFFFFFu, 0, NULL, 0);

  if (shaft_write_all(fd, packet, sizeof(packet), timeout_ms) != 0) return -1;
  if (shaft_read_exact(fd, response, sizeof(response), timeout_ms) != 0) {
    if (verbose) printf("%s: no final response\n", shaft_tool);
  }

  return 0;
}

static bool shaft_read_file(const char *path, uint8_t **data, size_t *size) {
  struct stat st;
  FILE *file;
  uint8_t *buffer;

  *data = NULL;
  *size = 0;

  file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "%s: cannot open %s: %s\n", shaft_tool, path, strerror(errno));
    return false;
  }

  if (fstat(fileno(file), &st) != 0 || st.st_size <= 0) {
    fprintf(stderr, "%s: cannot stat %s\n", shaft_tool, path);
    fclose(file);
    return false;
  }

  *size = (size_t)st.st_size;
  buffer = malloc(*size);
  if (buffer == NULL) {
    fprintf(stderr, "%s: cannot allocate %zu bytes\n", shaft_tool, *size);
    fclose(file);
    return false;
  }

  if (fread(buffer, 1, *size, file) != *size) {
    fprintf(stderr, "%s: cannot read %s\n", shaft_tool, path);
    free(buffer);
    fclose(file);
    *size = 0;
    return false;
  }

  fclose(file);
  *data = buffer;
  return true;
}

static bool shaft_header_valid(const ShaftSignedHeader *header) {
  if (header->magic != 0xFFFFFFFFu) return false;
  if (header->image_size < 0x800) return false;
  if (header->image_size > SHAFT_RW_SIZE) return false;
  if (header->rx_base != header->ro_base + 0x400u) return false;
  return true;
}

static size_t shaft_find_header(const uint8_t *data, size_t size) {
  size_t offset;

  if (size >= sizeof(ShaftSignedHeader) && size <= 0x4000 &&
      shaft_header_valid((const ShaftSignedHeader *)data)) {
    return 0;
  }

  if (size > 0x4000 && shaft_header_valid((const ShaftSignedHeader *)data) &&
      ((const ShaftSignedHeader *)data)->image_size == size) {
    return 0;
  }

  for (offset = SHAFT_RO_SIZE; offset + sizeof(ShaftSignedHeader) <= size; offset += 0x800) {
    if (shaft_header_valid((const ShaftSignedHeader *)(data + offset))) return offset;
  }

  return SIZE_MAX;
}

bool shaft_load_image(const char *path, ShaftImage *image) {
  uint8_t *data;
  size_t header_offset;
  const ShaftSignedHeader *header;
  size_t size;

  memset(image, 0, sizeof(*image));

  if (!shaft_read_file(path, &data, &size)) return false;

  header_offset = shaft_find_header(data, size);
  if (header_offset == SIZE_MAX) {
    fprintf(stderr, "%s: no valid RW header in %s\n", shaft_tool, path);
    free(data);
    return false;
  }

  header = (const ShaftSignedHeader *)(data + header_offset);
  image->data = data;
  image->size = size;
  image->rw_offset = header_offset;
  image->image_size = header->image_size;

  if (header_offset == 0) {
    image->image_base = SHAFT_RW_MEM_OFF;
  } else if (header->ro_base >= SHAFT_PROGRAM_BASE) {
    image->image_base = header->ro_base - SHAFT_PROGRAM_BASE;
  } else {
    fprintf(stderr, "%s: invalid RW base 0x%08x\n", shaft_tool, header->ro_base);
    free(data);
    memset(image, 0, sizeof(*image));
    return false;
  }

  if (header_offset + image->image_size > size) {
    fprintf(stderr, "%s: RW image extends past %s\n", shaft_tool, path);
    free(data);
    memset(image, 0, sizeof(*image));
    return false;
  }

  printf("%s: image header 0x%zx, base 0x%05x, size 0x%zx\n", shaft_tool, header_offset,
         image->image_base, image->image_size);
  return true;
}

void shaft_free_image(ShaftImage *image) {
  free(image->data);
  memset(image, 0, sizeof(*image));
}

int shaft_rescue(const char *tty, const char *firmware, unsigned timeout_ms,
                 unsigned retries, int verbose) {
  ShaftImage image;
  int fd;

  shaft_tool = "rescue";
  if (!shaft_load_image(firmware, &image)) return 1;

  fd = shaft_open_tty(tty);
  if (fd < 0) {
    shaft_free_image(&image);
    return 1;
  }

  printf("%s: engaging\n", shaft_tool);
  if (shaft_engage(fd, timeout_ms * 3u, verbose) != 0) {
    fprintf(stderr, "%s: did not see 'escue'\n", shaft_tool);
    close(fd);
    shaft_free_image(&image);
    return 1;
  }

  printf("%s: synchronizing\n", shaft_tool);
  if (shaft_sync(fd, timeout_ms) != 0) {
    fprintf(stderr, "%s: sync failed\n", shaft_tool);
    close(fd);
    shaft_free_image(&image);
    return 1;
  }

  if (shaft_transfer(fd, image.data + image.rw_offset, image.image_size, image.image_base,
                     timeout_ms, retries, verbose) != 0) {
    fprintf(stderr, "%s: transfer failed\n", shaft_tool);
    close(fd);
    shaft_free_image(&image);
    return 1;
  }

  if (shaft_send_end(fd, timeout_ms, verbose) != 0) {
    fprintf(stderr, "%s: end packet failed\n", shaft_tool);
    close(fd);
    shaft_free_image(&image);
    return 1;
  }

  printf("%s: done\n", shaft_tool);
  close(fd);
  shaft_free_image(&image);
  return 0;
}

int shaft_spiflash(const char *tty, const char *firmware, uint32_t offset,
                   unsigned timeout_ms, unsigned retries, int verbose) {
  uint8_t *data;
  size_t size;
  int fd;

  shaft_tool = "spiflash";
  if (!shaft_read_file(firmware, &data, &size)) return 1;

  fd = shaft_open_tty(tty);
  if (fd < 0) {
    free(data);
    return 1;
  }

  if (shaft_transfer(fd, data, size, offset, timeout_ms, retries, verbose) != 0) {
    fprintf(stderr, "%s: transfer failed\n", shaft_tool);
    close(fd);
    free(data);
    return 1;
  }

  if (shaft_send_end(fd, timeout_ms, verbose) != 0) {
    fprintf(stderr, "%s: end packet failed\n", shaft_tool);
    close(fd);
    free(data);
    return 1;
  }

  printf("%s: done\n", shaft_tool);
  close(fd);
  free(data);
  return 0;
}
