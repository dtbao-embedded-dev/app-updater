# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

## [Unreleased]

### Changed
- **`middleware/storage` no longer knows what it stores.** The record, its
  defaults, its version and its CRC moved to the new `middleware/cfg`;
  `storage_record_t`, `storage_record_default`, `storage_record_load` and
  `storage_record_save` are gone, replaced by `storage_blob_load` /
  `storage_blob_save` over an opaque byte blob with a `STORAGE_BLOB_MAX`
  ceiling. That ceiling is what lets the read-compare-write wear guard keep a
  fixed compare buffer instead of a VLA on a task stack. `nvs_flash` stays;
  `esp_rom` left with the CRC. **No migration is needed:** the record layout
  and the NVS key are unchanged, so a unit holding a valid record still reads
  it.
- `application/app` holds a `cfg_t` instead of a `storage_record_t` and takes
  the check interval from `cfg_check_interval_ms_get()`. The two adapter
  wrappers in `bring_up_storage()` are now the only place in the image that
  knows the settings live in NVS.
- **`driver/bsp` gained a per-chip port.** The USB and console pin numbers left
  `bsp.h`: they are fixed in silicon, so the chip's own SDK headers under
  `components/soc/<IDF_TARGET>/` are the one place that states them, and
  `src/port/bsp_<IDF_TARGET>.c` reads them behind the new `bsp_priv.h`
  contract. `driver/bsp/CMakeLists.txt` picks the port from `IDF_TARGET` and
  fails with an instruction rather than a missing-source error when a new
  target has none. Adding a chip is adding a file, never an `#ifdef` inside
  `bsp.c`.
- **`tool-esp.py` no longer hardcodes the product workspace.** A workspace is
  any directory under `workspace/` holding a `CMakeLists.txt`, so that listing
  is the list of products; the default comes from `WORKSPACE=` in `.env.esp`
  and `-w NAME` overrides it for one run. **Naming the product is required**,
  including while the repo holds exactly one workspace: nothing is inferred,
  because a product nobody chose is a product nobody checked and a build against
  the wrong one looks exactly like a build that worked. So fill `WORKSPACE=` in
  once per machine; `ci.yml` and `release.yml` pass `-w 0xF001` on their five
  build steps, a runner having no `.env.esp`, and that flag is now the only
  place those workflows state which product they build. `-w` is rejected for
  `format`, `test` and `analyse`, which cover the whole repo. The `.env.esp`
  template documents the case with the refusal it is answering - interpolated
  from the same function that prints it, so the two cannot drift - and that
  refusal creates the file when it is missing rather than naming a file a fresh
  clone does not have yet.
- **The console and boot log moved to UART0** (GPIO43/44). ESP32-S3 has a single
  internal USB PHY shared between USB-OTG and USB-Serial-JTAG, and the USB
  command channel claims it - so a log left on USB-Serial-JTAG would go silent.
  The consequence for anyone flashing: no more USB auto-download reset, so use
  a UART0 bridge or hold BOOT. Do not burn `EFUSE_USB_PHY_SEL`; it is one-way.
- The repo now has a managed dependency, `espressif/esp_tinyusb ~2.0.0`, because
  ESP-IDF v6.1 ships no USB device stack. The first build after a clone resolves
  it and needs network for that.

### Fixed
- The published release artifacts could not flash a blank board. `flash_args`
  named `ota_data_initial.bin`, which was never uploaded, and referred to the
  bootloader and partition table by the directories they sit in inside
  `build/` while the assets are flat. Both are fixed, and the release workflow
  now fails if either recurs. **v0.1.0 is affected**; use the individual
  offsets from its release notes, or the factory image from the next tag.

### Added
- **A settings library, `middleware/cfg`.** It owns the settings record -
  manifest URL, last-good firmware version, check interval, boot-fail count,
  behind a `version`/`length`/`crc32` envelope - its compiled-in defaults, and
  one validated get/set pair per setting. Where the bytes go is **not** its
  business: persistence arrives as a `cfg_store_t` of two callbacks, so
  re-pointing the settings at the reserved `cfg_setting` partition later is a
  new adapter and not an edit inside the module. A setter writes nothing; the
  caller decides when a batch of changes is worth one flash write. 13 host
  tests, one of them proved to bite by mutation.
- Validation the settings never had: **both** stored strings must be
  NUL-terminated inside their own field (only the URL was checked before), a
  setter refuses a value it cannot hold and leaves the old one in place, and
  `check_interval_ms` is refused at or past the update cycle's scheduling
  horizon. That last one closed a real hole - a record that passed its CRC
  carrying a large interval used to reach `updater_init()` unchecked and **fail
  bring-up**; it now falls back to the compiled-in defaults like any other
  unusable record.
- **A USB command channel.** The device enumerates as a CDC-ACM serial port on
  VID `0xA331` / PID `0xF001` and answers the binary protocol from
  `data-monitor/data-mirror-firmware/docs/spec/usb`: `PING`, `RESTART_APP`,
  Set/Get `BOOT_SLOT`, `VERSION`, both eFuse MACs, and the full Upgrade range
  (`UPG_BEGIN` / `UPG_WRITE` / `UPG_END`) that writes an image into the
  `app_firmware` slot and verifies it whole before finalising it. Three new
  modules: `middleware/protocol` (frame codec and opcode map),
  `middleware/command` (dispatch and handlers) and `driver/usb_cdc` (the
  TinyUSB transport). Every other opcode the spec defines answers
  `PROTOCOL_ERR_UNSUPPORTED`, because this board has none of the hardware they
  address.
- `docs/scripts/tool-usb.py`, the host half of that channel: `selftest`
  (no board needed), `ping`, `version`, `boot-slot`, `restart` and `upgrade`.
- `docs/scripts/tool-esp.py` gained `fullclean`, which is what makes a new
  component, a new managed dependency or an edited `sdkconfig.defaults` take
  effect - `clean` keeps the CMake cache and silently ignores all three.
- Releases now carry a merged `app-updater-<tag>-factory.bin` that provisions a
  blank board with one command at `0x0`, plus `ota_data_initial.bin` and the
  `sdkconfig` that produced the image.

## Released

Newest first. One file per version.

| Version | Date | Summary |
|---------|------|---------|
| [0.1.0](docs/CHANGELOG/v0.1.0.md) | 2026-09-06 | First scaffold, its CI and its memory bank. |

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/compare/v0.1.0...main
[0.1.0]: https://github.com/dtbao-embedded-dev/app-updater/releases/tag/v0.1.0
