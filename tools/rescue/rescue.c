#define _GNU_SOURCE

#include "shaft.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s --tty PATH --firmware FILE [--timeout SEC] [--retries N] [-v]\n",
          program);
}

int main(int argc, char **argv) {
  static const struct option options[] = {
    { "tty", required_argument, NULL, 't' },
    { "firmware", required_argument, NULL, 'f' },
    { "timeout", required_argument, NULL, 'T' },
    { "retries", required_argument, NULL, 'r' },
    { "verbose", no_argument, NULL, 'v' },
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 },
  };
  const char *tty = NULL;
  const char *firmware = NULL;
  unsigned timeout = 10;
  unsigned retries = 5;
  int verbose = 0;
  int option;

  while ((option = getopt_long(argc, argv, "t:f:T:r:vh", options, NULL)) != -1) {
    switch (option) {
      case 't':
        tty = optarg;
        break;
      case 'f':
        firmware = optarg;
        break;
      case 'T':
        timeout = (unsigned)strtoul(optarg, NULL, 0);
        break;
      case 'r':
        retries = (unsigned)strtoul(optarg, NULL, 0);
        break;
      case 'v':
        verbose = 1;
        break;
      case 'h':
        usage(argv[0]);
        return 0;
      default:
        usage(argv[0]);
        return 1;
    }
  }

  if (tty == NULL || firmware == NULL || timeout == 0) {
    usage(argv[0]);
    return 1;
  }

  return shaft_rescue(tty, firmware, timeout * 1000u, retries, verbose);
}
