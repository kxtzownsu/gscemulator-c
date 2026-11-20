# gscemulator-c

[GSCEmulator](https://github.com/HavenOverflow/gscemu), rewritten in C.

A standalone C11 emulator for the Google Security Chip. It is not a library and does not need Python. The H1B3C (Haven) backend boots production and old Cr50 images, covers the full H1 register map, persists flash, and exposes the vTPM and CCD USB proxies natively.

## support

| chip | status |
| --- | --- |
| H1B3C (Haven) | working |

H1B3C has full MMIO coverage for the peripherals in `hw_b1_regdefs.h`, including the M3 core, timers, GPIO, I2C, SPI, SPS/TPM, USB, KEYMGR SHA/AES/GCM, flash, PMU, and GLOBALSEC. Current and `r0.0.8` through `r0.0.14` images boot to the console and complete endorsement. (Including all RW images bundled with those RO images.)

## build

Requirements:

- a C11 compiler and `make`
- `pthread` and `libutil`
- `cmake` and `git` for the default static build
- Unicorn Engine and `pkg-config` when building with `STATIC=0`
- `clang` for `make fuzz`

Plain `make` builds the [better-unicorn](https://github.com/iuqw/better-unicorn) fork and links it statically. That fork provides the safe-hook API used for fast interrupt delivery. The first build clones it into `third_party/unicorn` and builds it under `build/<arch>/unicorn`, so it takes a while. Later builds reuse it.

To build against the system Unicorn instead:

```bash
make STATIC=0
```

Common build commands:

```bash
make                         # build build/$(uname -m)/gscemu
make BUNDLED_FIRMWARE=0      # source-only build, pass -r and -f at runtime
make STATIC=0                # link against the system Unicorn
make SINGLE_THREAD=1         # run emulation and handlers inline on the control thread
make tsan                    # rebuild with ThreadSanitizer
make fuzz FUZZ_TIME=60       # clang/libFuzzer + ASan/UBSan
make tools                   # build tools/rescue and tools/spiflash
make clean                   # remove gscemu and its objects
make fullclean               # remove build/<ARCH>, including unicorn and fuzz
```

### bundled firmware

`BUNDLED_FIRMWARE` defaults to `1`. The H1B3C BootROM and firmware are included under `src/H1B3C/firmware/`, so the default build embeds them and can start without `-r` or `-f`.

Each chip reads `src/<CHIP>/firmware/bootrom.bin` and `src/<CHIP>/firmware/firmware.bin` by default. Override a pair with:

```bash
BUNDLED_H1B3C_BOOTROM_BIN=/path/to/rom.bin \
BUNDLED_H1B3C_FIRMWARE_BIN=/path/to/fw.bin \
make
```

## running

```bash
./build/$(uname -m)/gscemu
```

The default build embeds the H1B3C BootROM and firmware, so no paths are needed. To boot a different pair:

```bash
./build/$(uname -m)/gscemu --chip h1b3c \
  -r path/to/bootrom.bin \
  -f path/to/firmware.bin
```

### console

`--console` selects where UART0 goes:

- `none` writes UART output to stdout and leaves stdin alone. This is the default when stdin is not a TTY.
- `stdio` creates a pty, prints its slave path, and pumps stdin/stdout through it. This is the default when stdin is a TTY.
- `pty` creates the pty and prints its slave path but does not attach stdio. Use this with `tools/` or another pty client.

Wait for `Console is enabled; type HELP for help.` before typing. `SINGLE_THREAD=1` builds always use `none`. Press `Ctrl-T` to pulse the emulated power button; CCD physical presence checks want five short presses.

```bash
./build/$(uname -m)/gscemu --console stdio
./build/$(uname -m)/gscemu --console pty
```

### flash

By default, flash is persisted under `$HOME/.local/share/gscemu-c/flash/` as `info0.bin`, `info1.bin`, and `prog.bin`. Use `--flash_dir DIR` to choose another directory, or `--no-persist` to keep everything in memory and never touch the host filesystem.

### proxies

When running as root, the emulator starts the vTPM and CCD USB proxies unless they are disabled:

```bash
sudo ./build/$(uname -m)/gscemu \
  -r src/H1B3C/firmware/bootrom.bin \
  -f src/H1B3C/firmware/firmware.bin
```

- `--disable-tpm-chardev` skips `vtpm-proxy` and the `/dev/tpmN` node.
- `--disable-usb-proxy` skips the raw-gadget CCD device.

The TPM proxy needs `tpm_vtpm_proxy` and `/dev/vtpmx`. The USB proxy needs `dummy_hcd`, `raw_gadget`, `/dev/raw-gadget`, and `dummy_udc.0`. Non-root runs skip both.

The CCD device enumerates as `18d1:5014`. The vTPM answers standard TPM2 commands through `/dev/tpmN`.

### other options

```text
-r, --bootrom <FILE>       BootROM binary (overrides the bundled one)
-c, --chip <VALUE>         Chip to emulate [ h1b3c | h1c3m | h1d3c | h1d3m | npcx ]
    --console <VALUE>      Console output [ none | stdio | pty ]
    --disable-tpm-chardev  Do not start the vTPM chardev proxy
    --disable-usb-proxy    Do not start the CCD USB proxy
-f, --firmware <FILE>      Firmware binary (overrides the bundled one)
    --flash_dir <DIR>      Directory for persisted flash images
    --no-persist           Do not read or write persisted flash
-v, --verbose              Verbose output
    --version              Show version information
    --wait-for-enter       Wait for ENTER before starting emulation
-h, --help                 Show help
```

## tools

`tools/rescue` flashes a signed RW image over the BootROM rescue protocol. `tools/spiflash` sends raw bootstrap packets. Both are standalone C programs and share the packet framing in `tools/common/shaft.c`. The BootROM bootstrap path is not modeled, so `spiflash` is a packet sender only.

```bash
make tools

./build/$(uname -m)/gscemu --console pty \
  -r src/H1B3C/firmware/bootrom.bin \
  -f src/H1B3C/firmware/firmware.bin
# Console PTS (pty-only): /dev/pts/7

tools/rescue/rescue --tty /dev/pts/7 \
  --firmware src/H1B3C/firmware/firmware.bin
```

See [tools/README.md](tools/README.md) for protocol details and options.

## notes

- Old images (`r0.0.8` and `r0.0.10`) only trust the test endorsement CA and use an older crypto mask load. The emulator patches the CA modulus and the legacy microcode entry before running them, so production certs install and the endorsement completes.
- `reboot` from the firmware console requires the CCD console to be open. Run `ccd open` first, otherwise the command prints `Access Denied`.
- `ccd testlab enable` and `ccd unlock` use the firmware's short physical-presence check. Press `Ctrl-T` five times, about a second apart.
- The flash image is writable by the guest. Use `--no-persist` if you do not want a boot to change the host flash files.
- The benchmark harness lives in `reference/benchmark/` and is local-only. It builds a tiny ROM and a benchmark firmware, then compares this emulator against upstream.

## credits

- [kxtzownsu](https://github.com/kxtzownsu) for gscemulator-c
- [undulary](https://github.com/iuqw) for deepseek
- [HavenOverflow](https://github.com/HavenOverflow) - original GSCEmulator
- the Unicorn Engine team and contributors
- Google for the Google Security Chip

## license

GNU General Public License v2. See [LICENSE](LICENSE).
