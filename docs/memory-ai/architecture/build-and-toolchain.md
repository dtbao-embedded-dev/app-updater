---
title: Build and Toolchain
category: architecture
order: 3
purpose: How the 0xF001 image is configured and compiled, including the main-less ESP-IDF build and the warning policy.
status: active
updated: 2026-09-06
source: workspace/0xF001/CMakeLists.txt, workspace/0xF001/sdkconfig.defaults, VERSION, .clang-format
confidence: inferred
keywords: EXTRA_COMPONENT_DIRS, COMPONENTS, house_warnings, PROJECT_VER, sdkconfig.defaults, idf.py, Werror, esp32s3
---

# Build and Toolchain

> One CMake entry in `workspace/0xF001/` pulls the three layer directories in, applies the house warning set to our targets only, and takes the version from the repo's single `VERSION` file.

🟡 **inferred:** the component names in `PRIV_REQUIRES` and the main-less build
shape below have **never been compiled against ESP-IDF 6.x** in this repo. They
follow the documented mechanism but are unverified. Treat the first successful
`build` as the confirmation, and flip this doc to `confirmed` then.

## Toolchain

| Setting | Value |
|---------|-------|
| Target | ESP32-S3 |
| SDK | ESP-IDF 6.x |
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

`-Wall -Wextra -Werror` plus `-Wshadow -Wconversion -Wsign-conversion -Wundef
-Wdouble-promotion -Wswitch-enum -Wpointer-arith -Wcast-align
-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition`.

Two consequences visible in the source:

- `-Wswitch-enum` with no `default` label is what forces every `..._err_str()`
  and `updater_state_str()` to name a newly added enum value or fail the build.
- `-Wmissing-prototypes` is why `app_main()` is declared in
  `application/app/src/app_priv.h`: ESP-IDF publishes no prototype for it.

🟢 Verified independently: `middleware/fw/src/fw.c` and
`application/updater/src/updater.c` compile clean under this exact warning set
with `-Werror` on host GCC 15 (with `esp_log.h` stubbed).

## Configuration knobs

Every build knob is versioned in `workspace/0xF001/sdkconfig.defaults`, never in
a developer's environment:

| Knob | Value | Why |
|------|-------|-----|
| `CONFIG_IDF_TARGET` | `esp32s3` | The product's chip. |
| `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` | on | Must change together with `partitions.csv`. |
| `CONFIG_PARTITION_TABLE_CUSTOM` + `..._FILENAME` | `partitions.csv` | Two OTA slots, no factory. |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | on | A new image gets one boot to confirm itself. |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE` | on | Two app slots must fit in 4 MB. |
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE` | on | Asserts stay on in Release. |
| `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG` / `CONFIG_LOG_DEFAULT_LEVEL_INFO` | — | Level is a compile-time filter. |

## Version single source

The repo-root `VERSION` file holds `MAJOR.MINOR.PATCH` and nothing else. CMake
reads it into `PROJECT_VER`, which lands in the image header, so
`esp_app_get_description()->version` reports that file and cannot drift from it.
Nothing else in the repo restates the number except the README badge line and
the changelog headings. See
[../rule/versioning-and-release.md](../rule/versioning-and-release.md).

## Reproduction notes

- The three CMake lines must stay in order: `cmake_minimum_required`, then the
  `set()` calls and function definition, then the IDF include, then `project()`.
- `PROJECT_VER` must be assigned **before** `project()` or the image header
  keeps the default.
- `house_warnings()` must be defined before the IDF include so it is in scope in
  every component subdirectory.

## See also

- [repo-layout.md](repo-layout.md) — the tree this build compiles
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — the partition table this config selects
- [../interface/tool-esp-cli.md](../interface/tool-esp-cli.md) — the wrapper that runs this build
