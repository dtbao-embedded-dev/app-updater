---
title: Core Dump Capture and Read-Out
category: behavior
order: 8
purpose: When a panic actually produces a core dump and when it does not, what the stored bytes are, how they leave the unit over USB, and why the .elf that decodes them must be the exact build that crashed.
status: active
updated: 2026-09-08
source: driver/coredump/src/coredump.c, middleware/command/src/command_dump.c, application/app/src/app.c:373-395, docs/scripts/tool-usb.py, workspace/0xF001/sdkconfig.defaults
confidence: confirmed
keywords: core dump, panic, esp_core_dump_write, panic_restart, PANIC_EXCEPTION, DUMP_INFO, DUMP_READ, DUMP_ERASE, report_last_panic, tool-usb.py dump, esp-coredump, info_corefile, core-format raw, app_elf_sha256, FLASH_NO_OVERWRITE, s_dumping_core
---

# Core Dump Capture and Read-Out

> The dump is written **before** the reset, not after it, and only by a panic that reaches the panic handler. By the time USB is up again the record is already in flash, so getting it out is an ordinary read-back problem — not a crash-time one.

## The order, which is the opposite of the obvious guess

`esp_system/panic.c` runs, in this order:

1. Print the panic message and `ELF file SHA256:` to the console (UART0 here).
2. **`esp_core_dump_write(info)`** — the dump is written to flash, with the
   watchdogs fed around it.
3. Set the reset-reason hint (`ESP_RST_PANIC`, `ESP_RST_INT_WDT`,
   `ESP_RST_TASK_WDT`).
4. `panic_restart()`.

So the sequence is **write, then reset** — not "reset, then save". Everything
about the design follows from that: nothing has to survive a reboot in RAM, and
nothing has to be transmitted while the chip is dying.

## Which resets produce a dump, and which do not

Only a reset that reaches the panic handler writes anything.

| Produces a dump | Produces nothing |
|-----------------|------------------|
| CPU exceptions — `LoadProhibited`, `StoreProhibited`, `IllegalInstruction` | Brownout, or the supply simply going away |
| `abort()`, and a failed `assert` (asserts ship enabled, R-BLD-03) | The EN pin / reset button |
| Interrupt watchdog (`PANIC_EXCEPTION_IWDT`) | An RTC watchdog reset that bypasses the panic handler |
| Task watchdog (`PANIC_EXCEPTION_TWDT`) | A hang no watchdog catches |

For everything in the right-hand column the partition stays blank and
`coredump_info_get()` answers `COREDUMP_ABSENT`. **That is an answer, not a
failure** — it says the unit did not panic, which is worth knowing.

One more way to get nothing: if the dump-writing code itself faults, the
`s_dumping_core` re-entry guard prints `Re-entered core dump! Exception happened
during core dump!` and no dump is stored at all.

## What the stored bytes are

Not a hex log — a **binary ELF core file**, in three parts:

| Part | Content |
|------|---------|
| 12 B header | `core_dump_header_t { data_len, version, chip_rev }` |
| body | A subset of ELF: `PT_LOAD` segments holding each task's TCB and stack, `PT_NOTE` / `NT_PRSTATUS` holding the register sets |
| 32 B trailer | SHA256 checksum |

Nothing in it is human-readable except task names (`char exc_task[16]`), so it
has to go through a tool. There is no format or checksum knob in ESP-IDF v6.1 —
ELF and SHA256 are what gets compiled in.

## Two ways out, and what each is for

### On the device, at boot: the reason as text

`report_last_panic()` logs the panic reason on the next boot
([boot-and-bring-up.md](boot-and-bring-up.md) step 3).
`esp_core_dump_get_panic_reason()` returns a **string**, so this needs no tool,
no host and no symbolisation. It answers "why did this unit reboot" and nothing
more — no backtrace, no line numbers.

### Over USB, after the reboot: the whole thing

Three opcodes, no session
([../interface/command-map.md](../interface/command-map.md)):

1. `DUMP_INFO` `0x0701` → `state` and `size`.
2. `DUMP_READ` `0x0702` → 4096 bytes at a time, `offset` and `len` in every
   request, so any chunk may be retried in any order.
3. `DUMP_ERASE` `0x0703` → after the host has the bytes on disk.

`tool-usb.py dump crash.bin` drives all three
([../interface/tool-usb-cli.md](../interface/tool-usb-cli.md)). The file is
written from a complete transfer **before** the erase, so the device holds the
only copy until the bytes are safe.

