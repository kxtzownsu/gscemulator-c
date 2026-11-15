#include <string.h>

#include <common/common.h>

Ctx ctx;

void ctx_init(void) {
  memset(&ctx, 0, sizeof(ctx));
}
