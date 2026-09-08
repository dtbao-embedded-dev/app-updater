# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

## [Unreleased]

### Changed
- **Middleware calls mapped drivers, not the vendor SDK.** The defect this
  started from: `command_upgrade_begin()` called `esp_ota_begin()` directly, so
  the USB upgrade path - this product's entire bench and production route - was
  nailed to one vendor, and its 31 host tests could only run because
  `test/host/stub/` shadowed vendor headers with fakes pretending to be
  ESP-IDF. Fixed everywhere it occurred rather than only where it was reported:
  `middleware/command` routes OTA through `driver/ota` and restart/MAC through
  `driver/bsp`; `protocol`, `cfg` and `command` dropped `esp_rom` for
  `fw_crc32_le()`; `middleware/storage` moved wholesale to `driver/storage`.
  **The SDK column is now empty for every middleware component but
  `ota_http`**, and `esp_log.h` is the only vendor header middleware still
  includes - both exceptions recorded as deviations 10 and 11. Verified by the
  two greps in the new rule doc, not by inspection.
- **`middleware/storage` is now `driver/storage`.** It was never anything but a
  wrapped NVS blob store, so it belongs under the portability line. It carries
  its own `storage_err_t` instead of `fw_err_t` (R-LAY-01: a driver may not
  include a middleware header) and **took over `nvs_flash_init()`** rather than
  documenting that the caller must call it first - a caller that has to
  remember will forget. `from_storage_err()` in `application/app/src/app.c` is
  the one place that knows both code spaces, and it clamps anything outside the
  shared `-1 .. -19` range to `FW_ERR_IO` rather than casting a value
  `fw_err_str()` cannot name.
- **The merged factory image is named `bl_<project>_<pid>_<MonDDYY>.bin`** -
  today, `bl_app_updater_0xF001_Sep0726.bin`. Every field comes from the build:
  the `project()` token in the workspace CMakeLists, the workspace directory
  name, and the UTC date. The month comes from a fixed English table, not
  `strftime("%b")`, which follows `LC_TIME` and would make the artifact name
  depend on whose laptop built it. The version left the file name - it is in
  the image header via `PROJECT_VER`, and `tool-usb.py version` reads it back
  off a running unit, which a file name cannot be checked against.
  `release.yml` copies the image by glob and fails when it does not find
  exactly one. **Known ceiling:** the date has no time of day, so two builds on
  the same day overwrite each other silently.
- README rewritten into the R-RPO-08 section order, with `Test` and
  `Contributing` below the fold where the rule puts them, `-w 0xF001` in the
  build commands, and a Layout tree checked path by path against disk.
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
- A `TODO` in `application/updater/src/updater.c` told whoever writes the
  DOWNLOADING step to call `esp_ota_write()` and `esp_ota_set_boot_partition()`
  directly - exactly what the new layer rule forbids. It now names
  `ota_session_write()` and `ota_boot_slot_set()` and says why. Found by
  running the rule doc's own grep, not by review.
- `tool-esp.py --help` claimed `-w` "defaults to WORKSPACE in .env.esp, or to
  the only workspace in the repo". The second half is the inference
  `resolve_workspace()` deliberately dropped, so the help described behaviour
  the code refuses.
- `application/updater/CMakeLists.txt` no longer names the dead `storage`
  dependency; `updater.c` includes no header of it. `ota_http`, `app_update`
  and `esp_partition` stay and are equally unused today - the CHECKING and
  DOWNLOADING steps are written against them.
- The published release artifacts could not flash a blank board. `flash_args`
  named `ota_data_initial.bin`, which was never uploaded, and referred to the
  bootloader and partition table by the directories they sit in inside
  `build/` while the assets are flat. Both are fixed, and the release workflow
  now fails if either recurs. **v0.1.0 is affected**; use the individual
  offsets from its release notes, or the factory image from the next tag.

### Added
- **`tool-usb.py dump FILE`**, the host end of the read-out. `DUMP_INFO` for
  the size, a read loop in 4096-byte chunks, the file written **once from a
  complete transfer**, and only then `DUMP_ERASE` - so the device keeps the
  only copy until the bytes are on disk, and a failed write leaves the dump in
  place to be fetched again. An **absent** dump writes no file and exits **1**,
  because `dump crash.bin && esp-coredump ... crash.bin` must not run the
  second half against a file that was never written; a **corrupt** dump is
  fetched anyway with a warning, since that is exactly the one worth looking
  at. `--keep` skips the erase and says out loud what that costs. On success it
  prints the `esp-coredump info_corefile --core-format raw` line, and warns
  that the `.elf` must be the exact build that crashed - a rebuild produces a
  plausible, wrong backtrace with no warning.
