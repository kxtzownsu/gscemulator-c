# AGENTS.md

`better-gscemu` is a standalone C11 reimplementation of GSCEmulator. It parses a small CLI, selects a chip backend, and runs an emulator lifecycle. Only H1B3C has a backend today; `h1c3m`, `h1d3c`, `h1d3m`, and `npcx` are placeholders in the help text.

## Commands

```bash
make                         # build build/$(uname -m)/gscemu
make clean                   # remove gscemu and its objects
make fullclean               # remove build/<ARCH>, including unicorn and fuzz
make STATIC=1                # static link against the local unicorn
make fuzz                    # clang/libFuzzer + ASan/UBSan, 60s default
make fuzz FUZZ_TIME=300      # longer run
make fuzz FUZZ_TIME=0        # run until interrupted
make fuzz FUZZ_ARGS='-runs=1000000'
```

`make` also regenerates `include/common/bundled-firmware.h` from one or more
chip firmware pairs. Set `BUNDLED_CHIPS="H1B3C H1C3M"` (where `H1B3C` and `H1C3M` are placeholders for the specific chips) to bundle multiple chips;
each chip reads `src/<CHIP>/firmware/bootrom.bin` and
`src/<CHIP>/firmware/firmware.bin` by default. Override a pair with
`BUNDLED_<CHIP>_BOOTROM_BIN=<path> BUNDLED_<CHIP>_FIRMWARE_BIN=<path>`, or run
`scripts/generate_firmware_header.sh` directly with repeated `--chip` groups.
The inputs are local-only and gitignored; the generated header is gitignored
and included only under `#ifdef BUNDLED_FIRMWARE`. Each bundled chip also gets
`#define BUNDLED_<CHIP> 1`; `BUNDLED_FIRMWARE` only gates the header, while a
backend uses bundled data only when its own `BUNDLED_<CHIP>` is defined.

`BUNDLED_FIRMWARE` defaults to `1`. That adds `-DBUNDLED_FIRMWARE`, makes the
bundled header a build dependency, and lets a bundled chip fall back to its
arrays when `-r`/`-f` is omitted. Use `make BUNDLED_FIRMWARE=0` for a source-only
build with no firmware inputs.

`STATIC=1` builds `build/$(uname -m)/unicorn/libunicorn.a` from
`third_party/unicorn` and links that instead of the system `-lunicorn`. It adds
`-Ithird_party/unicorn/include`, `-static`, and `-lpthread -lm`. The local clone
lives in `third_party/unicorn` (gitignored); `make fullclean` removes the built
static library under `build/<ARCH>`.

Run the binary directly:

```bash
./build/$(uname -m)/gscemu -r <bootrom>
./build/$(uname -m)/gscemu --help
./build/$(uname -m)/gscemu --chip h1b3c -r <bootrom> -v
```

With a bundled build, `-r` is optional and the binary falls back to `BUNDLED_H1B3C_BOOTROM_BIN`.

`make run` builds and runs the target directly.

- Normal builds use `gcc`; fuzzing uses `clang`.
- A normal build must stay warning-free. Do not add `-w` or disable format warnings.
- There is no unit-test suite yet. Fuzzing plus a manual CLI smoke run is the current check.

## Layout

- `src/main.c` - CLI flow, chip selection, and emulator lifecycle.
- `src/libargs.c`, `include/libargs.h` - argv parser and help printer.
- `src/common/` - chip-agnostic emulator interface, ctx, return codes, file I/O, essential memory, peripheral framework, exit handling, logger.
- `src/H1B3C/` - Haven chip backend (H1B3C).
- `src/H1B3C/peripherals/` - H1B3C MMIO components and their init table.
- `scripts/generate_firmware_header.sh` - bundles one or more BootROM/firmware pairs into `include/common/bundled-firmware.h`.
- `scripts/generate_version_header.sh` - generates `include/common/version.h` at build time.
- `scripts/make/` - modular build logic (`toolchain.mk`, `variables.mk`, `setup.mk`, `firmware.mk`, `unicorn.mk`, `version.mk`, `flags.mk`, `build.mk`, `fuzz.mk`, `clean.mk`).
- `tests/` - libFuzzer harness, dictionary, and seed corpus.
- `include/` - public headers; implementation stays in `src/`.
- `.opencode/skills/` - repo-local skills for fuzzing, chip work, libargs, and verification.

