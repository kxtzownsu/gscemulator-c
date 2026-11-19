# src/H1B3C

H1B3C is the Haven chip backend. Its entry points use the `Havn_` prefix.

- Public hooks are declared in `include/H1B3C/emulator.h` and wired through `get_chipemu` in `src/main.c`.
- `Havn_Initialize(void)` must set `ctx.chipemu.ready = true` only after setup succeeds. `main` exits through `emu_exit` when `ready` is false.
- `Havn_Initialize` stores the engine in `ctx.uc`; `Havn_EndEmulation` closes it and clears the pointer so repeated calls are safe.
- `Havn_Initialize` uses `ctx.firmware.bootrom` when `ctx.firmware.useBundledROM` is false. With `BUNDLED_H1B3C` defined, it sets `useBundledROM`/`useBundledFW` when the matching path is empty, and `load_firmware` copies `BUNDLED_H1B3C_BOOTROM_BIN`/`BUNDLED_H1B3C_FIRMWARE_BIN` into the ROM/firmware regions. A failed read or oversized bundle exits with `RC_CHIPEMU_FAILED_INIT`.
- `H1B3C_EssentialMemoryArray` in `src/H1B3C/memory.c` lists ROM (RX), SRAM (RW), INFO0/INFO1 (RW), and PROG (RWX). ROM and PROG are marked `ESSENTIAL_MEMORY_ROM` and `ESSENTIAL_MEMORY_FIRMWARE`.
- `Havn_Initialize` assigns the table to `ctx.essential_memory`, calls `map_essential_memory()`, then calls `load_firmware()` with the `BUNDLED_H1B3C_*` arrays under `BUNDLED_H1B3C` or NULLs when H1B3C is not bundled. INFO1 remains a chip-local write.
- `Havn_Initialize` calls `set_sp_pc(ctx.uc, FLASH_ROM_BASE)` after the flash writes. `set_sp_pc` sets SP/PC; `Havn_StartEmulation` sets the Thumb bit.
- `init_unicorn` installs the memory fault hook after `uc_open` and the Cortex-M3 model setup, so unmapped/protected accesses log the address, access type, and PC.
- `Havn_StartEmulation` runs on T1, checks `memory_fault_occurred()` after `uc_emu_start`, and reports failures with `runtime_fault` before inspecting the engine error. It returns instead of exiting.
- `ctx.firmware.progfw` / `--firmware` is loaded into the firmware region by `load_firmware`.
- Peripherals live in `src/H1B3C/peripherals/`. Each `X_Initialize()` registers a `Component` and `initialize_peripherals()` maps the `MmioRegion` table before `ready` is set. `component_register_threaded` starts an opt-in worker; `component_register` keeps the handler inline on T1.
- Register bases and offsets belong in the matching `include/H1B3C/peripherals/<name>.h`; `DEFAULT_REG_LENGTH` in `H1B3C/peripherals.h` is the MMIO region size for every peripheral. Keep offsets 4-byte aligned.
- `M3` is mapped at `REG_M3_BASE_ADDR` (`0xE0000000`, size `DEFAULT_REG_LENGTH`) and handles `DWT_CTRL`, `CPUID`, `ITCMCR`, and `DEMCR`. `CPUID` reads `0x410FC331` and is read-only; `ITCMCR` reads back `7` after any write, matching the Cr50 ROM.
- `FUSE` is mapped at `REG_FUSE_BASE_ADDR` (`0x40450000`, size `DEFAULT_REG_LENGTH`). Reads return the matching `fuse_defaults` value or `REG_FUSE_BLANK_VALUE` (`0x55555555`); writes are ignored and only logged at verbose level, matching the read-only eFuse.
- `GLOBALSEC` is mapped at `REG_GLOBALSEC_BASE_ADDR` (`0x40090000`, size `DEFAULT_REG_LENGTH`). `ALERT_CONTROL` and `DBG_CONTROL` (`0x405C`, `0xA6D0`) are plain registers; `DUMMYKEY0..2` (`0x9598`, `0xAF04`, `0xB2AC`) back a three-entry array indexed by offset.
- `PMU` is mapped at `REG_PMU_BASE_ADDR` (`0x40000000`, size `DEFAULT_REG_LENGTH`). `PERICLKSET0/1` set bits and `PERICLKCLR0/1` clear bits in a 48-entry clock-enable array; reads on any of the four return the current enabled mask. It registers with `component_register` and runs inline on T1; the handlers are pure state and never block.
- MMIO handlers must not call `emu_exit` or `uc_close`; use `runtime_fault` from any thread and let the control thread clean up. `Havn_StartEmulation` must not exit on runtime errors.
- `DEFINE_BASIC_REGISTER` is for file-scope singletons only. Use `Component.state` for components that need instance state.
- `Havn_StartEmulation` and `Havn_EndEmulation` must not call `exit`. Return normally, or use `runtime_fault` for runtime errors.
- `Havn_EndEmulation` runs on T0 after the emulation thread and workers have stopped, and must tolerate a partially initialized backend because failure paths call it through `emu_exit`.
- Keep Haven state, register definitions, and chip-specific parsing in this module. Do not add them to `common/Emulator`.
- Use `LOG`, `LOG_VBS`, and `LOG_ERR`; do not call `printf` directly.
- New chips follow the same shape:
  1. add an `EmulatorChips` value in `include/common/emulator.h`
  2. add `include/<CHIP>/emulator.h` and `src/<CHIP>/emulator.c`
  3. add a `case` in `get_chipemu`
  4. add `include/<CHIP>/memory.h` and `src/<CHIP>/memory.c` with the essential memory table; assign it to `ctx.essential_memory`, call `map_essential_memory()`, then `load_firmware()`
  5. add `include/<CHIP>/peripherals.h` with `DEFAULT_REG_LENGTH`, one `include/<CHIP>/peripherals/<name>.h` per peripheral, and `src/<CHIP>/peripherals/`
  6. add `src/<CHIP>/firmware/{bootrom,firmware}.bin` and the matching `BUNDLED_<CHIP>_*` arrays under `#ifdef BUNDLED_<CHIP>`
  7. add the chip to `BUNDLED_CHIPS` in the Makefile
  8. update the `--chip` help text, default selection if needed, and `tests/fuzz.dict`
- Do not advertise `h1c3m`, `h1d3c`, `h1d3m`, or `npcx` as working until their backends exist and have been smoke-tested.
