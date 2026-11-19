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
- `./build/$(uname -m)/gscemu -r <bootrom> -v` runs the H1B3C lifecycle and prints verbose logs prefixed with the thread role (`[emu]`, `[control]`, ...). No peripheral uses a worker today, so PMU logs come from `[emu]`.
- A default build has 2 tasks (control, emu); `--console pts` adds the console task; `make SINGLE_THREAD=1` has 1.
- The current H1B3C boot stops at the unmapped write at `0x400600c4` with status 246.
- `./build/$(uname -m)/gscemu --console pts -r <loop-rom> -f <loop-rom>` prints the pty slave path and relays stdin/stdout through it.
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
clang --analyze -Iinclude -std=c11 -Wall -Wextra src/main.c -o /dev/null
```

Treat analyzer warnings as real until proven otherwise. Do not add blanket suppressions.