Examples belong in `examples/`. Reference material belongs in `reference/`, which is ignored. `flash/` and `build/` are generated or local-only; never edit or commit them.

## Runtime contract

CLI:

- `--chip` / `-c` selects a chip. Omitting it means `h1b3c`.
- `--bootrom` / `-r` copies the BootROM path into `ctx.firmware.bootrom` and clears `ctx.firmware.useBundledROM`. A path longer than `MAX_FILE_PATH - 1` exits with `RC_ARG_PATH_TOO_LONG`.
- `--firmware` / `-f` copies the firmware path into `ctx.firmware.progfw` and clears `ctx.firmware.useBundledFW`. H1B3C loads it into the firmware region. A path longer than `MAX_FILE_PATH - 1` exits with `RC_ARG_PATH_TOO_LONG`.
- With a chip that defines `BUNDLED_<CHIP>`, omitting `-r`/`-f` uses that chip's bundled arrays. Otherwise the file path is required; an empty path exits with `RC_CHIPEMU_FAILED_INIT`. With `BUNDLED_FIRMWARE=0`, nothing is bundled.
- `--verbose` / `-v` enables verbose logging.
- `--version` prints the build version and exits 0. The output includes the program name, `0.1.0-<commit>[-dirty]`, toolchain, unicorn version/commit, build date/unix timestamp, available chips, and bundled firmware.
- Unknown chips exit with failure. Do not silently map them to H1B3C.
- `-h` / `--help` prints help and exits 0 through `argcheck_check_help` or `argcheck_destroy`.

libargs:

- `argcheck_new` deep-copies `argv`; `argcheck_destroy` frees everything.
- `argcheck_fequals` and `argcheck_fequals_str` return pointers into `ArgCheck` storage. They are valid only until `argcheck_destroy`. `argcheck_fequals` returns NULL when absent; `argcheck_fequals_str` returns `""`.
- `argcheck_fbool` returns 0 or 1.
- Every lookup also registers the flag for help output. Keep long names, short names, and descriptions stable once used.
- `argcheck_destroy` is not idempotent. Do not call it twice.

Files:

- `common/file.h` owns `FileRC`, `file_readbuf`, and `file_writebuf`.
- `file_readbuf(path, buf, buffer_size)` reads at most `buffer_size - 1` bytes into `buf` and NUL-terminates at `buf[read]`, so the last buffer byte holds the terminator when the file is truncated. It never allocates and leaves bytes past the terminator untouched.
- `file_writebuf(path, buf, buffer_size)` creates or truncates `path` and writes exactly `buffer_size - 1` bytes; `buffer_size` includes the buffer's trailing NUL.
- Both validate `path`, `buf`, and `buffer_size` before touching the file and return `FILE_INVALID_ARGUMENT` for bad input. `buffer_size == 0` and a `NULL` buffer are always invalid because the terminator needs room.
- `FILE_PERMISSION_DENIED` covers `EACCES` and `EPERM`; other failures map to the specific cause.
- These helpers log and return a code. They never call `emu_exit`; the caller decides whether a failure is fatal.

Essential memory:

