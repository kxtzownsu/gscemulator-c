#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <common/common.h>

static FileRC file_rc_from_errno(int err) {
  return (err == EACCES || err == EPERM) ? FILE_PERMISSION_DENIED : FILE_OPEN_FAILED;
}

FileRC file_readbuf(const char *path, void *buf, size_t buffer_size) {
  FILE *f;
  long length;
  size_t to_read;
  size_t read = 0;
  int err;

  if (path == NULL || path[0] == '\0' || buf == NULL || buffer_size == 0) {
    LOG_ERR("Invalid arguments passed to file_readbuf\n");
    return FILE_INVALID_ARGUMENT;
  }

  f = fopen(path, "rb");
  if (f == NULL) {
    err = errno;
    LOG_ERR("Failed to open %s: %s\n", path, strerror(err));
    return file_rc_from_errno(err);
  }

  if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
    err = errno;
    LOG_ERR("Failed to get the size of %s: %s\n", path, strerror(err));
    fclose(f);
    return FILE_SIZE_ERROR;
  }

  to_read = buffer_size - 1;
  if ((uintmax_t)length < (uintmax_t)to_read) to_read = (size_t)length;

  if (to_read > 0) read = fread(buf, 1, to_read, f);

  if (read != to_read) {
    if (ferror(f)) {
      err = errno;
      LOG_ERR("Failed to read %s: %s\n", path, strerror(err));
      fclose(f);
      return FILE_READ_FAILED;
    }
    LOG_ERR("File %s changed while reading\n", path);
    fclose(f);
    return FILE_SIZE_ERROR;
  }

  ((char *)buf)[read] = '\0';

  if (fclose(f) != 0) {
    err = errno;
    LOG_ERR("Failed to close %s: %s\n", path, strerror(err));
    return FILE_READ_FAILED;
  }

  return FILE_SUCCESS;
}

FileRC file_writebuf(const char *path, const void *buf, size_t buffer_size) {
  FILE *f;
  size_t to_write;
  size_t written = 0;
  int err;

  if (path == NULL || path[0] == '\0' || buf == NULL || buffer_size == 0) {
    LOG_ERR("Invalid arguments passed to file_writebuf\n");
    return FILE_INVALID_ARGUMENT;
  }

  to_write = buffer_size - 1;

  f = fopen(path, "wb");
  if (f == NULL) {
    err = errno;
    LOG_ERR("Failed to open %s: %s\n", path, strerror(err));
    return file_rc_from_errno(err);
  }

  if (to_write > 0) written = fwrite(buf, 1, to_write, f);

  if (written != to_write) {
    err = errno;
    LOG_ERR("Failed to write %s: %s\n", path, strerror(err));
    fclose(f);
    return FILE_WRITE_FAILED;
  }

  if (fclose(f) != 0) {
    err = errno;
    LOG_ERR("Failed to close %s: %s\n", path, strerror(err));
    return FILE_WRITE_FAILED;
  }

  return FILE_SUCCESS;
}
