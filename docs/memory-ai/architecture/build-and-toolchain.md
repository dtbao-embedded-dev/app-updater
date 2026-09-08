---
title: Build and Toolchain
category: architecture
order: 3
purpose: How the 0xF001 image is configured and compiled, including the main-less ESP-IDF build and the warning policy.
status: active
updated: 2026-09-08
source: workspace/0xF001/CMakeLists.txt, workspace/0xF001/sdkconfig.defaults, VERSION, .clang-format
confidence: confirmed
keywords: EXTRA_COMPONENT_DIRS, COMPONENTS, house_warnings, PROJECT_VER, CMAKE_CONFIGURE_DEPENDS, file(READ), sdkconfig.defaults, idf.py, Werror, esp32s3, fw_config.h, FW_FEATURE_USB_COMMAND, FW_FEATURE_UPDATER, feature switch
---

# Build and Toolchain

> One CMake entry in `workspace/0xF001/` pulls the three layer directories in, applies the house warning set to our targets only, and takes the version from the repo's single `VERSION` file.

🟢 **Verified by building it.** `idf.py build` completes inside
`espressif/idf:v6.1` — 1090 targets, `app_updater.bin` at 0x30380 bytes (198 KB)
against a 0x1E0000 slot, 90% free. The main-less shape, the component names in
`PRIV_REQUIRES`, `EXTRA_COMPONENT_DIRS` and the partition table are therefore
observed facts, not documented intentions.

## Toolchain

| Setting | Value |
|---------|-------|
| Target | ESP32-S3 |
| SDK | ESP-IDF **v6.1** (CI pins `espressif/idf:v6.1`) |
| Language | C11 (`-std=gnu11`) |
| Build system | CMake, ESP-IDF component model |
| Formatter | `clang-format`, config at repo root, requires v15+ |

## What the project CMakeLists does

In order, in `workspace/0xF001/CMakeLists.txt`:

1. Defines a CMake function `house_warnings(<target>)` **before** the IDF build
   runs, so every component of ours can call it on its own library target.
2. Sets `EXTRA_COMPONENT_DIRS` to the three layer directories, reached with
   relative paths up out of the workspace.
3. Reads the repo-root `VERSION` file and strips it into `PROJECT_VER`, before
   `project()` is called.
4. Includes the IDF project script and declares the project as `app_updater`.

A commented-out `set(COMPONENTS app)` line is the trim knob: leaving it off
builds every component found, which is slower but cannot fail on a missing
implicit dependency.

## The main-less build

ESP-IDF normally auto-adds a `main` component and makes every other component
its dependency. This repo has no `main`, because the house standard forbids any
source of ours under `workspace/`. `application/app/` provides `app_main()`
instead.

Consequences a rebuild must honour:

- `application/app/` must be reachable through `EXTRA_COMPONENT_DIRS`.
- Its dependencies are **not** inferred: every one is listed explicitly in its
  `idf_component_register` call.
- `MINIMAL_BUILD` cannot be used — that build property relies on a `main`
  component existing.

## Warning policy

`house_warnings()` applies, **per target and never globally**, so a vendor
warning we cannot fix cannot stop our build:

`-Wall -Wextra -Werror` plus `-Wshadow -Wconversion -Wsign-conversion
-Wdouble-promotion -Wswitch-enum -Wpointer-arith -Wcast-align
-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition`.

**`-Wundef` is missing from the target list on purpose**, and it is the one
place the house warning set cannot be applied whole. It is a *preprocessor*
warning: it fires where a header is included, not where the header lives, so a
per-target flag cannot keep it off the vendor SDK the way R-BLD-01 requires.
ESP-IDF's own `esp_log_config.h`, `esp_log_color.h` and `esp_log_timestamp.h`
test `BOOTLOADER_BUILD`, `CONFIG_LOG_COLORS_SUPPORT` and others without
defining them, so **every** file of ours that includes `esp_log.h` failed the
build. Observed in CI run 34031391115, then reproduced locally.

The check keeps its teeth where it can work: `test/host` still applies
`-Wundef`, and no SDK header is in scope there.

Two consequences visible in the source:

- `-Wswitch-enum` with no `default` label is what forces every `..._err_str()`
  and `updater_state_str()` to name a newly added enum value or fail the build.
- `-Wmissing-prototypes` is why `app_main()` is declared in
  `application/app/src/app_priv.h`: ESP-IDF publishes no prototype for it.

🟢 Verified independently: `middleware/fw/src/fw.c` and
`application/updater/src/updater.c` compile clean under this exact warning set
with `-Werror` on host GCC, through the `test/host/` harness, which applies the
same list on purpose — a host test must not be able to pass on code the target
build would reject. See [../rule/testing.md](../rule/testing.md).

