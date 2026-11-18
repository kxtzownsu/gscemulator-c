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
- `make fuzz FUZZ_TIME=1` when clang is available
- `gcc -fanalyzer` on `src/libargs.c`, `src/common/file.c`, `src/common/essential-memory.c`, `src/common/memory-fault.c`, `src/common/version.c`, `src/common/vtor.c`, `src/common/peripherals/component.c`, and the `src/H1B3C/peripherals/*.c` components, and `clang --analyze` on `src/main.c`

## Manual equivalent

```bash
make clean && make BUNDLED_FIRMWARE=0
BIN="build/$(uname -m)/gscemu"
"$BIN" --help
"$BIN" --version
"$BIN" || true                    # exits 252 without --bootrom
"$BIN" -r <bootrom> -v
"$BIN" --chip invalid || true
make fuzz FUZZ_TIME=1
```

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
