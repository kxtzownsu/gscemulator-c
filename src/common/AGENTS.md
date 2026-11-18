# src/common

Chip-agnostic runtime code shared by every backend.

- `emulator.h` defines the interface every chip implements. Keep it generic; do not add Haven-specific state or fields here.
- `ctx.c` owns the global `Ctx`. Keep it a plain data container: `main` fills `ac`, `chip`, `firmware` (including the bundled-mode flags), and `verbose`, the backend fills `chipemu` and `uc`. Do not add behavior or chip state here.
- `exit.c` owns `emu_exit`. It takes an `Rc`, calls `end_emulation` when one is set, destroys `ctx.ac`, then exits. It clears both fields first so re-entry cannot double-free. It is `_Noreturn`; add no cleanup after it.
- `rc.h` owns `Rc`. Every program exit uses an `RC_*` value; never pass a raw number to `emu_exit` or `exit`.
- `essential-memory.c` owns `map_essential_memory`, `load_firmware`, and `load_bundled_binary`. It maps the registry, then pre-fills the ROM/firmware destinations with `0xFF` and loads them from files or bundled arrays. Keep it chip-agnostic.
- `memory-fault.c` owns `install_memory_fault_hook`. The hook logs unmapped/protected read/write/fetch details and PC; it must not call `emu_exit` or `uc_close`.
- `vtor.c` owns `set_sp_pc`. It reads SP/PC from a caller-provided vector-table base and writes the ARM registers; keep chip-specific bases in the backend.
- `file.c` owns `file_readbuf` and `file_writebuf`. `buffer_size` includes the trailing NUL: reads use at most `buffer_size - 1` bytes and terminate, writes emit `buffer_size - 1` bytes. Validate every path, buffer, and size before touching the file system, return `FileRC`, and never call `emu_exit` from this layer. Treat paths as untrusted; never pass one as a format string.
- `peripherals/component.c` owns the registry, `uc_mmio_map` trampolines, and `component_log_unhandled`. Keep component state in the backend; this layer only stores `name`, `state`, and the two handlers. Callbacks must not call `emu_exit` or `uc_close`.
- Use `emu_exit` only for unrecoverable setup or runtime failures, not normal control flow.
- `logger.c` owns `LOG`, `LOG_ERR`, and `LOG_VBS`. `LOG_VBS` is gated by `ctx.verbose`; do not add another verbose flag. Every message is a printf format string and must be a compile-time literal; pass data with `%s`, `%d`, and friends.
- `LOG_ERR` currently writes to stdout. Do not assume stderr unless you change the logger and all callers together.
- The log macros use the GNU `##__VA_ARGS__` extension. GCC and Clang are supported; do not assume MSVC.
- Do not include chip headers from this directory. If a common interface grows chip-specific behavior, move that behavior into the backend instead.
- Changing an `Emulator` function pointer signature is a cross-cutting change: update the header, every backend, and `get_chipemu` in `main.c` in one commit.
