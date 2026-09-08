---
title: Repository Layout
category: architecture
order: 1
purpose: The three-layer source tree, the shape of one module, and where build entries, scripts and hooks live.
status: active
updated: 2026-09-07
source: application/, middleware/, driver/, workspace/0xF001/, test/host/, .github/workflows/, docs/, README.md
confidence: confirmed
keywords: application, middleware, driver, bsp, usb_cdc, coredump, protocol, command, workspace, 0xF001, include, src, module directory
---

# Repository Layout

> Three top-level source directories name the three layers, one directory per module inside them, and a `workspace/<product-id>/` that composes them into an image without holding any source of its own.

## Responsibility

App Updater is firmware for an ESP32-S3 product whose job is to keep itself up
to date in the field: check a manifest on a schedule, write a new image into the
spare OTA slot, then confirm or roll back on the next boot.

The top level names the layer, so an upward dependency is visible in a path
before anyone opens a header.

## Layout

| Path | Holds |
|------|-------|
| `application/app/` | Entry point. Brings every module up, owns the main loop. |
| `application/updater/` | The update cycle: when to check, when to retry, when to give up. |
| `middleware/fw/` | The project-wide status code every app and middleware call returns. |
| `middleware/ota_http/` | Fetches an image over HTTPS and hands it out chunk by chunk. |
| `middleware/protocol/` | The USB wire format: frame codec, status codes, opcode map. |
| `middleware/command/` | Dispatches a decoded USB frame to the handler that serves it. |
| `middleware/cfg/` | The device's settings: the record, its defaults, and one validated get/set pair per setting. Persistence arrives as an adapter. |
| `driver/ota/` | The two app slots: sizes, versions, which one runs, which boots next, and the write session. The only module naming `esp_ota_*`. |
| `driver/storage/` | One opaque blob in NVS, with a read-compare-write wear guard. Knows nothing about what is in it. |
| `driver/bsp/` | Pin map, clock, flash geometry. The only place a pin number appears, and the only module with a per-chip port. |
| `driver/usb_cdc/` | The CDC-ACM byte pipe on USB-OTG. Owns the TinyUSB stack. |
| `driver/coredump/` | What the last panic left in the `coredump` partition: whether a dump is there and sound, its bytes, the panic reason as text, and the erase. The only module naming `esp_core_dump_*`. |
| `workspace/0xF001/` | Build entry for product 0xF001: CMakeLists, sdkconfig.defaults, partitions.csv. |
| `test/host/` | Unity runner, the host fakes for the SDK headers our logic includes, and the CMake that builds them. |
| `.github/workflows/` | CI on every push, release on every `v*` tag. |
| `docs/scripts/` | Developer commands (Python 3). |
| `docs/.githooks/` | Git hooks (Python 3). |
| `docs/memory-ai/` | This memory bank. |

There is no `third_party/` yet, but there is now one **managed dependency**:
`driver/usb_cdc/idf_component.yml` pulls `espressif/esp_tinyusb`, because
ESP-IDF v6.1 ships no USB device stack at all. It lands in
`workspace/0xF001/managed_components/`, which is gitignored along with
`dependencies.lock`, so a fresh clone resolves it on its first build.
 `test/` exists but
holds only the **host harness** — the tests themselves stay beside their modules
(R-RPO-07), and no on-target test exists yet.

## Module shape

Every directory under the three layer directories has the same inside:

| File | Role |
|------|------|
| `include/<mod>.h` | The single public header. The only file an outsider includes. |
| `src/<mod>.c` | Implementation. |
| `src/<mod>_priv.h` | Internal declarations. Present only where something is actually shared: `application/app/`, `middleware/command/` and `driver/bsp/`. |
| `src/port/<mod>_<target>.c` | The per-chip half of a module, one file per MCU family, picked by `IDF_TARGET` (R-LIB-02). Present only in `driver/bsp/`. |
| `test/test_<mod>.c` | Host tests, present for `fw`, `updater`, `protocol`, `command` and `cfg`. Each function must also be listed in `test/host/runner.c` or it never runs. |
| `CMakeLists.txt` | ESP-IDF component registration. |

The directory name, the public header name, and the symbol prefix are the same
word, so one grep for the prefix finds the folder, the file and every symbol in
it. Verified: each of `app`, `updater`, `fw`, `ota_http`, `protocol`, `command`,
`cfg`, `storage`, `bsp`, `usb_cdc` is declared in exactly one module directory.

`middleware/command/` has two sources - `command.c` for the dispatch and the
stateless handlers, `command_upgrade.c` for the image transfer session - which
is what `command_priv.h` exists to bridge.

`driver/bsp/` splits on a different axis: not by object but by **chip**.
`src/bsp.c` holds the board logic and names no chip; `src/port/bsp_esp32s3.c`
holds what only the S3 can answer, behind the one function in `src/bsp_priv.h`.
The CMake picks the port by `IDF_TARGET` and stops with an instruction when
there is no file for the target, so a retarget cannot silently compile against
another chip's pinout. Adding a chip is adding one file under `src/port/`,
never an `#ifdef` inside `bsp.c` - see
[../interface/bsp-api.md](../interface/bsp-api.md).

## Dependencies & build

Toolchain is ESP-IDF 6.x targeting ESP32-S3, C11. The build entry is
`workspace/0xF001/`; it reaches the layer directories through
`EXTRA_COMPONENT_DIRS` rather than copying or symlinking, so one
`driver/bsp/` compiles into every product that lists it. See
[build-and-toolchain.md](build-and-toolchain.md).

## Reproduction notes

- A module lives in exactly one layer directory. A module that looks like it
  belongs to two is two modules: the part that knows the device goes down, the
  part that knows why it is used stays up.
- **No source of ours ever lives under `workspace/`.** That is why
  `application/app/` provides `app_main()` and ESP-IDF has no `main` component
  here — see [build-and-toolchain.md](build-and-toolchain.md).
- Pin numbers and peripheral instances live only in `driver/bsp/`. Verified: no
  pin literal appears under `application/` or `middleware/`.
- A second product takes the next id as a sibling of `workspace/0xF001/`, never
  a `<mcu>-<role>` name and never a `#if` inside a module. The directory with a
  `CMakeLists.txt` in it is all the tooling needs: `tool-esp.py` discovers
  workspaces from this listing and takes the default from `WORKSPACE=` in
  `.env.esp` — see
  [../interface/tool-esp-cli.md](../interface/tool-esp-cli.md).

## See also

- [layering-and-dependencies.md](layering-and-dependencies.md) — which layer may call which
- [build-and-toolchain.md](build-and-toolchain.md) — how the tree is compiled
- [../rule/coding-standard-source.md](../rule/coding-standard-source.md) — where the R-XXX-nn rules come from
