# Checks

## Expected results

- `make clean && make BUNDLED_FIRMWARE=0` exits 0 with no warnings; `make clean` removes only the target and objects.
- `make clean && make` exits 0 when `src/<CHIP>/firmware/{bootrom,firmware}.bin` or the matching `BUNDLED_<CHIP>_*` overrides exist; without them it fails at the bundled-header target.
- `make fullclean` removes `build/<ARCH>`, including the local unicorn build.
- `./build/$(uname -m)/gscemu --help` exits 0 and prints usage plus registered flags, including `--bootrom` and `--firmware`.
- With `BUNDLED_FIRMWARE=0`, `./build/$(uname -m)/gscemu` without `-r` exits with `RC_CHIPEMU_FAILED_INIT` (252).
- If the header defines `BUNDLED_H1B3C`, omitting `-r` uses `BUNDLED_H1B3C_BOOTROM_BIN`; otherwise it exits 252. The exit status depends on the bundled image.
- `./build/$(uname -m)/gscemu -r <bootrom> -v` runs the H1B3C lifecycle and prints verbose logs.
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
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/libargs.c -o /tmp/libargs.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/common/file.c -o /tmp/file.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/common/essential-memory.c -o /tmp/essential-memory.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/common/memory-fault.c -o /tmp/memory-fault.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/common/vtor.c -o /tmp/vtor.o
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/common/peripherals/component.c -o /tmp/component.o
clang --analyze -Iinclude -std=c11 -Wall -Wextra src/main.c -o /dev/null
```

Treat analyzer warnings as real until proven otherwise. Do not add blanket suppressions.
