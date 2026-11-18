#include <common/version.h>

#include <stdio.h>

void print_version(void) {
  printf("%s %s (%s)", GSCEMU_NAME, GSCEMU_VERSION, GSCEMU_TOOLCHAIN);
  if (GSCEMU_UNICORN[0] != '\0') printf(" %s", GSCEMU_UNICORN);
  printf("\n");
  printf("Build Date: %s (%s)\n", GSCEMU_BUILD_DATE, GSCEMU_BUILD_UNIX_TS);
  printf("Chips: %s\n", GSCEMU_CHIPS);
  printf("Bundled firmware: %s\n", GSCEMU_BUNDLED_FIRMWARE);
}
