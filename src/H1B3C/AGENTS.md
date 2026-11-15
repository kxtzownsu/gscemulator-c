# src/H1B3C

H1B3C is the Haven chip backend. Its entry points use the `Havn_` prefix.

- Public hooks are declared in `include/H1B3C/emulator.h` and wired through `get_chipemu` in `src/main.c`.
- `Havn_Initialize(void)` must set `ctx.chipemu.ready = true` only after setup succeeds. `main` exits through `emu_exit` when `ready` is false.
- `Havn_Initialize` stores the engine in `ctx.uc`; `Havn_EndEmulation` closes it and clears the pointer so repeated calls are safe.
- `Havn_Initialize` reads `ctx.firmware.bootrom` into a `FLASH_ROM_SIZE` byte buffer and writes exactly that buffer to `FLASH_ROM_BASE`. The buffer size must match the mapped flash region; a failed read exits with `RC_CHIPEMU_FAILED_INIT`.
- Peripherals live in `src/H1B3C/peripherals/`. Each `X_Initialize()` registers a `Component` and `initialize_peripherals()` maps the `MmioRegion` table before `ready` is set.
- Register bases, region sizes, and offsets belong in `include/H1B3C/register_map.h`. Keep offsets 4-byte aligned.
- MMIO handlers must not call `emu_exit` or `uc_close`; log and return 0, or stop emulation and let `Havn_StartEmulation` clean up.
- `DEFINE_BASIC_REGISTER` is for file-scope singletons only. Use `Component.state` for components that need instance state.
- `Havn_StartEmulation` and `Havn_EndEmulation` must not call `exit`. Return normally, or use `emu_exit` for fatal errors.
- `Havn_EndEmulation` must tolerate a partially initialized backend because failure paths call it through `emu_exit`.
- Keep Haven state, register definitions, and chip-specific parsing in this module. Do not add them to `common/Emulator`.
- Use `LOG`, `LOG_VBS`, and `LOG_ERR`; do not call `printf` directly.
- New chips follow the same shape:
  1. add an `EmulatorChips` value in `include/common/emulator.h`
  2. add `include/<CHIP>/emulator.h` and `src/<CHIP>/emulator.c`
  3. add a `case` in `get_chipemu`
  4. update the `--chip` help text, default selection if needed, and `tests/fuzz.dict`
- Do not advertise `h1c3m`, `h1d3c`, `h1d3m`, or `npcx` as working until their backends exist and have been smoke-tested.
