#ifndef FILE_H
#define FILE_H

#include <stddef.h>

typedef enum {
  FILE_SUCCESS = 0,
  FILE_PERMISSION_DENIED = 1,
  FILE_SIZE_ERROR = 2,
  FILE_OPEN_FAILED = 3,
  FILE_READ_FAILED = 4,
  FILE_WRITE_FAILED = 5,
  FILE_INVALID_ARGUMENT = 6
} FileRC;

FileRC file_readbuf(const char *path, void *buf, size_t buffer_size);
FileRC file_writebuf(const char *path, const void *buf, size_t buffer_size);

#endif /* FILE_H */
