---
title: Layering and Dependencies
category: architecture
order: 2
purpose: The call direction between layers, the component dependency graph, and why there are two separate error code spaces.
status: active
updated: 2026-09-07
source: application/app/CMakeLists.txt, application/updater/CMakeLists.txt, middleware/*/CMakeLists.txt, driver/bsp/CMakeLists.txt
confidence: confirmed
keywords: REQUIRES, PRIV_REQUIRES, layering, dependency direction, callback, fw_err_t, bsp_err_t, usb_cdc_err_t, command_busy_cb_t
---

# Layering and Dependencies

> Calls go down, events come back up through a callback, and no file includes a header from a layer above it.

## Responsibility

The layer a module sits in decides what it may include. Naming the layer in the
path makes a violation visible at review, in the `#include` line, before anyone
opens a header.

```mermaid
flowchart TD
    APP["application/ - app, updater"]
    MW["middleware/ - fw, ota_http, storage, protocol, command"]
    DRV["driver/ - bsp pins and clock (+ per-chip port), usb_cdc the USB stack"]
    SDK["ESP-IDF - nvs_flash, esp_http_client, app_update, esp_timer, esp_tinyusb"]

    APP --> MW --> DRV --> SDK
    MW -. "event callback" .-> APP
```

## Component dependency graph

Each module is one ESP-IDF component. `REQUIRES` means the dependency appears in
the module's own public header; `PRIV_REQUIRES` means only the `.c` uses it.

| Component | REQUIRES | PRIV_REQUIRES (ours) | PRIV_REQUIRES (SDK) |
|-----------|----------|----------------------|---------------------|
| `app` | `fw` | `bsp`, `command`, `protocol`, `storage`, `updater`, `usb_cdc` | `app_update`, `esp_app_format`, `esp_partition`, `esp_timer`, `freertos`, `nvs_flash` |
| `updater` | `fw` | `ota_http`, `storage` | `app_update`, `esp_partition` |
| `ota_http` | `fw` | — | `esp_http_client`, `esp-tls` |
| `command` | `fw`, `protocol` | — | `app_update`, `esp_app_format`, `esp_partition`, `esp_hw_support`, `esp_system`, `esp_rom`, `freertos` |
| `protocol` | `fw` | — | `esp_rom` |
| `storage` | `fw` | — | `nvs_flash`, `esp_rom` |
| `fw` | — | — | — |
| `bsp` | — | — | `esp_driver_gpio`, `esp_hw_support`, `spi_flash`, `soc` (via the common requires, for the port's pin headers) |
| `usb_cdc` | — | — | `esp_tinyusb`, `freertos` |

`fw` is a leaf: it depends on nothing, which is what lets both layers above it
include it.

## Two error code spaces, and where they meet

| Space | Type | Used by | Why |
|-------|------|---------|-----|
| Project-wide | `fw_err_t` | `application/`, `middleware/` | One code space means the application handles a timeout the same way whichever module produced it. |
| Driver-local | `bsp_err_t` | `driver/bsp/` | The driver layer must not include a middleware header, so it cannot use `fw_err_t`. |
| Driver-local | `usb_cdc_err_t` | `driver/usb_cdc/` | Same reason. Mapped in `bring_up_usb()`, the one place that knows both. |
| Wire | `protocol_status_t` | the USB channel | Not an internal code at all: these are bytes a PC parses, so they may never be renumbered. `middleware/command` returns them; nothing converts them to `fw_err_t` because they mean different things. |

Both share the same meanings for the generic range `-1 .. -19`, so the map
between them is one-for-one. It is applied in exactly one place per driver, inside
`bring_up_bsp()` and `bring_up_usb()` in `application/app/src/app.c`. If a
driver ever grows a code the application must distinguish, those functions are
the only edit.

Vendor status codes (`esp_err_t`) never escape the module that called the SDK:
each of `bsp`, `storage` and `ota_http` has a private `from_esp_err()` helper
that logs the vendor value and returns the module's own code.

## Reproduction notes

- Verified: nothing under `driver/` includes `fw.h`, `storage.h`, `ota_http.h`,
  `protocol.h`, `command.h`, `updater.h` or `app.h`; nothing under
  `middleware/` includes `updater.h` or `app.h`.
- `middleware/fw` is included sideways by its middleware siblings. That is a
  one-way include of a leaf, not a cycle.
- **The USB channel is the sharpest case of "calls go down".** The dispatcher
  in `middleware/command` has to know whether the update cycle - which lives in
  `application/` - is already writing an OTA slot. It may not include
  `updater.h`, so it **asks** through a `command_busy_cb_t` the application
  supplies, exactly as `ota_http` reports progress upward without knowing who it
  notifies. Reading the state directly would have been an upward include and is
  the one thing this layering forbids.
- A driver must never report upward with a direct call. `updater` receives state
  changes from nothing below it, but `ota_http` reports progress upward through
  a `void *ctx` callback supplied at init — the module does not know the name of
  what it notifies.
- `esp_err_t` must not appear in any public header above the driver layer.
  `ota_http_t` and `storage_t` therefore hold their vendor handles as `void *`
  and `uint32_t` respectively, described in their interface docs.

## See also

- [repo-layout.md](repo-layout.md) — the tree these layers occupy
- [../data/error-code-model.md](../data/error-code-model.md) — the two code spaces in detail
