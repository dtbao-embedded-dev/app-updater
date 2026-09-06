# app-updater

Keeps an ESP32-S3 product up to date in the field: it checks a manifest on a
schedule, writes the new image into the spare OTA slot, and confirms or rolls
back on the next boot.

- **Target**: ESP32-S3, ESP-IDF 6.x, C11 (`-std=gnu11`)
- **Version**: 0.1.0 — see [CHANGELOG.md](CHANGELOG.md)

## Build

```bash
. $IDF_PATH/export.sh          # Windows: %IDF_PATH%\export.bat
python docs/scripts/tool-esp.py build
```

Builds product **0xF001**, the only workspace in this repo. The version comes
from the `VERSION` file, so the number the firmware reports on boot is that
file and nothing else.

## Flash and monitor

```bash
python docs/scripts/tool-esp.py flash --port COM7      # Linux: --port /dev/ttyUSB0
python docs/scripts/tool-esp.py monitor --port COM7
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
docs/scripts/      developer commands (Python 3)
docs/.githooks/    git hooks (Python 3)
```

Calls go down and events come back up through a callback; nothing under
`driver/` includes from a layer above it.

## Hardware assumptions

Not yet measured on a board — every number below is a placeholder from
`driver/bsp/src/bsp.c` and must be checked against the 0xF001 schematic before
the first bring-up.

- Status LED on **GPIO2**, active high. `BSP_GPIO_NONE` disables it.
- **4 MB** SPI flash, two 1.875 MB OTA slots and no factory partition —
  rollback works only between OTA slots. See
  `workspace/0xF001/partitions.csv`; changing a size there means changing
  `CONFIG_ESPTOOLPY_FLASHSIZE_*` in the same commit.
- Network bring-up (Wi-Fi or Ethernet) is **not** in this repo yet. The
  updater assumes something else has brought the interface up before
  `ota_http_fetch()` runs.
- `ota_http` reads the body into a 1 KB stack buffer, so the task that calls
  `ota_http_fetch()` needs that much headroom on top of the TLS stack.

## Hooks

```bash
git config core.hooksPath docs/.githooks
```

The pre-commit hook refuses a commit whose staged C sources are not
`clang-format` clean. Fix with `python docs/scripts/tool-esp.py format`.

## License

Copyright © 2026 dtbao. All rights reserved. UNLICENSED — no licence to copy,
redistribute, publish, or create derivative works is granted, and no warranty
of any kind is given or implied.