- **The core dump partition now keeps the FIRST dump, not the last.**
  `CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE=y`, against the ESP-IDF default. In a
  boot loop the crash that explains the loop is the one that started it, and
  the default kept replacing it with a symptom of itself. **The cost,
  accepted:** clearing the partition stops being optional - `espcoredump`
  refuses to write while a dump is stored, so a unit read but never erased
  captures no further panic for the rest of its life. That is why `DUMP_ERASE`
  exists, why `tool-usb.py dump` erases by default, and why
  `report_last_panic()` at boot deliberately does not.
- **The boot log now says why the last run ended.** `report_last_panic()` in
  `application/app/src/app.c`, called right after `confirm_or_roll_back()` so
  R-VER-08 keeps the first word: if the `coredump` partition holds anything, it
  logs the panic reason as text plus whether the dump is valid or corrupt and
  how many bytes it is. Needs no module up, because `driver/coredump` has no
  lifecycle at all. **Silent on a clean boot** - a "no core dump" line every
  time trains a reader to skip the one boot where it matters - and it does
  **not** erase what it read, so `DUMP_READ` can still fetch it. The reason is
  already text when it arrives, so nothing here symbolises an address; that is
  the difference between a one-call step and a host-tool problem. Image grew
  0x3fc90 to 0x40d80.
- **The three core dump handlers, and the buffer they answer out of.**
  `middleware/command/src/command_dump.c` serves `DUMP_INFO` / `DUMP_READ` /
  `DUMP_ERASE` through `driver/coredump`. `DUMP_READ` could not use the
  dispatcher's payload buffer at all - `PAYLOAD_MAX` is 32 bytes on the stack of
  a task with a 4096-byte stack - so it stages its answer in a new 4096-byte
  `command_t.chunk` and points the existing `echo` route at it, the same
  mechanism PING uses for the request's own bytes. `.bss` is now 75 504 bytes,
  22.09 % of DRAM, and the buffer is 4096 of it. Ten new host tests, and they
  were **proved to have teeth by mutation**: reading from offset 0 instead of
  the requested offset left two of the three content assertions green, because
  the first fill pattern repeated with period 256 and every offset under test
  was a multiple of 256 - the pattern now folds in the high byte of the index
  and the same mutation turns all three red. The host suite went from 84 tests
  to 94.
- **A core dump range on the USB command protocol, `0x07`.** `DUMP_INFO`
  `0x0701` (no payload, answers `[state:1][rsv:3][size:4]`), `DUMP_READ`
  `0x0702` (`[offset:4][len:4]`, `len` at most `PROTOCOL_DUMP_CHUNK_MAX` 4096)
  and `DUMP_ERASE` `0x0703`. The served set went from ten opcodes to thirteen.
  **No BEGIN and no END:** a read has no session to open, so a host may retry
  any chunk in any order and a transfer that dies half way costs nothing -
  upgrade needs a session because it mutates a slot, this only looks. A range
  of its own rather than three numbers in Get System because `0x0206` is a
  retired opcode the map deliberately resolves as absent, and reusing it would
  make an old tool asking for PRODUCT_ID reach the dump reader. The 4096 cap is
  not the upgrade chunk band: a dump is at most 64 KB and read once in a unit's
  life, so 16 round-trips cost nothing while a 32 KB answer would cost 32 KB of
  permanent `.bss` in the dispatcher.
- **`driver/coredump`, the mapped core dump driver.** `esp_core_dump_*` now
  lives in exactly one module. Four calls - `coredump_info_get`,
  `coredump_read`, `coredump_erase`, `coredump_reason_get` - behind
  `coredump_err_t`, so no `esp_err_t` reaches a header above the driver layer
  (R-LAY-03) and the second layer-boundary grep learned the new prefix.
  Unusually for this repo it has no `init`/`deinit` pair and no instance: there
  is no hardware to claim, the partition is found by subtype on every call, and
  that is what lets the boot-time panic report run before any module is up.
  A damaged dump is reported through `coredump_state_t`
  (`ABSENT` / `VALID` / `CORRUPT`) rather than as an error, and its bytes stay
  readable - a dump nobody can checksum is exactly the one worth looking at.
  With `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` off the component refuses with an
  `#error` naming the option, because a stub that always answered "no dump"
  would be a unit that silently never captures a panic.
