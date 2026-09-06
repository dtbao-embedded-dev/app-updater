# app-updater

[![CI](https://github.com/dtbao-embedded-dev/app-updater/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dtbao-embedded-dev/app-updater/actions/workflows/ci.yml)
[![version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![target](https://img.shields.io/badge/target-ESP32--S3-e7352c)](#hardware-assumptions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-informational)](https://docs.espressif.com/projects/esp-idf/en/v6.1/)
[![C](https://img.shields.io/badge/C-11%20%28gnu11%29-lightgrey)](#build)
[![license](https://img.shields.io/badge/license-UNLICENSED-lightgrey)](#license)

Recovery-side firmware for an ESP32-S3 product: it checks a manifest on a
schedule, writes the new product image into the `app_firmware` slot, and lets
the bootloader confirm it or roll back on the next boot.

- **Target**: ESP32-S3, ESP-IDF **v6.1**, C11 (`-std=gnu11`)
- **Version**: 0.1.0 — see [CHANGELOG.md](CHANGELOG.md). **Not released**; no
  tag exists yet, and [the open holes](docs/memory-ai/rule/known-deviations.md)
  are why.

> **This repo is one of a pair.** `app_updater` (this one) is the small recovery
> app; `app_firmware` (sibling repo) is the product. They occupy the two app
> slots of the same flash — see [Hardware assumptions](#hardware-assumptions).

## Build

```bash
. $IDF_PATH/export.sh          # Windows: %IDF_PATH%\export.bat
python docs/scripts/tool-esp.py build
```

Builds product **0xF001**, the only workspace in this repo. The version comes
from the `VERSION` file and is injected into the image header, so the number the
firmware reports on boot is that file and nothing else.

The repo root has **no** `CMakeLists.txt` — the build entry lives in
`workspace/0xF001/`, which is why every command goes through the wrapper.

## Test

```bash
python docs/scripts/tool-esp.py test
```

Configures `test/host/`, compiles the module tests with **the same warning set
and `-Werror` the firmware uses**, and runs them under CTest. No board needed.
Unity comes from your ESP-IDF checkout; point it elsewhere once with
`cmake -S test/host -B build/host -G Ninja -DUNITY_DIR=<dir with unity.c>`.

## Flash and monitor

```bash
python docs/scripts/tool-esp.py flash --port COM7      # Linux: --port /dev/ttyUSB0
python docs/scripts/tool-esp.py monitor --port COM7
python docs/scripts/tool-esp.py size                   # flash and RAM budget
```

`flash` stays attached afterwards — the boot log is what says whether it
worked. Leave `--port` off and esptool picks the port itself.

## Layout

```text
application/       what the product does — no register, no pin
  app/             entry point (app_main), brings every module up
  updater/         the update cycle: when to check, when to give up
middleware/        protocol and storage, product-agnostic
  fw/              the project-wide status code, fw_err_t
  ota_http/        fetches an image over HTTPS, chunk by chunk
  storage/         the persisted record, versioned and CRC-checked
driver/
  bsp/             pin map, clock, flash geometry — the only pin numbers
workspace/0xF001/  build entry: CMakeLists, sdkconfig.defaults, partitions.csv
test/host/         Unity runner + host fakes; the tests live with their modules
docs/scripts/      developer commands (Python 3)
docs/.githooks/    git hooks (Python 3)
docs/memory-ai/    memory bank — read this before changing anything
```

Calls go down and events come back up through a callback; nothing under
`driver/` includes from a layer above it.

## Hardware assumptions

Not yet measured on a board — every number below is a placeholder from
`driver/bsp/src/bsp.c` and must be checked against the 0xF001 schematic before
the first bring-up.

- Status LED on **GPIO2**, active high. `BSP_GPIO_NONE` disables it.
- **4 MB** SPI flash. The two app slots hold **two different applications**, not
  two copies of one:

  | Partition | SubType | Size | Holds |
  |-----------|---------|------|-------|
  | `app_updater` | `ota_0` | 1.875 MB | this repo |
  | `app_firmware` | `ota_1` | 1.875 MB | the sibling `app-firmware` repo |

  A firmware that fails to confirm itself falls back to the updater, which can
  fetch a replacement. **The cost: there is only one updater image, so the
  updater itself has no slot to roll back to.** A bad updater is a bad unit.
- No `factory` partition — the bootloader never rolls back to one.
- Network bring-up (Wi-Fi or Ethernet) is **not** in this repo. The updater
  assumes something else brought the interface up before a fetch runs.
- `ota_http` reads the body into a 1 KB stack buffer, so the task that calls
  `ota_http_fetch()` needs that much headroom on top of the TLS stack.

## Contributing

```bash
git config core.hooksPath docs/.githooks   # once per clone
python docs/scripts/tool-esp.py format     # clang-format decides layout
```

Branches: work on `developing`, branch per task, merge into `main` through a
pull request — `main` is push-protected. The pre-commit hook refuses a commit
whose staged C sources are not `clang-format` clean; CI re-checks it with the
**same pinned version**, so the two cannot disagree.

Every `R-XXX-nn` in a comment cites the house embedded-C standard; see
[docs/memory-ai/rule/coding-standard-source.md](docs/memory-ai/rule/coding-standard-source.md).

## License

Copyright © 2026 dtbao. All rights reserved.

`UNLICENSED` — no licence to copy, redistribute, publish, or create derivative
works is granted, and no warranty of any kind is given or implied.
