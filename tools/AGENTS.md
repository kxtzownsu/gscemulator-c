# AGENTS.md

`tools/` holds host-side utilities that drive a GSC over a TTY. They are not
part of the emulator binary and do not use unicorn.

## Layout

- `common/shaft.c`, `common/shaft.h` - packet framing, SHA-256, bounded TTY I/O, image lookup, and the rescue/spiflash flows.
- `rescue/rescue.c` - rescue CLI.
- `spiflash/spiflash.c` - bootstrap packet-sender CLI.
- `Makefile` - builds both programs with `-Wall -Wextra -Werror`.

## Contract

- Packets are 1024 bytes: 32-byte hash, 32-bit frame, 32-bit flash offset, 984 bytes of data.
- The packet hash covers bytes 32..1023. The expected response is the SHA-256 of the whole 1024-byte packet with zero bytes replaced by one.
- `rescue` waits for `oops?|`, sends exactly one `r`, then sends two 1024-byte zero sync packets. Do not add extra `r` or sync bytes; that misaligns the guest receive buffer.
- The prompt is only valid if the tool is attached while the BootROM is waiting. A `oops?|no` completion means rescue was declined, so fail instead of injecting a key.
- Retries are bounded by `--retries`; never retry forever or dump per-packet data by default.
- `spiflash` sends raw file bytes. It does not parse a signed header.
- TTY I/O loops on partial reads and writes and treats `EAGAIN`/`EWOULDBLOCK` as retryable.
- Keep stdout for progress and stderr for failures. Prefix messages with the active tool name.

## Verification

```bash
make tools
tools/rescue/rescue --help
tools/spiflash/spiflash --help
git diff --check
```

An end-to-end rescue run needs a booted emulator with `--console pty` and a
fresh `--flash_dir`. The tool must reach `done` and the resulting `prog.bin`
must match the RW region of the input image.