**Why the read-out is raw and the parsing is on the host.** `esp_core_dump_get_summary()`
would give `exc_pc` plus sixteen raw PC values — which still need `addr2line`
and the matching `.elf` to become file and line, so parsing on the device costs
code and gains nothing. Worse, it loses: the raw dump already carries the
crashing image's `app_elf_sha256`, which is what tells a reader **which of the
two apps** crashed, since `app_updater` and `app_firmware` share this one
partition. Ship the bytes, keep everything.

## Why there is no crash-time USB path

`esp_system/panic.c` has exactly three character-output functions:
`panic_print_char_uart` (a polling loop into the TX FIFO),
`panic_print_char_usb_cdc` (the ROM CDC driver) and
`panic_print_char_usb_serial_jtag`. **TinyUSB is not among them and cannot be:**
a panic runs with interrupts off and no scheduler, so the TinyUSB task and its
endpoint servicing never run. All three supported paths are direct register
polling for that reason.

The one USB path that does work in a panic is the ROM CDC console
(`CONFIG_ESP_CONSOLE_USB_CDC`, component `esp_usb_cdc_rom_console`), and it
drives `USB0` directly with its own `esp_intr_alloc(ETS_USB_INTR_SOURCE)` —
the same USB-OTG controller TinyUSB holds for the command channel. Two stacks,
one controller: mutually exclusive. Enabling it would also mean
`ENABLE_TO_UART`, giving up the flash partition entirely.

None of this matters, because the dump is already in flash before the reset.
The flash partition is what turns a crash-time problem into a read-back one.

## Decoding it, and the one thing that makes a dump worthless

```
esp-coredump info_corefile --core-format raw -c crash.bin <prog>.elf
```

`--core-format` accepts `auto | b64 | elf | raw`; a raw flash image is `raw`.
On a bench with the chip in download mode, `idf.py coredump-info -p COM7` does
the read and the parse in one step and needs no firmware support at all — which
is why the USB path only earns its place for a unit that cannot be put into
download mode.

**The `.elf` must be the exact build that crashed.** Not "an `.elf` of
`app_firmware`" — the one that produced the running image. One recompile shifts
every address, and a wrong `.elf` yields a plausible, wrong backtrace **with no
error**. That is the failure mode to fear here, not a missing file.

The dump carries `app_elf_sha256` so the tool can tell you — and you can tell
without the tool, because that field **is** `sha256(app_updater.elf)`. So the
`SHA256SUMS` a release publishes doubles as the dump-to-release index: grep it
for the hash the crash reported and the matching release is the one that
answers. `release.yml` archives `app_updater.elf` with its `.map`, the
bootloader's pair, `sdkconfig` and the size report for exactly this.

🔴 **Gap: `app_firmware` has no such archive yet.** The sibling repo is empty,
and a crash there is the likely one — it is the product and it runs almost all
the time. If its release does not archive its `.elf` the same way, a dump
fetched from the field will be bytes with nothing to decode them against.

## Keeping the first dump, not the last

`CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE=y`
([../data/flash-and-partitions.md](../data/flash-and-partitions.md)). In a boot
loop the crash that explains the loop is the one that started it, so a second
panic must not overwrite it.

The consequence runs through the whole feature: `DUMP_ERASE` is load-bearing,
`tool-usb.py dump` erases by default, and `report_last_panic()` deliberately
does **not** — erasing at boot would destroy the first crash before anyone
could fetch it.

## What none of this proves

⚠ **No core dump has ever been written or read on hardware.** The host suite
proves the frame codec, the status mapping and the chunk arithmetic; it cannot
prove that `esp_partition_read` on this partition returns what `espcoredump`
wrote, because only a real panic on real silicon writes it. The bench sequence
that would settle it: force a panic, let it reboot, check the boot log names the
reason, then `tool-usb.py dump crash.bin -p COM7` and `esp-coredump` against
`app_updater.elf` must print a backtrace into `app.c`.

## See also

- [../interface/coredump-api.md](../interface/coredump-api.md) — the driver contract, and what each call costs
- [../interface/command-map.md](../interface/command-map.md) — the `0x07` range on the wire
- [usb-command-dispatch.md](usb-command-dispatch.md) — how a 4 KB answer leaves a dispatcher whose payload buffer is 32 bytes
- [boot-and-bring-up.md](boot-and-bring-up.md) — where the boot-time report sits in the sequence