- `common/essential-memory.h` defines `EssentialMemoryEntry` (`NAME`, `base_addr`, `size`, `perms`, `role`) plus `map_essential_memory`, `load_firmware`, and `load_bundled_binary`.
- Each chip exposes a table terminated by an entry with an empty `NAME` and assigns it to `ctx.essential_memory`, e.g. `H1B3C_EssentialMemoryArray` in `include/H1B3C/memory.h`. Mark exactly one entry `ESSENTIAL_MEMORY_ROM` and one `ESSENTIAL_MEMORY_FIRMWARE`.
- `map_essential_memory` maps every entry with `uc_mem_map` before any flash writes or `uc_emu_start`. Regions must not overlap.
- `load_firmware(bundled_rom, bundled_rom_len, bundled_fw, bundled_fw_len)` finds the ROM and firmware roles, pre-fills each destination region with `0xFF`, loads the image from its `ctx.firmware` path or the bundled array according to `useBundledROM`/`useBundledFW`, and writes it with `uc_mem_write`. Missing roles exit `RC_CHIPEMU_FAILED_INIT`.
- Permissions use `UC_PROT_*`; H1B3C maps ROM RX, SRAM RW, INFO0/INFO1 RW, and PROG RWX. `Havn_Initialize` sets `useBundledROM`/`useBundledFW` when `BUNDLED_H1B3C` is defined and the matching path is empty, then calls `load_firmware` with the `BUNDLED_H1B3C_*` arrays (or NULLs when the chip is not bundled).

Memory faults:

- `common/memory-fault.h` owns `install_memory_fault_hook`, which registers `UC_HOOK_MEM_{READ,WRITE,FETCH}_INVALID` on `ctx.uc`.
- The hook logs `Unmapped` or `Protected`, the access type, address, size, write value, and PC. It returns; `uc_emu_start` still fails and the backend exits.
- Install it after `uc_open` and before `uc_emu_start`. Never call `emu_exit` or `uc_close` from the hook.

Peripherals:

- `common/peripherals/component.h` defines `Component` (`name`, `state`, `read`, `write`) and `MmioRegion` (`component`, `base`, `size`).
- `component_register` appends a component to `ctx.components`; `component_find` looks one up by name. `COMPONENT_MAX` bounds the registry.
- `component_map_mmio` maps each `MmioRegion` with `uc_mmio_map`. Component handlers receive an offset relative to the region base plus the access size, and must return `uint64_t`.
- `component_log_unhandled` logs unknown MMIO offsets with the access type, offset, component name, and PC. Use it in per-component default cases; never exit from a component callback.
- `include/H1B3C/register_map.h` is the single source for peripheral bases, region sizes, and register offsets. Registers are 4-byte aligned (`0x0`, `0x4`, `0x8`, ...).
- Components are registered and mapped by `initialize_peripherals()` before `ctx.chipemu.ready` is set and before `uc_emu_start`.
- Never call `emu_exit` or `uc_close` from an MMIO callback. Log and return 0, or set a fault flag and call `uc_emu_stop` so cleanup happens after `uc_emu_start` returns.
- `DEFINE_BASIC_REGISTER` is file-scope singleton only. Components that need per-instance state use `Component.state`.

Emulator:

- `get_chipemu(chip)` fills `ctx.chipemu` with `initialize`, `start_emulation`, and `end_emulation`.
- `initialize()` must set `ctx.chipemu.ready = true` only after successful setup. `main` exits when `ready` is false.
- `start_emulation()` and `end_emulation()` take no arguments and use `ctx`. `end_emulation` must tolerate a partially initialized backend because `emu_exit` may call it on failure.
- `set_sp_pc(uc, vtor_base)` reads SP from `vtor_base + 0x0` and PC from `vtor_base + 0x4`, writes `UC_ARM_REG_SP`/`UC_ARM_REG_PC`, and exits with the VTOR `RC_*` codes on read failure. It does not set the Thumb bit; `start_emulation` does.
- `emu_exit(Rc rc)` is `_Noreturn`: it calls `end_emulation` when one is set, destroys `ctx.ac`, and exits with `rc`. It clears `ctx.chipemu.end_emulation` and `ctx.ac` before running them, so a nested `emu_exit` cannot double-close or double-free. Never call `exit()` directly.

Context:

- `common/ctx.h` exposes one global `Ctx ctx` holding `ac`, `chip`, `chipemu`, `uc`, `essential_memory`, `firmware`, `components`, and `verbose`.
- `ctx.firmware` holds `bootrom`, `progfw`, `infobnk`, and the `useBundledROM`/`useBundledFW` flags. `main` fills the paths and flags from `--bootrom`/`--firmware`; the backend decides whether to read a file or use the bundled arrays.
- `ctx_init` zeroes the container; `main` calls it before touching anything else.
- `ctx.verbose` is the only verbose switch. `LOG_VBS` reads it; do not add a second flag.
- Ownership follows the field: `argcheck_destroy` frees `ctx.ac`, and a backend's `end_emulation` closes `ctx.uc` and sets it to NULL. Never destroy or close the same resource twice.

Return codes:

- `common/rc.h` defines `Rc`. Every `emu_exit` call, `exit` call, and `main` return uses an `RC_*` value; never pass a raw number.
- `RC_SUCCESS` covers a normal run and the help path.
- `RC_ARGCHECK_FAILED`, `RC_ARG_CHIP_INVALID`, `RC_ARG_PATH_TOO_LONG`, `RC_CHIPEMU_FAILED_INIT`, `RC_CHIPEMU_UNSUPPORTED`, `RC_CHIPEMU_FAILED_VTOR_SP_READ`, `RC_CHIPEMU_FAILED_VTOR_PC_READ`, `RC_CHIPEMU_EMULATION_ERROR`, and `RC_LOGGER_INVALID_TYPE` cover failures.
- Values are negative, so the process status is the low byte (`RC_ARG_CHIP_INVALID` reports as 254). Add a new `RC_*` value instead of reusing an unrelated one.

Version:

- `include/common/version.h` is generated by `scripts/generate_version_header.sh` and gitignored. Only `src/common/version.c` includes it; `common/common.h` exposes `print_version`.
- A `build/<ARCH>/.build-config` stamp holds `STATIC`, `BUNDLED_FIRMWARE`, `BUNDLED_CHIPS`, `CC`, `CHIPS`, and `PROGRAM_VERSION`. The stamp is rewritten only when its content changes, so a rebuild that would only change the build date does not recompile or relink. Changing `STATIC` (or any other config value in the stamp) rebuilds every object.
- `--version` prints `GSCEMU_NAME`, `GSCEMU_VERSION` (`0.1.0-<commit>[-dirty]`), toolchain, unicorn version/commit, build date/unix timestamp, available chips, and bundled firmware.
- `PROGRAM_VERSION` lives in `scripts/make/version.mk`; `CHIPS` lists available chips; the bundled summary comes from `BUNDLED_CHIPS`.

## Security rules

- Treat ROMs, CLI arguments, paths, guest data, and MMIO writes as untrusted.
- Validate MMIO offsets and sizes before using them to index state; unknown offsets must not write outside the component's state.
- Never pass untrusted data as a `printf`/`LOG*` format string. Use `LOG("%s", value)`. The Makefile makes non-literal formats a build error.
- Check every allocation. Use `size_t` and overflow guards for allocation arithmetic.
- Keep the public API opaque. Do not expose internal structs in headers.
- Do not add network access, host device I/O, or destructive operations without explicit approval.
- Do not claim a chip is implemented until it has a backend wired through `get_chipemu` and a smoke-tested boot path.

## Code style

- C11, Google C style, 2-space indentation.
- Opening brace on the same line. Keep function signatures on one line when practical; do not split parameters just to fit a column limit.
- No comments unless the code is genuinely hard to follow. Use `/* ... */`, never `//`.
- Include order: own header first where applicable, then standard headers, then project headers, with blank lines between groups.
- Source files use `"header.h"` for their own header and `<module/header.h>` for project headers.
- Headers use include guards, include what they use, and keep C++ guards only where the public C API needs them.
- Prefer `snprintf` over `sprintf` or `strcpy`. Check truncation where the result matters.
- Keep chip-specific logic in the chip backend. `src/common/` must not include `H1B3C` or future chip headers.
- When an interface changes, update the header, every implementation, `main.c`, and the fuzz harness in the same change.
- New `src/**/*.c` files are picked up automatically by the Makefile, except `src/H1B3C/firmware/`, which is target firmware and is excluded from the host build.

### Makefile