- **`driver/ota`, the mapped OTA driver.** `esp_ota_*` and `esp_partition_*`
  now live in exactly one module. Eleven functions - `ota_session_begin` /
  `_write` / `_end` / `_abort`, `ota_slot_size_get`, `ota_slot_version_get`,
  `ota_running_slot_get`, `ota_running_version_get`, `ota_boot_slot_set`,
  `ota_pending_verify_is`, `ota_mark_valid` - behind `ota_err_t` and an opaque
  `ota_session_t`, so neither `esp_err_t` nor `esp_ota_handle_t` reaches a
  header above the driver layer (R-LAY-03).
- **`bsp_restart(grace_ms)` and `bsp_mac_get(kind)`** in `driver/bsp`. Both are
  instance-free, unlike the rest of that module: neither reads board data nor
  touches a pin, which is also what lets the boot banner print the MAC before
  `bsp_init()` has run.
- **`fw_crc32_le()`** in `middleware/fw`, a 16-entry nibble-table reflected
  CRC-32: 64 bytes of table, 128 bytes of image in total. Bit for bit what
  `esp_rom_crc32_le()` computed, chaining included, so every CRC already stored
  in flash still checks out.
- **`middleware/fw/include/fw_config.h`, compile-time feature switches.**
  `FW_FEATURE_USB_COMMAND` and `FW_FEATURE_UPDATER`, both defaulting to 1 and
  applied only at the wiring points in `application/app/src/app.c` - a module
  never tests its own switch. Measured, not estimated: turning the USB channel
  off frees **67 688 bytes of `.bss`** (20.66 % of DRAM down to 0.85 %) and
  41.4 KB of flash; turning the updater off leaves `updater_step` with no
  address at all in `app_updater.map`. `confirm_or_roll_back()` sits outside
  both on purpose, because a unit that cannot fetch updates must still confirm
  the image it was given.
- **First host tests for the blob store**, eleven of them, including the
  read-compare-write wear guard that had never been exercised: ten saves of
  identical bytes must reach flash once. The suite went from 69 tests to **83,
  all green**, and `test/host/fake/` now holds host implementations of our own
  driver contracts - `ota_fake.c`, `bsp_fake.c`, `nvs_fake.c` - which is where
  a test of middleware belongs.
- `docs/memory-ai/rule/layer-boundaries.md`: the rule below, its two named
  exceptions with the reason for each, and the two greps that check it.
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
- Releases now carry a merged factory image that provisions a blank board with
  one command at `0x0`, plus `ota_data_initial.bin` and the `sdkconfig` that
  produced it. (First shipped as `app-updater-<tag>-factory.bin`; renamed to
  `bl_<project>_<pid>_<MonDDYY>.bin` later in this same unreleased block.)

### Removed
- Seven vendor header stubs from `test/host/stub/`: `esp_fake.c`/`.h`,
  `esp_ota_ops.h`, `esp_partition.h`, `esp_app_desc.h`, `esp_mac.h`,
  `esp_system.h` and both `freertos/` headers. Once middleware stopped calling
  the SDK there was nothing left for them to stub. What remains is `esp_log.h`
  and an independent CRC-32 kept **on purpose** as the second opinion
  `fw_crc32_le()` is compared against - a single wrong nibble in the new table
  reddens that comparison and every protocol frame test with it, which is how
  the new CRC was proved rather than assumed.
- `command_slot_partition()` from `middleware/command`, `copy_field()` from
  `command.c`, and the `VERSION_FILE` constant from `tool-esp.py` - each made
  unused by the changes above.

## Released

Newest first. One file per version.

| Version | Date | Summary |
|---------|------|---------|
| [0.1.0](docs/CHANGELOG/v0.1.0.md) | 2026-09-06 | First scaffold, its CI and its memory bank. |

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/compare/v0.1.0...main
[0.1.0]: https://github.com/dtbao-embedded-dev/app-updater/releases/tag/v0.1.0
