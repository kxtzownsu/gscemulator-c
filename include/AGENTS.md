# include/

Public headers. They define the contract for `src/`.

- Keep implementation details in `.c` files. Do not put struct layouts or state in public headers unless the API needs them.
- Every header needs an include guard. Include the headers it uses directly instead of relying on include order.
- `libargs.h` is the public C API. Keep `ArgCheck` opaque and keep the `extern "C"` guard for C++ consumers.
- Put `ARGCHECK_NORETURN` and printf-format attributes on declarations so every caller sees them.
- `common/common.h` is the umbrella include for `src/common/`. Keep it declarations and includes only, with no state or logic.
- `common/ctx.h` declares the global `Ctx ctx` container and `ctx_init`. Keep it a plain data struct; resource ownership stays with the code that created the resource, and `ctx.verbose` is the only verbose switch. Firmware paths are fixed `char[MAX_FILE_PATH]` buffers; copy CLI strings in, never store `ArgCheck` pointers.
- `common/emulator.h` owns `Emulator` and `EmulatorChips`. Keep them chip-agnostic. Adding a field or changing a function pointer signature requires updating every backend and `get_chipemu`.
- `common/exit.h` exposes `emu_exit`; it must stay `ARGCHECK_NORETURN`.
- `common/rc.h` owns `Rc`. Every exit path uses an `RC_*` value; add a new one instead of reusing an unrelated code.
- `common/file.h` owns `FileRC` and the file helpers. Keep them free of `ctx` and `emu_exit`; they report failures with a code and treat `buffer_size` as including the trailing NUL.
- `common/peripherals/component.h` owns the generic `Component`, `MmioRegion`, registry, and mapping helpers. Keep it chip-agnostic.
- `common/peripherals/define.h` owns `DEFINE_BASIC_REGISTER`. It is a file-scope singleton helper, not a per-instance register container.
- `H1B3C/register_map.h` owns peripheral base addresses, region sizes, and register offsets. Keep every offset 4-byte aligned and add new registers there.
- `H1B3C/peripherals.h` and `H1B3C/peripherals/*.h` expose component init entry points and `initialize_peripherals`.
- Chip headers expose only lifecycle entry points. Chip-specific state and helpers stay in `src/<chip>/`.
- Keep prototypes in sync with definitions. A header change is not complete until all implementations, `main.c`, and the fuzz harness compile.