- Build logic lives in `scripts/make/*.mk`; the top-level `Makefile` only sets `project_name`, `GOAL`, includes the modules, and sets `.DEFAULT_GOAL`/`.PHONY`.
- `toolchain.mk` owns host detection, `ARCH`, `Q`, the shell, and tool paths. `variables.mk` owns directories, source discovery, and fuzz paths. `flags.mk` owns compile/link flags. `build.mk` owns the target/object rules.
- `ARCH` selects `build/<ARCH>`; it defaults to `uname -m`.
- Recipes are silent by default and print a two-space label. Use `make VERBOSE=1` to show the full commands.
- Recipe lines use tabs; variable assignments and indentation use 2 spaces.
- Keep hardening flags and format warnings. Do not disable them to make a build pass.
- Put build products under `build/<arch>/`. `make clean` removes only `gscemu`, `build/<ARCH>/obj`, and the generated version header; `make fullclean` removes the whole `build/<ARCH>` (including the local unicorn build) plus the version header.
- Add fuzz paths through the `FUZZ_*` variables instead of hardcoding them.
- `BUNDLED_FIRMWARE ?= 1` adds `-DBUNDLED_FIRMWARE` and makes `include/common/bundled-firmware.h` a build dependency. `make BUNDLED_FIRMWARE=0` disables both.
- `BUNDLED_CHIPS` selects the firmware pairs; each pair defaults to `src/<CHIP>/firmware/{bootrom,firmware}.bin`. The generated header defines `BUNDLED_<CHIP>` per chip; backends gate bundled use on that macro, not on `BUNDLED_FIRMWARE`.
- `STATIC=1` replaces the pkg-config unicorn flags with `third_party/unicorn/include`, `$(UNICORN_STATIC_LIB)`, `-static`, and `-lpthread -lm`.
- `LOCAL_UNICORN=1` only builds the local static library; it does not switch the headers or link. Use `STATIC=1` for a local static link.
- `SRCS` filters out `src/H1B3C/firmware/`; those files build for the emulated target, not the host. Do not add them to the host objects.

## Verification

```bash
make clean && make BUNDLED_FIRMWARE=0
./build/$(uname -m)/gscemu --help
./build/$(uname -m)/gscemu --chip h1b3c -r <bootrom> -v
make fuzz FUZZ_TIME=1
```

For a bundled build, provide `src/<CHIP>/firmware/{bootrom,firmware}.bin` or the matching `BUNDLED_<CHIP>_*` overrides and run plain `make`.

Run the fuzz target after touching `libargs`, CLI parsing, or the fuzz harness.

Optional static checks:

```bash
gcc -Iinclude -std=c11 -Wall -Wextra -Wformat=2 -fanalyzer -c src/libargs.c -o /tmp/libargs.o
clang --analyze -Iinclude -std=c11 -Wall -Wextra src/main.c -o /dev/null
```

A normal build must produce no warnings. Sanitizer or fuzzer findings are bugs; fix them instead of suppressing them.

## Commit messages

History uses signed commits and a short prefix:

```text
feat: add h1c3m chip skeleton
fix: guard libargs flag growth overflow
test: extend fuzzer for --chip
docs: document emulator lifecycle
```

- Lowercase imperative summary, 45-67 characters, no trailing period.
- Add a body when the reason or behavior change is not obvious.
- Always add `Signed-off-by: kxtzownsu <kxtzownsu@gmail.com>`.
- No emoji, `WIP`, bracketed prefixes, vague summaries, or AI/tooling attribution.
- Do not rewrite unrelated history or squash commits unless asked.

## Working rules

- Read the current files and `git log` before changing behavior.
- Apply changes directly to files; do not just describe them.
- Preserve user edits and avoid unrelated cleanup.
- Follow the nearest `AGENTS.md` for the directory you are editing.
- Keep `main.c` small: wiring and lifecycle only; chip work belongs in its backend.
- If a task changes the build, security-sensitive parsing, or a public interface, run the checks above and report the commands run.
- Ask before adding dependencies, network access, or destructive hardware behavior.
