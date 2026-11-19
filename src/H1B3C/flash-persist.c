#include <H1B3C/flash-persist.h>
#include <H1B3C/memory.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <common/common.h>
#include <common/logger.h>

typedef struct {
  const char *name;
  uint32_t base;
  uint32_t size;
} FlashRegion;

static const FlashRegion flash_regions[] = {
  { "info0.bin", FLASH_INFO0_BASE, FLASH_INFO0_SIZE },
  { "info1.bin", FLASH_INFO1_BASE, FLASH_INFO1_SIZE },
  { "prog.bin", FLASH_PROG_BASE, FLASH_PROG_SIZE },
};

static bool flash_path(char *path, size_t path_size, const char *name) {
  int length = snprintf(path, path_size, "%s/%s", ctx.flash_dir, name);

  return length >= 0 && (size_t)length < path_size;
}

static bool flash_ensure_dir(void) {
  char work[MAX_FILE_PATH];
  char *cursor;
  int length;

  length = snprintf(work, sizeof(work), "%s", ctx.flash_dir);

  if (length < 0 || (size_t)length >= sizeof(work)) return false;

  for (cursor = work + 1; *cursor != '\0'; cursor++) {
    if (*cursor != '/') continue;

    *cursor = '\0';

    if (mkdir(work, 0700) != 0 && errno != EEXIST) {
      LOG_ERR("Flash: cannot create %s: %s\n", work, strerror(errno));
      return false;
    }

    *cursor = '/';
  }

  if (mkdir(work, 0700) != 0 && errno != EEXIST) {
    LOG_ERR("Flash: cannot create %s: %s\n", work, strerror(errno));
    return false;
  }

  return true;
}

static bool flash_load_region(const FlashRegion *region) {
  char path[MAX_FILE_PATH];
  FILE *file;
  uint8_t *buffer;
  long length;
  size_t read;

  if (!flash_path(path, sizeof(path), region->name)) return false;

  file = fopen(path, "rb");

  if (file == NULL) {
    if (errno != ENOENT) LOG_ERR("Flash: cannot open %s: %s\n", path, strerror(errno));
    return false;
  }

  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    LOG_ERR("Flash: cannot size %s\n", path);
    fclose(file);
    return false;
  }

  if ((uint32_t)length != region->size) {
    LOG_ERR("Flash: %s is %ld bytes, expected %u\n", path, length, region->size);
    fclose(file);
    return false;
  }

  buffer = malloc(region->size);

  if (buffer == NULL) {
    fclose(file);
    LOG_ERR("Flash: cannot allocate %u bytes for %s\n", region->size, path);
    return false;
  }

  read = fread(buffer, 1, region->size, file);
  fclose(file);

  if (read != region->size) {
    LOG_ERR("Flash: short read from %s\n", path);
    free(buffer);
    return false;
  }

  if (uc_mem_write(ctx.uc, region->base, buffer, region->size) != UC_ERR_OK) {
    LOG_ERR("Flash: cannot load %s into guest memory\n", path);
    free(buffer);
    return false;
  }

  free(buffer);
  LOG_VBS("Flash: loaded %s\n", path);
  return true;
}

static bool flash_save_region(const FlashRegion *region) {
  char path[MAX_FILE_PATH];
  char temporary[MAX_FILE_PATH + 8];
  FILE *file;
  uint8_t *buffer;
  size_t written;
  int length;

  if (!flash_path(path, sizeof(path), region->name)) return false;

  length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);

  if (length < 0 || (size_t)length >= sizeof(temporary)) return false;

  buffer = malloc(region->size);

  if (buffer == NULL) {
    LOG_ERR("Flash: cannot allocate %u bytes for %s\n", region->size, path);
    return false;
  }

  if (uc_mem_read(ctx.uc, region->base, buffer, region->size) != UC_ERR_OK) {
    LOG_ERR("Flash: cannot read %s from guest memory\n", path);
    free(buffer);
    return false;
  }

  file = fopen(temporary, "wb");

  if (file == NULL) {
    LOG_ERR("Flash: cannot create %s: %s\n", temporary, strerror(errno));
    free(buffer);
    return false;
  }

  written = fwrite(buffer, 1, region->size, file);
  free(buffer);

  {
    bool ok = written == region->size && fflush(file) == 0;

    if (fclose(file) != 0) ok = false;

    if (!ok) {
      LOG_ERR("Flash: cannot write %s\n", temporary);
      (void)unlink(temporary);
      return false;
    }
  }

  (void)chmod(temporary, 0600);

  if (rename(temporary, path) != 0) {
    LOG_ERR("Flash: cannot replace %s: %s\n", path, strerror(errno));
    (void)unlink(temporary);
    return false;
  }

  LOG_VBS("Flash: saved %s\n", path);
  return true;
}

void FlashPersist_Apply(void) {
  bool missing = false;
  size_t i;

  if (!ctx.persist_flash || ctx.uc == NULL) return;

  if (ctx.flash_dir[0] == '\0' || !flash_ensure_dir()) {
    LOG_ERR("Flash: persistence disabled, cannot use the flash directory\n");
    ctx.persist_flash = false;
    return;
  }

  for (i = 0; i < sizeof(flash_regions) / sizeof(flash_regions[0]); i++) {
    if (!flash_load_region(&flash_regions[i])) missing = true;
  }

  if (missing) FlashPersist_Save();
}

void FlashPersist_Save(void) {
  size_t i;

  if (!ctx.persist_flash || ctx.uc == NULL) return;
  if (ctx.flash_dir[0] == '\0' || !flash_ensure_dir()) return;

  for (i = 0; i < sizeof(flash_regions) / sizeof(flash_regions[0]); i++) {
    (void)flash_save_region(&flash_regions[i]);
  }
}
