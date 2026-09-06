# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

## [Unreleased]

### Changed
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
