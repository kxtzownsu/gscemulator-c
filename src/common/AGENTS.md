# src/common

Chip-agnostic runtime code shared by every backend.

- `emulator.h` defines the interface every chip implements. Keep it generic; do not add Haven-specific state or fields here.
- `ctx.c` owns the global `Ctx`. Keep it a plain data container: `main` fills `ac`, `chip`, `firmware` (including the bundled-mode flags), and `verbose`, the backend fills `chipemu` and `uc`. Do not add behavior or chip state here.
- `exit.c` owns `emu_exit`. It is control-thread-only (debug assert), stops component workers and the console, then calls `end_emulation` when one is set, destroys `ctx.ac`, and exits. It clears both fields first so re-entry cannot double-free. It is `_Noreturn`; add no cleanup after it.
- `runtime/control.c` owns the control loop, the fault mailbox, signal handling, the emulation thread, and teardown. Only this code calls `emu_exit` on the control thread. `runtime_fault` is callable from any thread: first error wins, it sets `shutting_down`, calls `uc_emu_stop`, and wakes the control poll.
- `runtime/thread.c` owns `tctx`, `thread_ctx_init`, and `thread_is_control`. Keep it a small role/label record; no machine state.
- `runtime/worker.c` owns the per-component FIFO workers. A worker never calls unicorn; ops carry the PC snapshot. Default writes are synchronous so guest MMIO order is preserved.
- `runtime/console.c` owns the pty and the stdio relay. T2 owns the master fd; the control thread owns stdin/stdout and polls the slave. Console is `none` by default and opt-in with `--console pts`.
- `rc.h` owns `Rc`. Every program exit uses an `RC_*` value; never pass a raw number to `emu_exit` or `exit`.
- `essential-memory.c` owns `map_essential_memory`, `load_firmware`, and `load_bundled_binary`. It maps the registry, then pre-fills the ROM/firmware destinations with `0xFF` and loads them from files or bundled arrays according to `ctx.firmware.useBundled*`, which the backend sets from its per-chip `BUNDLED_<CHIP>` macro and empty paths. Keep it chip-agnostic.
- `memory-fault.c` owns `install_memory_fault_hook` and `memory_fault_occurred`. The hook logs unmapped/protected read/write/fetch details and PC, sets an atomic fault flag, and calls `uc_emu_stop`; it must not call `emu_exit` or `uc_close`.
- `version.c` owns `print_version`, which prints the generated `common/version.h` fields for `--version`.
- `vtor.c` owns `set_sp_pc`. It reads SP/PC from a caller-provided vector-table base and writes the ARM registers; keep chip-specific bases in the backend.
- `file.c` owns `file_readbuf` and `file_writebuf`. `buffer_size` includes the trailing NUL: reads use at most `buffer_size - 1` bytes and terminate, writes emit `buffer_size - 1` bytes. Validate every path, buffer, and size before touching the file system, return `FileRC`, and never call `emu_exit` from this layer. Treat paths as untrusted; never pass one as a format string.
- `peripherals/component.c` owns the registry, `uc_mmio_map` trampolines, threaded dispatch, `component_stop_all`, and `component_log_unhandled`. Keep component state in the backend; this layer stores `name`, `state`, the two handlers, and an optional `worker`. Inline handlers run on T1; threaded handlers run on their worker. Callbacks must not call `emu_exit` or `uc_close`; use `runtime_fault`.
- `peripherals/define.h` owns `DEFINE_BASIC_REGISTER` (storage only), `BasicRegister`, `BASIC_REGISTER_ENTRY`, `basic_register_find`, and `ARRAY_LEN`. Add one table entry per plain register; keep switch cases for side effects, read-only/constant values, validation, or per-instance/array state.
- Use `emu_exit` only for unrecoverable setup or runtime failures, not normal control flow.
- `logger.c` owns `LOG`, `LOG_ERR`, and `LOG_VBS`. It serializes one line at a time with a mutex and prefixes the current `tctx.name` (`[control]`, `[emu]`, `[PMU]`, ...). `LOG_VBS` is gated by `ctx.verbose`; do not add another verbose flag. Every message is a printf format string and must be a compile-time literal; pass data with `%s`, `%d`, and friends.
- `LOG_ERR` currently writes to stdout. Do not assume stderr unless you change the logger and all callers together.
- The log macros use the GNU `##__VA_ARGS__` extension. GCC and Clang are supported; do not assume MSVC.
- Do not include chip headers from this directory. If a common interface grows chip-specific behavior, move that behavior into the backend instead.
- Changing an `Emulator` function pointer signature is a cross-cutting change: update the header, every backend, and `get_chipemu` in `main.c` in one commit.
