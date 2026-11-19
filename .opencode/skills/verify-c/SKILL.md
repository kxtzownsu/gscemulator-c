---
name: Verify C changes
description: Verify better-gscemu C changes before finishing; use to run a clean warning-free build, CLI smoke tests, static analyzers, and a short libFuzzer run.
---

# Verify C changes

Run the bundled script:

```bash
.opencode/skills/verify-c/scripts/verify.sh
```

It checks:

- `make clean && make BUNDLED_FIRMWARE=0`
- `--help`, `--version`, a run without `--bootrom` exiting non-zero (source-only build), and `-r <bootrom> -v`
- an invalid chip exits non-zero
- an invalid `--console` mode exits non-zero
- `make fuzz FUZZ_TIME=1` when clang is available
- `gcc -fanalyzer` on `src/libargs.c`, `src/common/{file,essential-memory,memory-fault,version,vtor}.c`, `src/common/peripherals/component.c`, the `src/common/runtime/*.c` files, and the `src/H1B3C/peripherals/*.c` components, and `clang --analyze` on `src/main.c`

## Manual equivalent

```bash
make clean && make BUNDLED_FIRMWARE=0
BIN="build/$(uname -m)/gscemu"
"$BIN" --help
"$BIN" --version
"$BIN" || true                    # exits 252 without --bootrom
"$BIN" --console bogus || true    # exits 244
"$BIN" -r <bootrom> -v
"$BIN" --chip invalid || true
make fuzz FUZZ_TIME=1
```

## When to run the full pass

`verify.sh` is the full gate. Use it for threading, worker, runtime, console,
build-flag, or CLI changes.

For a single register addition to an existing component, skip it: run a
warning-free `make`, the boot smoke test, and `git diff --check`. A targeted
readback blob is enough extra coverage, and it also exercises the worker when
the component is threaded. Do not burn the full suite on one
`DEFINE_BASIC_REGISTER`.

For any register or range change, also load the `audit-mmio` skill and run its
checker. Boot success is not register coverage.

## Thread and console checks

```bash
make SINGLE_THREAD=1
./build/$(uname -m)/gscemu       # status 246, [control] logs
make tsan
./build/$(uname -m)/gscemu       # no ThreadSanitizer report
./build/$(uname -m)/gscemu --console pts -r <loop-rom> -f <loop-rom>
printf 'hello\n' | timeout -s INT 2 ./build/$(uname -m)/gscemu --console pts -r <loop-rom> -f <loop-rom>
```

A default build should show control/emu/UART/TRNG/KEYMGR tasks; `--console pts` adds the console task. `--console none` is the default and creates no console thread. PMU is inline, so its logs come from `[emu]`.

## Bundled builds

Plain `make` uses `BUNDLED_FIRMWARE=1` and needs `src/<CHIP>/firmware/bootrom.bin`
and `src/<CHIP>/firmware/firmware.bin` (or `BUNDLED_<CHIP>_*` overrides). The
generated header defines `BUNDLED_<CHIP>`; H1B3C only uses bundled images when
`BUNDLED_H1B3C` is present and the matching path is empty. Otherwise omitting
`-r` exits 252. The verifier uses `BUNDLED_FIRMWARE=0` so it works in a clean
checkout.

## Rules

- A normal build must be warning-free. Do not suppress warnings to get a pass.
- If clang is missing, report that fuzzing was skipped instead of silently passing.
- Sanitizer or analyzer findings are bugs. Fix them or report them with the command and output.

See [checks](references/checks.md) for expected results and optional longer runs.
