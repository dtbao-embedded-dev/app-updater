---
title: Middleware Calls Only Mapped Drivers
category: rule
order: 8
purpose: The rule that keeps the vendor SDK out of middleware and application code, its two named exceptions, and the grep that enforces it.
status: active
updated: 2026-09-07
source: driver/ota/include/ota.h, driver/bsp/include/bsp.h, driver/storage/include/storage.h, middleware/fw/include/fw.h, middleware/command/src/command_upgrade.c, middleware/ota_http/src/ota_http.c
confidence: confirmed
keywords: R-LAY-01, R-LAY-03, R-LAY-04, mapped driver, wrapper, esp_ota_begin, esp_log, esp_http_client, esp_rom_crc32_le, nvs, layering, portability line, driver fake
---

# Middleware Calls Only Mapped Drivers

> A file under `middleware/` or `application/` calls a driver in `driver/`, never the vendor SDK directly — with exactly two exceptions, both named below.

## When this applies

Whenever you are about to write `#include "esp_...h"`, `#include "nvs...h"`,
`#include "driver/...h"`, `#include "soc/...h"` or `#include "freertos/...h"`
in a file under `middleware/` or `application/`. Also when reviewing a diff
that adds one.

## The rule

`R-LAY-01` in the house standard says calls go down and no layer includes a
header from the layer above it. Its table also lets middleware call "HAL
primitives", which read on its own permits `esp_ota_begin()` in
`middleware/command`. **This repo reads it more strictly than that**, because
the looser reading is what produced the defect this rule was written after:
`command_upgrade_begin()` called `esp_ota_begin()` directly, so the USB upgrade
path — the product's entire bench and production route — was nailed to one
vendor's SDK, and its tests could only run by shadowing vendor headers with
fakes that pretended to be ESP-IDF.

So, here:

| Layer | May include | Must not |
|-------|-------------|----------|
| `application/` | `middleware/`, `driver/`, the two exceptions | Any other vendor header |
| `middleware/` | `driver/`, sibling `middleware/`, the two exceptions | Any other vendor header |
| `driver/` | The vendor SDK, freely — this is its job | A header from `middleware/` or `application/` |

A capability the vendor SDK provides and middleware needs gets a **mapped
driver**: a module under `driver/` with its own `<mod>_err_t` code space
(R-LAY-03 — no `esp_err_t` above the driver layer), its own opaque handle type
where the SDK has one, and a `from_esp_err()` private helper that is the last
place the vendor status exists.

The four that exist:

| Driver | Wraps | Handle type it hides |
|--------|-------|----------------------|
| `driver/ota` | `esp_ota_*`, `esp_partition_*`, `esp_app_get_description` | `ota_session_t` over `esp_ota_handle_t` |
| `driver/bsp` | GPIO, `esp_flash`, `esp_read_mac`, `esp_restart`, chip pin headers | — |
| `driver/storage` | `nvs_*`, `nvs_flash_*` | `uint32_t` over `nvs_handle_t` |
| `driver/usb_cdc` | TinyUSB | — |

A pure function with no hardware behind it does not need a driver — it needs a
home in `middleware/fw`, the dependency-free leaf. `fw_crc32_le()` is the
worked example: it replaced `esp_rom_crc32_le()` in three modules for 128 bytes
of image, and it is checked against records and frames that outlive any one
chip, so it may not depend on one chip's ROM.

## The two exceptions

**1. `esp_log.h`.** Every module keeps using `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`
with its own `TAG`. A wrapper usable by `driver/bsp` would have to sit *below*
the driver layer, which is precisely where the SDK's own logging already sits —
so the wrapper would buy nothing and cost an edit at every log line in the
repo. This is the same reasoning already recorded as deviation 3 in
[known-deviations.md](known-deviations.md); the host harness shadows
`esp_log.h` with a sink, which is all a test needs.

**2. `middleware/ota_http` keeps `esp_http_client`.** HTTP is a protocol stack,
and the house layer table assigns protocols and stacks to *middleware*, not to
`driver/`, whose remit is "one device or peripheral role". An HTTP client is
not a device. Wrapping it in `driver/` would put a non-device in the driver
layer to satisfy the letter of a rule whose purpose — R-LAY-04, the portability
line at the driver/BSP seam — it does not serve: `esp_http_client` is portable
across every ESP part this repo will ever build for. The vendor status still
stops at the module boundary: `ota_http.c` has its own `from_esp_err()` and
`ota_http_t` holds its handle as `void *`.

Both exceptions are recorded in [known-deviations.md](known-deviations.md) as
deliberate, so a reviewer finds a decision there rather than an oversight.

## How to check it

Two greps, and they are the same ones each refactor was gated on. Firmware
sources only — `*/src` and `*/include` — because the test tree deliberately
keeps an independent CRC-32 reference under `test/host/stub/esp_rom_crc.h` for
`fw_crc32_le()` to be compared against:

```bash
# Anything vendor-shaped in middleware, except the one permitted include.
grep -rn '#include "esp_\|#include "nvs\|#include "freertos/\|#include "soc/\|#include "driver/' \
     middleware/*/src middleware/*/include | grep -v 'esp_log.h' | grep -v 'ota_http'

# The specific APIs that used to be called directly, plus the ones a mapped
# driver now owns and nothing above it may reach for.
grep -rn 'esp_ota_\|esp_partition_\|esp_core_dump_\|nvs_\|esp_restart\|esp_read_mac\|esp_rom' \
     middleware/*/src middleware/*/include application/*/src application/*/include
```

The first must come back empty. The second is expected to return **comment
prose only** — as of 2026-09-07, exactly two lines: `middleware/fw/include/fw.h`
naming the ROM routine `fw_crc32_le()` replaced, and the `TODO` in
`application/updater/src/updater.c` saying *not* to call `esp_ota_*` when the
DOWNLOADING step is written. A hit on a line that is not a comment is the
violation.

Neither grep is wired into CI yet — that is a hole, not a claim: today the rule
is enforced at review with these two commands. Both were run against the tree
this doc was written from.

## Why it is worth the wrapper

The reason is not tidiness. Three things fall out of it that did not exist
before:

- **A host test of middleware fakes a contract we own.** `test/host/fake/`
  holds `ota_fake.c`, `bsp_fake.c` and `nvs_fake.c` — implementations of *our*
  headers. Before, `test/host/stub/` held nine files impersonating ESP-IDF; it
  now holds two, and one of those is a deliberate second opinion rather than a
  stub.
- **The port to another part is a directory, not a search.** Every vendor call
  is under `driver/`, so there is a finite list to rewrite and nothing above it
  to audit (R-LAY-04).
- **A feature can be compiled out.** `FW_FEATURE_USB_COMMAND` in
  `middleware/fw/include/fw_config.h` removes the USB channel from the image
  because the wiring is in one file and every dependency is named. Turning it
  off frees 67 688 bytes of `.bss`, measured.

## See also

- [../architecture/layering-and-dependencies.md](../architecture/layering-and-dependencies.md) — the component graph this rule produces
- [known-deviations.md](known-deviations.md) — where both exceptions are recorded
- [coding-standard-source.md](coding-standard-source.md) — how to resolve `R-LAY-01` itself
- [testing.md](testing.md) — the driver fakes the rule made possible
