---
title: USB Tool CLI (tool-usb.py)
category: interface
order: 11
purpose: The command surface of the host-side USB tool and what each subcommand puts on the wire.
status: active
updated: 2026-09-08
source: docs/scripts/tool-usb.py
confidence: confirmed
keywords: tool-usb.py, selftest, ping, version, boot-slot, restart, upgrade, dump, --arm, --chunk, --target, --bytes, --keep, --timeout, pyserial, VID PID detection, esp-coredump
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
| `dump FILE` | `0x0701` → N × `0x0702` → `0x0703` | Pulls the stored core dump to `FILE`, then erases it on the device. See below — the exit code carries information |

| Flag | Applies to | Meaning |
|------|-----------|---------|
| `-p` / `--port` | every command but `selftest` | e.g. `COM7`. Omit to detect |
| `--bytes` | `ping` | payload size, default 64 |
| `--target` | `upgrade` | `1` `app_firmware` (default), `0` `app_updater` |
| `--chunk` | `upgrade` | 4096..32768, multiple of 1024. Default 32768 |
| `--arm` | `upgrade` | after a successful `UPG_END`, also send `Set BOOT_SLOT` and `RESTART_APP` |
| `--keep` | `dump` | skip the `DUMP_ERASE`. **Not harmless** — see below |
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

`dump`'s output path gets the same treatment, but only the **directory** is
checked and nothing is created. The file is written once, at the end, from a
complete transfer: a half-written `crash.bin` that `esp-coredump` then refuses
is worse than no file, and creating it up front would also destroy a previous
dump before knowing there is a new one to replace it with.

## `dump`, and why its exit code matters

`DUMP_INFO` first, then the read loop, then the erase:

| `state` from `DUMP_INFO` | What the tool does | Exit |
|--------------------------|--------------------|------|
| `0` absent | Prints `no core dump stored - nothing to fetch`. Writes no file | **1** |
| `1` valid | Reads it in `DUMP_CHUNK_MAX` (4096) chunks, writes the file, erases | 0 |
| `2` corrupt | Warns that the checksum does not match, then **fetches it anyway** | 0 |

**Absent is not an error on the device's part** — nothing has panicked — but the
exit is still non-zero, because `dump crash.bin && esp-coredump ... crash.bin`
must not run the second half against a file that was never written.

**A corrupt dump is fetched deliberately.** It is exactly the one worth looking
at; `esp-coredump` may still decode part of it, and a partial backtrace beats
none. Only an *absent* dump stops the command.

Order is load-bearing: the file is written **before** the erase, so the device
keeps the only copy until the bytes are on disk. A failed write says so and
leaves the dump in place to be fetched again.

`--keep` skips the erase, and the tool says out loud what that costs, because
`CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` is on
([../data/flash-and-partitions.md](../data/flash-and-partitions.md)): a unit
whose dump is not erased **captures no further panic** until it is. Recovery
from an interrupted read-out is simply running `dump` again without `--keep`.

On success it prints the command that turns the file into a backtrace:

```
esp-coredump info_corefile --core-format raw -c crash.bin <the .elf of the build that crashed>
```

**That `.elf` must be the exact build that crashed** — not a rebuild. Addresses
shift with any recompile and a wrong `.elf` produces a plausible, wrong
backtrace with no warning. The dump carries the crashing image's SHA256 for the
tool to check against, and `release.yml` archives `app_updater.elf` for exactly
this.

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
