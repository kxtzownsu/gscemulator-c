---
name: Add chip backend
description: Scaffold and wire a new emulator chip backend through EmulatorChips, get_chipemu, CLI help text, and the fuzz dictionary; use for H1C3M, H1D3C, H1D3M, NPCX, or other chip support in better-gscemu.
---

# Add chip backend

Only H1B3C is implemented. The help text lists future chips, but they have no backend until you add one.

## Read first

- `AGENTS.md`
- `src/H1B3C/AGENTS.md`
- `include/common/emulator.h`
- `include/H1B3C/emulator.h`
- `src/H1B3C/emulator.c`
- `src/main.c`

## Lifecycle contract

- `initialize(void)` must set `ctx.chipemu.ready = true` only after successful setup. Leave it false on failure.
- `start_emulation(void)` runs after a successful initialize.
- `end_emulation(void)` takes no arguments, uses `ctx` for state, and must tolerate a partially initialized backend.
- `emu_exit` is `_Noreturn`, takes an `Rc`, may call `end_emulation` before destroying `ctx.ac`, and is control-thread-only. Do not call `exit` from a backend; use `runtime_fault` for runtime errors and an `RC_*` value for fatal setup errors.
- Peripherals register with `component_register` (inline) or `component_register_threaded` (worker) and are mapped in `initialize_peripherals()` through `component_map_mmio`. MMIO callbacks must not call `emu_exit` or `uc_close`; use `runtime_fault`.
- Use `DEFINE_BASIC_REGISTER(name)` for any plain 32-bit read/write register with no side effects; add the offset switch cases yourself. Hand-roll only when the register has side effects, is read-only/constant, needs value validation, or keeps per-instance/array state.
- Bundled firmware: `--bootrom`/`--firmware` override the `BUNDLED_<CHIP>_*` arrays. The backend sets `ctx.firmware.useBundledROM`/`useBundledFW` when its own `BUNDLED_<CHIP>` macro is defined and the matching path is empty.

## Steps

1. Add the enum value in `include/common/emulator.h` before `EMULATORCHIPS_END`.
2. Add `include/<CHIP>/emulator.h` with an include guard and the three lifecycle prototypes.
3. Add `src/<CHIP>/emulator.c`. H1B3C uses the `Havn_` prefix; pick a short, consistent prefix for the new chip.
4. Include the new header in `src/main.c`, add a `case` in `get_chipemu`, and fill `ctx.chipemu` with the three function pointers.
5. Add `include/<CHIP>/memory.h` and `src/<CHIP>/memory.c` with the essential memory table; mark one ROM and one firmware role, assign it to `ctx.essential_memory`, and call `map_essential_memory()` before loading firmware.
6. Call `load_firmware()` with the chip's bundled arrays under `#ifdef BUNDLED_<CHIP>` (or NULLs when the chip is not bundled), and set `useBundledROM`/`useBundledFW` when the path is empty.
7. Call `set_sp_pc(ctx.uc, <VTOR base>)` during initialize when the chip boots from a fixed vector table.
8. Call `install_memory_fault_hook()` after `uc_open` so unmapped/protected accesses identify the address, access type, and PC; check `memory_fault_occurred()` after `uc_emu_start` and call `runtime_fault(RC_CHIPEMU_MEMORY_FAULT, ...)`.
9. Add `include/<CHIP>/peripherals.h` with `DEFAULT_REG_LENGTH`, then one `include/<CHIP>/peripherals/<name>.h` per peripheral with its base and 4-byte-aligned register offsets.
10. Add `src/<CHIP>/peripherals/` components and wire `initialize_peripherals()` with `component_register` or `component_register_threaded`, then `component_map_mmio`; use `component_log_unhandled()` in default register cases. Use `DEFINE_BASIC_REGISTER` for plain registers; hand-roll only for side effects, read-only values, validation, or per-instance/array state.
11. Add `src/<CHIP>/firmware/{bootrom,firmware}.bin`, add the chip to `BUNDLED_CHIPS` and `CHIPS` (`scripts/make/variables.mk`), and include the `BUNDLED_<CHIP>_*` arrays under `#ifdef BUNDLED_<CHIP>` when bundled boot is supported. The generated header emits `BUNDLED_<CHIP>`.
12. Extend chip string parsing in `main.c`, update the `--chip` help text, and keep the default at `h1b3c`.
13. Add the lowercase chip name to `tests/fuzz.dict`.
14. Verify with `make clean && make BUNDLED_FIRMWARE=0` (or plain `make` with firmware inputs), `--chip <name> -r <bootrom> -v`, an invalid chip, and `make fuzz FUZZ_TIME=1`.

See [checklist](references/checklist.md) for the exact file list.

## Boundaries

- Do not add chip-specific fields to `common/Emulator`. Keep backend state private to `src/<CHIP>/`.
- Do not put chip logic in `src/common/`.
- Do not advertise a chip as working until its backend exists and has been smoke-tested.
- Do not hand-edit `include/common/bundled-firmware.h`; regenerate it with `scripts/generate_firmware_header.sh`.
- If the backend needs per-instance state, extend the interface deliberately instead of hiding state in globals.
