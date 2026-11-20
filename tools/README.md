# tools

Host-side helpers for driving a GSC in rescue or bootstrap mode over a TTY.
They share the packet framing in `common/shaft.c`.

Both tools talk to a serial device, so the emulator must expose one. Use
`--console pty` and pass the printed `/dev/pts/N` path.

## rescue

Flashes a signed RW image through the BootROM rescue protocol.

```bash
./build/$(uname -m)/gscemu --console pty -r src/H1B3C/firmware/bootrom.bin \
  -f src/H1B3C/firmware/firmware.bin
# Console PTS (pty-only): /dev/pts/7

tools/rescue/rescue --tty /dev/pts/7 --firmware src/H1B3C/firmware/firmware.bin
```

The tool waits for the `oops?|` marker, sends one `r`, reads the `escue`
banner, syncs, and streams the packet stream. Every request is acknowledged
with a SHA-256 digest; a packet is retried a bounded number of times before
the transfer fails.

The prompt only exists for a short window during boot, so start the tool as
soon as the pty path is printed. If the guest finished the prompt with `no`,
the tool exits without sending anything rather than injecting bytes into the
firmware console.

Options:

- `--timeout SEC` - per-request timeout, default `10`.
- `--retries N` - retries per packet, default `5`.
- `--verbose` - print acknowledgements and the first digest mismatch.

The image is located in the input file by scanning for a valid signed header,
starting at the RO size and stepping by `0x800`. The RW offset and base come
from that header.

## spiflash

Sends a raw file to the BootROM bootstrap packet receiver.

```bash
tools/spiflash/spiflash --tty /dev/pts/7 --firmware image.bin --offset 0x4000
```

The BootROM only accepts bootstrap packets when `dev_mode` is asserted at
reset, and the current emulator does not model that path, so this is a packet
sender only.

Options are the same as `rescue`, plus `--offset ADDR` (default `0`).

## build

```bash
make tools
make tools-clean
```

The tools link `src/common/sha256.c` and build against `include/`, so run them
from the repository root.
