#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

TMP_O="$(mktemp)"
FUZZ_LOG="$(mktemp)"
trap 'rm -f "$TMP_O" "$FUZZ_LOG"' EXIT

make clean
make BUNDLED_FIRMWARE=0

BIN="build/$(uname -m)/gscemu"

"$BIN" --help >/dev/null
"$BIN" --version | grep -q '^Chips:'
if "$BIN" >/dev/null 2>&1; then
  echo "verify-c: run without --bootrom unexpectedly succeeded" >&2
  exit 1
fi

if "$BIN" --console bogus >/dev/null 2>&1; then
  echo "verify-c: invalid console mode unexpectedly succeeded" >&2
  exit 1
fi

if [ -n "${VERIFY_BOOTROM:-}" ] && [ -n "${VERIFY_FIRMWARE:-}" ]; then
  set +e
  timeout -s INT 20 "$BIN" -r "$VERIFY_BOOTROM" -f "$VERIFY_FIRMWARE" >"$TMP_O" 2>&1
  boot_rc=$?
  set -e

  if [ "$boot_rc" -ne 124 ] && [ "$boot_rc" -ne 245 ]; then
    cat "$TMP_O" >&2
    echo "verify-c: boot lifecycle exited with $boot_rc" >&2
    exit 1
  fi

  if ! grep -q "Inits done" "$TMP_O"; then
    cat "$TMP_O" >&2
    echo "verify-c: boot lifecycle did not reach Inits done" >&2
    exit 1
  fi
else
  echo "verify-c: VERIFY_BOOTROM/VERIFY_FIRMWARE not set, skipped the boot lifecycle" >&2
fi

if "$BIN" --chip invalid >/dev/null 2>&1; then
  echo "verify-c: invalid chip unexpectedly succeeded" >&2
  exit 1
fi

if command -v clang >/dev/null 2>&1; then
  if ! make fuzz FUZZ_TIME=1 >"$FUZZ_LOG" 2>&1; then
    cat "$FUZZ_LOG" >&2
    exit 1
  fi
else
  echo "verify-c: clang not found, skipped fuzzing" >&2
fi

if command -v gcc >/dev/null 2>&1; then
  ANALYZE=(gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer)
  for src in \
    src/libargs.c \
    src/common/file.c \
    src/common/essential-memory.c \
    src/common/memory-fault.c \
    src/common/sha256.c \
    src/common/aes.c \
    src/common/version.c \
    src/common/vtor.c \
    src/common/peripherals/component.c \
    src/common/runtime/control.c \
    src/common/runtime/console.c \
    src/common/runtime/thread.c \
    src/common/runtime/worker.c \
    src/H1B3C/peripherals/m3.c \
    src/H1B3C/peripherals/fuses.c \
    src/H1B3C/peripherals/globalsec.c \
    src/H1B3C/peripherals/pmu.c \
    src/H1B3C/peripherals/pinmux.c \
    src/H1B3C/peripherals/uart.c \
    src/H1B3C/peripherals/trng.c \
    src/H1B3C/peripherals/swdp.c \
    src/H1B3C/peripherals/xo.c \
    src/H1B3C/peripherals/gpio.c \
    src/H1B3C/peripherals/keymgr.c \
    src/H1B3C/peripherals/flash.c \
    src/H1B3C/peripherals/rtc.c \
    src/H1B3C/peripherals/timels.c \
    src/H1B3C/peripherals/watchdog.c \
    src/H1B3C/peripherals/rbox.c \
    src/H1B3C/peripherals/crypto.c \
    src/H1B3C/peripherals/crypto-engine.c \
    src/H1B3C/peripherals/rdd.c \
    src/H1B3C/peripherals/spi.c; do
    "${ANALYZE[@]}" -c "$src" -o "$TMP_O"
  done
fi

if command -v clang >/dev/null 2>&1; then
  clang --analyze -Iinclude -std=c11 -Wall -Wextra src/main.c -o /dev/null
fi

echo "verify-c: ok"
