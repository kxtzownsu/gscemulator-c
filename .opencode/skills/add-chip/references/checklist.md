# New chip checklist

Copy this list into the task and tick each item.

- [ ] `include/common/emulator.h`: enum value added before `EMULATORCHIPS_END`
- [ ] `include/<CHIP>/emulator.h`: include guard, three prototypes
- [ ] `src/<CHIP>/emulator.c`: all three hooks implemented
- [ ] `initialize` sets `ctx.chipemu.ready = true` only after success
- [ ] `end_emulation` is safe after a failed or partial initialize
- [ ] `include/<CHIP>/memory.h` and `src/<CHIP>/memory.c`: essential memory table with region permissions and one ROM/firmware role each
- [ ] `initialize` assigns `ctx.essential_memory`, calls `map_essential_memory()`, then calls `load_firmware()` before firmware writes
- [ ] `initialize` calls `set_sp_pc()` with the chip VTOR base
- [ ] `initialize` installs `install_memory_fault_hook()` after `uc_open`, and `start_emulation` calls `runtime_fault(RC_CHIPEMU_MEMORY_FAULT, ...)` when `memory_fault_occurred()` is set
- [ ] `include/<CHIP>/peripherals.h` defines `DEFAULT_REG_LENGTH`, and each `include/<CHIP>/peripherals/<name>.h` has its base and 4-byte-aligned offsets
- [ ] `src/<CHIP>/peripherals/`: components registered with `component_register` / `component_register_threaded`, mapped in `initialize_peripherals()`, unknown offsets use `component_log_unhandled()`
- [ ] register ranges audited with the `audit-mmio` skill: counts end at the next register, special registers are dispatched first, and headers carry `_Static_assert` guards
- [ ] plain read/write registers use a `BasicRegister` table (`DEFINE_BASIC_REGISTER` + `BASIC_REGISTER_ENTRY` + `basic_register_find`); switch cases only for side effects, read-only/constant values, validation, or per-instance/array state
- [ ] `src/<CHIP>/firmware/{bootrom,firmware}.bin`: inputs present and the chip added to `BUNDLED_CHIPS` and `CHIPS`
- [ ] backend uses `BUNDLED_<CHIP>_*` under `#ifdef BUNDLED_<CHIP>` and sets `useBundledROM`/`useBundledFW` for empty paths
- [ ] `src/main.c`: header included
- [ ] `src/main.c`: `get_chipemu` case fills `ctx.chipemu` with all three pointers
- [ ] `src/main.c`: lowercase chip string maps to the enum
- [ ] `src/main.c`: `--chip` help text updated
- [ ] `tests/fuzz.dict`: lowercase chip string added
- [ ] `make clean && make BUNDLED_FIRMWARE=0` (or plain `make` with firmware inputs)
- [ ] `./build/$(uname -m)/gscemu --chip <name> -r <bootrom> -v`
- [ ] invalid chip still exits non-zero
- [ ] `make fuzz FUZZ_TIME=1`

If the chip is not ready, do not leave a half-wired `case` that advertises support. Keep the help text honest.