## Configuration knobs

Every build knob is versioned in `workspace/0xF001/sdkconfig.defaults`, never in
a developer's environment:

| Knob | Value | Why |
|------|-------|-----|
| `CONFIG_IDF_TARGET` | `esp32s3` | The product's chip. |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` | on | Must change together with `partitions.csv`. |
| `CONFIG_PARTITION_TABLE_CUSTOM` + `..._FILENAME` | `partitions.csv` | Two OTA slots, no factory. |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240` | on | The part's maximum, over ESP-IDF's 160 MHz default: a TLS-over-Wi-Fi fetch is CPU bound, and a longer download has more chances to be interrupted. |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | on | A new image gets one boot to confirm itself. |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE` | on | Two app slots must fit in 16 MB. |
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE` | on | Asserts stay on in Release. |
| `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG` / `CONFIG_LOG_DEFAULT_LEVEL_INFO` | — | Level is a compile-time filter. |

**The generated `sdkconfig` lives in `build/`, not next to the defaults.**
`workspace/0xF001/CMakeLists.txt` sets `SDKCONFIG` to `${CMAKE_BINARY_DIR}/sdkconfig`
before including `project.cmake`. ESP-IDF reads `sdkconfig.defaults` only when
the generated file is absent, so left in the source tree it would outlive every
clean and silently ignore later edits to the defaults — the failure being a
partition table built for one flash size against a bootloader header carrying
another. Down in `build/` it dies with the build directory, and deleting that
directory is all a changed default needs.

The cost: `idf.py menuconfig` changes are throwaway, cleared by the next clean.
That is the intent — R-BLD-05 says a knob that matters belongs in
`sdkconfig.defaults` under version control, not in a developer's working tree.

## Version single source

The repo-root `VERSION` file holds `MAJOR.MINOR.PATCH` and nothing else. CMake
reads it into `PROJECT_VER`, which lands in the image header, so
`esp_app_get_description()->version` reports that file and cannot drift from it.
Nothing else in the repo restates the number except the README badge line and
the changelog headings. See
[../rule/versioning-and-release.md](../rule/versioning-and-release.md).

**And CMake is told to watch it.** `file(READ)` does not register a dependency
on what it reads, so reading `VERSION` that way is not enough on its own:

```cmake
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/../../VERSION")
```

Without that line a version bump changes nothing until something else forces a
reconfigure. The cache keeps the old `PROJECT_VER`, ninja has no reason to
re-run cmake, and the image reports the previous number while `VERSION` reads
the new one.

**Observed on 2026-09-08, not assumed:** after `0.1.0 -> 0.1.1`, a rebuild left
`esptool image-info` reporting `App version: 0.1.0`; deleting `CMakeCache.txt`
and rebuilding reported `0.1.1`. CI and the release workflow never saw it,
because a fresh checkout configures from scratch — which is what made it a
**local-only** trap: it ships nothing wrong and costs an afternoon. The
published `v0.1.1` image reports `0.1.1` correctly.

The fix was proved by replaying the failure: with the line in place, editing
`VERSION` alone triggers a reconfigure and the image follows, in both
directions.

## Reproduction notes

- The three CMake lines must stay in order: `cmake_minimum_required`, then the
  `set()` calls and function definition, then the IDF include, then `project()`.
- `PROJECT_VER` must be assigned **before** `project()` or the image header
  keeps the default.
- `house_warnings()` must be defined before the IDF include so it is in scope in
  every component subdirectory.

## Feature switches, and why they are not Kconfig

`middleware/fw/include/fw_config.h` holds `FW_FEATURE_USB_COMMAND` and
`FW_FEATURE_UPDATER`, both `1` by default. Setting one to `0` removes the
calls, so the linker drops the feature from the image; the components still
compile, which keeps a disabled feature from rotting while it is off.

ESP-IDF's own mechanism for this is Kconfig, and it was not used, for one
reason: a `CONFIG_*` symbol does not exist in the host test build, where
`sdkconfig.h` is never generated. `#if CONFIG_FEATURE_X` would then silently
evaluate to 0 in every host test — a switch that reads one way on target and
the other way under test is worse than no switch. A plain header reads
identically in both builds and is greppable in one place.

The cost accepted: no `menuconfig` entry, and a product-specific answer means a
header under `workspace/<pid>/` with that directory ahead of
`middleware/fw/include` on the include path. Worth revisiting only when a
second product actually disagrees.

## See also

- [ci-pipeline.md](ci-pipeline.md) — where this build runs unattended
- [repo-layout.md](repo-layout.md) — the tree this build compiles
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — the partition table this config selects
- [../interface/tool-esp-cli.md](../interface/tool-esp-cli.md) — the wrapper that runs this build
