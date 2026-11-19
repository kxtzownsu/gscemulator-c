# Checks

## Expected results

- `make clean && make BUNDLED_FIRMWARE=0` exits 0 with no warnings; `make clean` removes only the target and objects.
- `make clean && make` exits 0 when `src/<CHIP>/firmware/{bootrom,firmware}.bin` or the matching `BUNDLED_<CHIP>_*` overrides exist; without them it fails at the bundled-header target.
- `make fullclean` removes `build/<ARCH>`, including the local unicorn build.
- `./build/$(uname -m)/gscemu --help` exits 0 and prints usage plus registered flags, including `--bootrom`, `--firmware`, `--console`, and `--version`.
- `./build/$(uname -m)/gscemu --version` exits 0 and prints the program name/version, `Build Date:`, `Chips:`, and `Bundled firmware:`.
- With `BUNDLED_FIRMWARE=0`, `./build/$(uname -m)/gscemu` without `-r` exits with `RC_CHIPEMU_FAILED_INIT` (252).
- `./build/$(uname -m)/gscemu --console bogus` exits with `RC_ARG_CONSOLE_INVALID` (244).
- If the header defines `BUNDLED_H1B3C`, omitting `-r` uses `BUNDLED_H1B3C_BOOTROM_BIN`; otherwise it exits 252. The exit status depends on the bundled image.
- `./build/$(uname -m)/gscemu -r <bootrom> -f <firmware> -v` runs the H1B3C lifecycle and prints verbose logs prefixed with the thread role (`[emu]`, `[UART]`, `[TRNG]`, `[KEYMGR]`, `[control]`, ...). PMU is inline and logs as `[emu]`; UART, TRNG, and KEYMGR are the workers today.
- Set `VERIFY_BOOTROM` and `VERIFY_FIRMWARE` to exercise the full boot in `verify.sh`; it waits up to 20s and requires `Inits done` in the output. The check runs without `-v` because per-access verbose logging slows emulation by more than an order of magnitude.
- An interactive default build has 6 tasks (control, emu, console, UART, TRNG, KEYMGR); a non-TTY default and `--console none` have 5; `make SINGLE_THREAD=1` has 1.
- The current H1B3C boot prints the banner, `exp ?36`, `Himg`/`Hfss`/`Hinf`, `exp ?40`, `exp ?40`, `exp ?59`, `jump @00040400`, `Bldr |<n>` (a nonzero 24 MHz cycle count), `retry|0`, `oops?|no`, the loader `Himg`/`Hfss`/`Hinf` lines with `: 1`, `devm :0`, `jump @44000`, then the RW banner (`--- UART initialized after reboot ---`, reset cause, image, `Inits done`), the strap readings, `Running in FIPS 140-2 approved mode`, the `Console is enabled` banner, and keeps running until interrupted.
- `./build/$(uname -m)/gscemu --console pts -r <loop-rom> -f <loop-rom>` creates a pty, prints the slave path, and routes stdin/stdout and UART through it.
- `make SINGLE_THREAD=1` runs emulation inline on the control thread and rejects `--console pts` with 244.
- `make tsan` builds and the boot smoke test produces no ThreadSanitizer report.
- `./build/$(uname -m)/gscemu -f <firmware> -r <bootrom>` accepts `--firmware` and loads it into the firmware region.
- `./build/$(uname -m)/gscemu --chip invalid` exits with `RC_ARG_CHIP_INVALID` (254).
- `make fuzz FUZZ_TIME=1` exits 0 with no ASan/UBSan report.

## Longer runs

```bash
make fuzz FUZZ_TIME=60
make fuzz FUZZ_ARGS='-runs=1000000'
make fuzz FUZZ_TIME=0
```

Use a longer run after parser rewrites, allocation changes, or new CLI syntax.

## Static analysis

```bash
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/libargs.c -o /tmp/libargs.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/file.c -o /tmp/file.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/essential-memory.c -o /tmp/essential-memory.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/memory-fault.c -o /tmp/memory-fault.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/sha256.c -o /tmp/sha256.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/version.c -o /tmp/version.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/vtor.c -o /tmp/vtor.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/peripherals/component.c -o /tmp/component.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/runtime/control.c -o /tmp/control.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/runtime/console.c -o /tmp/console.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/runtime/thread.c -o /tmp/thread.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/common/runtime/worker.c -o /tmp/worker.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/m3.c -o /tmp/m3.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/fuses.c -o /tmp/fuses.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/globalsec.c -o /tmp/globalsec.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/pmu.c -o /tmp/pmu.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/pinmux.c -o /tmp/pinmux.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/uart.c -o /tmp/uart.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/trng.c -o /tmp/trng.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/swdp.c -o /tmp/swdp.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/xo.c -o /tmp/xo.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/gpio.c -o /tmp/gpio.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/keymgr.c -o /tmp/keymgr.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -pthread -fanalyzer -c src/H1B3C/peripherals/flash.c -o /tmp/flash.o
clang --analyze -Iinclude -std=c11 -Wall -Wextra src/main.c -o /dev/null
```

Treat analyzer warnings as real until proven otherwise. Do not add blanket suppressions.
