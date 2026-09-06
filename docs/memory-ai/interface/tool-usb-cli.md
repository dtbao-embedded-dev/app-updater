---
title: USB Tool CLI (tool-usb.py)
category: interface
order: 11
purpose: The command surface of the host-side USB tool and what each subcommand puts on the wire.
status: active
updated: 2026-09-07
source: docs/scripts/tool-usb.py
confidence: confirmed
keywords: tool-usb.py, selftest, ping, version, boot-slot, restart, upgrade, --arm, --chunk, --target, --bytes, --timeout, pyserial, VID PID detection
---

# USB Tool CLI (tool-usb.py)

> The host half of the command channel, so the wire format can be exercised
> against a real board and not only against the host suite.

## Contract

Invoked as `python docs/scripts/tool-usb.py <command> [argument] [flags]`.

| Command | Puts on the wire | Notes |
|---------|------------------|-------|
| `selftest` | nothing | **No board and no pyserial needed.** Checks `crc32("123456789") == 0xCBF43926` and five frame round trips |
| `ping` | `0x0006` | `--bytes N` sets the payload size, default 64. Fails if the echo differs by a byte |
| `version` | `0x0201` then `0x0202` | Prints both version fields and the running slot. A blank slot prints `(unset)` rather than an empty line |
| `boot-slot SLOT` | `0x0101` | `SLOT` is 0 or 1. Says "armed for the next boot" and does **not** read it back, because a Get would report the old value |
| `restart` | `0x0001` | The port re-enumerates afterwards |
| `upgrade FILE` | `0x0601` → N × `0x0602` → `0x0603` | Prints per-chunk progress. Arms nothing unless `--arm` |

| Flag | Applies to | Meaning |
|------|-----------|---------|
| `-p` / `--port` | every command but `selftest` | e.g. `COM7`. Omit to detect |
| `--bytes` | `ping` | payload size, default 64 |
| `--target` | `upgrade` | `1` `app_firmware` (default), `0` `app_updater` |
| `--chunk` | `upgrade` | 4096..32768, multiple of 1024. Default 32768 |
| `--arm` | `upgrade` | after a successful `UPG_END`, also send `Set BOOT_SLOT` and `RESTART_APP` |
| `--timeout` | every command but `selftest` | seconds to wait for one response, default 5 |

**A flag that does not apply is rejected, not ignored** — `selftest -p COM7`,
`ping --arm` and `version --bytes 8` each exit non-zero saying what the flag is
for. Same rule as `tool-esp.py`: a flag that silently does nothing is one
somebody will believe in.

## Finding the port

Detection matches on the device's own **VID:PID `A331:F001`**, not on "the one
serial port", so a bench with other adapters attached still works. No match
lists every port on the machine and says to plug the cable into the OTG port;
two matches is an error rather than a coin flip, because guessing between two
boards is how an image lands on the wrong one.

## Checked before the port opens

`--chunk` and the image file are validated **before** the serial port is
touched, for the same reason the firmware checks every `UPG_BEGIN` argument
before it erases anything: a request that was never going to work should cost
nothing, and a bad `--chunk` reported as a port error sends the reader looking
in the wrong place.

## Dependencies

`pyserial`, and only for the commands that talk to a board — the import is
deferred so `selftest` and `--help` work without it. Frames are built and
checked with Python's own `zlib.crc32`, so no CRC implementation is written
here either.

## The constants are copies

`middleware/protocol/include/protocol.h` is the authority on every opcode and
constant; the copies in this script exist because it has to run without the
firmware's build. **When the two disagree, the header is right.** `selftest`
pins the CRC against the same fixed vector the firmware's own tests use, which
holds both implementations to one value rather than to each other.

## See also

- [../behavior/usb-host-flow.md](../behavior/usb-host-flow.md) — the sequence this automates
- [command-map.md](command-map.md) — every opcode
- [tool-esp-cli.md](tool-esp-cli.md) — the build and flash entry point
