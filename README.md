# App Updater

[![CI](https://github.com/dtbao-embedded-dev/app-updater/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dtbao-embedded-dev/app-updater/actions/workflows/ci.yml)
[![version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![target](https://img.shields.io/badge/target-ESP32--S3-e7352c)](#hardware-assumptions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-informational)](https://docs.espressif.com/projects/esp-idf/en/v6.1/)
[![C](https://img.shields.io/badge/C-11%20%28gnu11%29-lightgrey)](#build)
[![license](https://img.shields.io/badge/license-UNLICENSED-lightgrey)](#license)

Recovery-side firmware for an ESP32-S3 product: it puts a new product image
into the `app_firmware` slot — fetched over HTTPS on a schedule, or pushed down
a USB cable from a PC — and lets the bootloader confirm it or roll back on the
next boot.

- **Target**: ESP32-S3, ESP-IDF **v6.1**, C11 (`-std=gnu11`)
- **Version**: 0.1.0 — see [CHANGELOG.md](CHANGELOG.md)

> **This repo is one of a pair.** `app_updater` (this one) is the small recovery
> app; `app_firmware` (sibling repo) is the product. They occupy the two app
> slots of the same flash — see [Hardware assumptions](#hardware-assumptions).

## Build

```bash
python docs/scripts/tool-esp.py -w 0xF001 build
```

`0xF001` is the product, and naming it is **required** — that is the only
workspace in the repo today and nothing is inferred, because a product nobody
chose is a product nobody checked. Put `WORKSPACE=0xF001` in `.env.esp` to stop
typing `-w`, per machine. Add `workspace/0xF002/` and the refusal simply lists
both.

**The first run writes `.env.esp` and stops.** Paste the path to your ESP-IDF
checkout after `IDF_PATH=` and run it again — the script exports ESP-IDF from
there itself, so there is no `export.sh` to remember. That file is per-machine
and gitignored.

A checkout is not enough: its tools are installed once with
`install.bat esp32s3` (or `install.sh esp32s3`), and the script says exactly
that, quoting ESP-IDF's own error, when they are missing.

The repo root has **no** `CMakeLists.txt` — the build entry lives in
`workspace/0xF001/`, which is why every command goes through the wrapper.

Two things the build reads that are worth knowing before changing either:

- **`VERSION`** is the one source of the version number. It reaches the image
  header as `PROJECT_VER`, so what the firmware reports on boot is that file
  and nothing else.
- **`middleware/fw/include/fw_config.h`** holds the compile-time feature
  switches. Setting `FW_FEATURE_USB_COMMAND` to 0 drops the USB command channel
  from the image and gives back **67 688 bytes of `.bss`** (measured, 20.7 % of
  DRAM); `FW_FEATURE_UPDATER` drops the scheduled HTTP update cycle. Both
  default to 1.

## Flash and monitor

```bash
python docs/scripts/tool-esp.py -w 0xF001                  # build + flash + monitor
python docs/scripts/tool-esp.py -w 0xF001 flash            # port found for you
python docs/scripts/tool-esp.py -w 0xF001 monitor -p COM7  # or name it
python docs/scripts/tool-esp.py -w 0xF001 size             # flash and RAM budget
python docs/scripts/tool-esp.py -w 0xF001 merge            # one image for a blank board

python docs/scripts/tool-esp.py -w 0xF001 erase-flash                       # whole chip
python docs/scripts/tool-esp.py -w 0xF001 erase-flash --address 0x19000 --size 0x4000
python docs/scripts/tool-esp.py -w 0xF001 erase-flash --address 0x220000 --size all
```

**With no command at all** it builds, flashes and monitors in one go, finding
the serial port itself. One port on the machine is used and named; none, or
more than one, is an error listing what it saw — guessing between two boards is
how firmware lands on the wrong one. `flash` stays attached afterwards: the
boot log is what says whether it worked.

**Flashing needs UART0 or the BOOT button**, not plain USB. The command channel
claims the chip's single USB PHY, so there is no USB auto-download reset any
more — see [Hardware assumptions](#hardware-assumptions).

`merge` writes one image that provisions a blank board in a single `esptool`
write at `0x0`, named from the build rather than by hand:

```text
workspace/0xF001/build/bl_app_updater_0xF001_Sep0726.bin
                       ^^^ ^^^^^^^^^^^ ^^^^^^ ^^^^^^^
                       |   project()   pid    build date, MonDDYY UTC
                       starts at the bootloader
```

Two builds on the same day produce the same name and the second overwrites the
first without a word. The version is deliberately not in the name: it is in the
image header, and `python docs/scripts/tool-usb.py version` reads it back off a
running unit, which a file name cannot be checked against.

`erase-flash` finds the port like the other board commands, so with one board
plugged in it needs no argument — and it erases that board with no confirmation
step. `--address`/`--size` narrow it to a single region (sector multiples only);
`--size all` runs from that address to the end of the flash. A partition-table
change needs the whole-chip form: move a partition and the old `otadata` is
left at an offset the new table calls something else, so the bootloader reads a
slot selection out of whatever happens to be there.

## Layout

```text
application/       what the product does — no register, no pin
  app/             entry point (app_main), brings every module up
  updater/         the update cycle: when to check, when to give up
middleware/        product logic, no vendor SDK — see the rule below
  fw/              fw_err_t, fw_crc32_le(), the fw_config.h feature switches
  cfg/             the settings record, its defaults and validated accessors
  ota_http/        fetches an image over HTTPS, chunk by chunk
  protocol/        the USB wire format: frames, status codes, opcode map
  command/         dispatches a decoded USB frame to its handler
driver/            everything that touches the SDK or a pin
  bsp/             pin map, clock, flash geometry, MAC, reset
  ota/             the OTA slots: session, boot slot, running slot, confirm
  storage/         one opaque blob in NVS, with a wear guard
  usb_cdc/         the CDC-ACM byte pipe on USB-OTG (TinyUSB)
workspace/0xF001/  build entry: CMakeLists, sdkconfig.defaults, partitions.csv
test/host/         Unity runner, fake/ for our drivers, stub/ for vendor headers
docs/scripts/      developer commands (Python 3)
docs/.githooks/    git hooks (Python 3)
docs/memory-ai/    memory bank — read this before changing anything
```

Calls go down and events come back up through a callback. **Middleware calls
drivers, never the vendor SDK** — two named exceptions, both written up with
the greps that check them, in
[docs/memory-ai/rule/layer-boundaries.md](docs/memory-ai/rule/layer-boundaries.md).

## Hardware assumptions

Not yet measured on a board — every number below is a placeholder from
`driver/bsp/src/bsp.c` and must be checked against the 0xF001 schematic before
the first bring-up.

- Status LED on **GPIO2**, active high. `BSP_GPIO_NONE` disables it.
- **16 MB** SPI flash. The two app slots hold **two different applications**, not
  two copies of one:

  | Partition | SubType | Size | Holds |
  |-----------|---------|------|-------|
  | `app_updater` | `ota_0` | 2 MB | this repo |
  | `app_firmware` | `ota_1` | 13.875 MB | the sibling `app-firmware` repo |

  A firmware that fails to confirm itself falls back to the updater, which can
  fetch a replacement. **The cost: there is only one updater image, so the
  updater itself has no slot to roll back to.** A bad updater is a bad unit.
- No `factory` partition — the bootloader never rolls back to one.
- **One internal USB PHY, shared.** USB-OTG and USB-Serial-JTAG take turns on
  GPIO19/20 and only one works at a time; the command channel claims it, so the
  console and boot log are on **UART0** (GPIO43/44) and flashing over USB needs
  the BOOT button. Do not burn `EFUSE_USB_PHY_SEL` — it is one-way.
- The USB channel costs about **66 KB of static RAM** (two 32788-byte frame
  buffers), which is 20.7 % of DRAM. `FW_FEATURE_USB_COMMAND` at 0 gives it
  back.
- Network bring-up (Wi-Fi or Ethernet) is **not** in this repo. The updater
  assumes something else brought the interface up before a fetch runs.
- `ota_http` reads the body into a 1 KB stack buffer, so the task that calls
  `ota_http_fetch()` needs that much headroom on top of the TLS stack.

## Test

```bash
python docs/scripts/tool-esp.py test
```

Configures `test/host/`, compiles the module tests with **the same warning set
and `-Werror` the firmware uses**, and runs them under CTest. No board needed,
and no workspace to name — the host suite covers the whole repo. Unity comes
from your ESP-IDF checkout; point it elsewhere once with
`cmake -S test/host -B build/host -G Ninja -DUNITY_DIR=<dir with unity.c>`.

The tests live next to their modules; `test/host/` holds only what it takes to
run them on a PC, in two directories that mean different things:

- **`fake/`** — host implementations of **our own** driver headers (`ota_fake.c`,
  `bsp_fake.c`, `nvs_fake.c`). This is where a test of middleware belongs: it
  drives the same contract the target implementation has to satisfy.
- **`stub/`** — shadows of **vendor** headers, down to two files. One is
  `esp_log.h`; the other is a deliberately independent CRC-32 that
  `fw_crc32_le()` is compared against, so the two are only interchangeable if
  they agree.

## Contributing

```bash
git config core.hooksPath docs/.githooks   # once per clone
python docs/scripts/tool-esp.py format     # clang-format decides layout
python docs/scripts/tool-esp.py analyse    # cppcheck + clang-tidy
```

Branches: work on `developing`, branch per task, merge into `main` through a
pull request — `main` is push-protected. The pre-commit hook refuses a commit
whose staged C sources are not `clang-format` clean; CI re-checks it with the
**same pinned version**, so the two cannot disagree. Commit messages carry no
AI attribution trailer — see
[docs/memory-ai/rule/commit-messages.md](docs/memory-ai/rule/commit-messages.md).

Every `R-XXX-nn` in a comment cites the house embedded-C standard; see
[docs/memory-ai/rule/coding-standard-source.md](docs/memory-ai/rule/coding-standard-source.md).

## License

Copyright © 2026 dtbao. All rights reserved.

`UNLICENSED` — no licence to copy, redistribute, publish, or create derivative
works is granted, and no warranty of any kind is given or implied.
