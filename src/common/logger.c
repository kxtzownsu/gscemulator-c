#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include <common/common.h>
#include <common/runtime/control.h>
#include <common/runtime/thread.h>

static pthread_mutex_t logger_lock = PTHREAD_MUTEX_INITIALIZER;

void log_internal(LogType logtype, const char *file, int line, const char *msg, ...) {
  const char *tag = "";
  va_list args;

  switch (logtype) {
    case NORMAL:
      tag = "INFO";
      break;
    case ERROR:
      tag = "ERROR";
      break;
    case VERBOSE:
      if (ctx.verbose) {
        tag = "VERBOSE";
      } else {
        return;
      }
      break;
    default:
      runtime_fault(RC_LOGGER_INVALID_TYPE, "invalid log type");
      return;
  }

  va_start(args, msg);

  pthread_mutex_lock(&logger_lock);

  if (tctx.name != NULL && tctx.name[0] != '\0') printf("[%s] ", tctx.name);
  printf("[%s] (%s:%d) ", tag, file, line);
  vprintf(msg, args);

  pthread_mutex_unlock(&logger_lock);

  va_end(args);
}
