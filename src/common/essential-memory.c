#include <common/essential-memory.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include <common/ctx.h>
#include <common/exit.h>
#include <common/file.h>
#include <common/logger.h>
#include <common/rc.h>

static const EssentialMemoryEntry *find_essential_memory(EssentialMemoryRole role) {
  size_t i;

  for (i = 0; i < MAX_ESSENTIAL_MEMORY_ENTRIES; i++) {
    const EssentialMemoryEntry *entry = &ctx.essential_memory[i];

    if (entry->NAME[0] == '\0') break;
    if (entry->role == role) return entry;
  }

  return NULL;
}

static void load_image(const EssentialMemoryEntry *entry, bool bundled, const uint8_t *data,
                       size_t data_len, const char *path, const char *label) {
  uint8_t *buf;
  size_t buf_len;
  uc_err err;

  if (entry->size == 0 || entry->size == UINT32_MAX) {
    LOG_ERR("Invalid %s region size\n", label);
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  buf_len = (size_t)entry->size + 1;
  buf = malloc(buf_len);
  if (buf == NULL) {
    LOG_ERR("Failed to allocate %s buffer\n", label);
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  memset(buf, 0xFF, buf_len);

  if (bundled) {
    if (load_bundled_binary(data, data_len, buf, entry->size) != 0) {
      free(buf);
      emu_exit(RC_CHIPEMU_FAILED_INIT);
    }
    LOG_VBS("Loaded bundled %s\n", label);
  } else {
    FileRC frc = file_readbuf(path, buf, buf_len);
    if (frc != FILE_SUCCESS) {
      LOG_ERR("Failed to load %s %s\n", label, path);
      free(buf);
      emu_exit(RC_CHIPEMU_FAILED_INIT);
    }
    LOG_VBS("Loaded %s from %s\n", label, path);
  }

  err = uc_mem_write(ctx.uc, entry->base_addr, buf, entry->size);
  if (err != UC_ERR_OK) {
    LOG_ERR("Failed to write %s to %s: %s\n", label, entry->NAME, uc_strerror(err));
    free(buf);
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  LOG_VBS("Wrote %s to %s\n", label, entry->NAME);
  free(buf);
}

void load_firmware(const uint8_t *bundled_rom, size_t bundled_rom_len,
                   const uint8_t *bundled_fw, size_t bundled_fw_len) {
  const EssentialMemoryEntry *rom;
  const EssentialMemoryEntry *firmware;

  if (ctx.uc == NULL) {
    LOG_ERR("Cannot load firmware without an engine\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (ctx.essential_memory == NULL) {
    LOG_ERR("No essential memory map registered\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  rom = find_essential_memory(ESSENTIAL_MEMORY_ROM);
  firmware = find_essential_memory(ESSENTIAL_MEMORY_FIRMWARE);

  if (rom == NULL) {
    LOG_ERR("No ROM region in essential memory\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (firmware == NULL) {
    LOG_ERR("No firmware region in essential memory\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  load_image(rom, ctx.firmware.useBundledROM, bundled_rom, bundled_rom_len,
             ctx.firmware.bootrom, "BootROM");
  load_image(firmware, ctx.firmware.useBundledFW, bundled_fw, bundled_fw_len,
             ctx.firmware.progfw, "Firmware");
}

void map_essential_memory(void) {
  size_t i;

  if (ctx.uc == NULL) {
    LOG_ERR("Cannot map essential memory without an engine\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  if (ctx.essential_memory == NULL) {
    LOG_ERR("No essential memory map registered\n");
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }

  for (i = 0; i < MAX_ESSENTIAL_MEMORY_ENTRIES; i++) {
    const EssentialMemoryEntry *entry = &ctx.essential_memory[i];
    uc_err err;

    if (entry->NAME[0] == '\0') break;

    err = uc_mem_map(ctx.uc, entry->base_addr, entry->size, entry->perms);
    if (err != UC_ERR_OK) {
      LOG_ERR("Failed to map %s (base=0x%08x size=0x%08x): %s\n",
              entry->NAME, entry->base_addr, entry->size, uc_strerror(err));
      emu_exit(RC_CHIPEMU_FAILED_INIT);
    }

    LOG_VBS("Mapped %s (base=0x%08x size=0x%08x perms=0x%x)\n",
            entry->NAME, entry->base_addr, entry->size, entry->perms);
  }
}

int load_bundled_binary(const uint8_t *src, size_t src_len, uint8_t *dest, size_t dest_len) {
  if (src == NULL || dest == NULL) {
    LOG_ERR("Invalid bundled binary buffer\n");
    return -1;
  }

  if (src_len == 0) {
    LOG_ERR("Bundled binary is empty\n");
    return -1;
  }

  if (src_len > dest_len) {
    LOG_ERR("Bundled binary exceeds destination (%zu > %zu)\n", src_len, dest_len);
    return -1;
  }

  memcpy(dest, src, src_len);
  return 0;
}
