---
title: Developer CLI (tool-esp.py)
category: interface
order: 6
purpose: The command surface of the repo's single developer entry point and what each command expands to.
status: active
updated: 2026-09-06
source: docs/scripts/tool-esp.py
confidence: confirmed
keywords: tool-esp.py, build, flash, monitor, size, clean, menuconfig, format, test, analyse, --port, --check, IDF_PATH, UNITY_DIR
---

# Developer CLI (tool-esp.py)

> Every developer command in one place: each is `idf.py` or `clang-format` with the parts nobody remembers already filled in.

## Contract

Invoked as `python docs/scripts/tool-esp.py <command> [flags]`.

| Command | Expands to | Notes |
|---------|-----------|-------|
| `build` | `idf.py -C workspace/0xF001 build` | |
| `flash` | `idf.py -C … flash monitor` | Deliberately fused: the boot log is what says whether it worked |
| `monitor` | `idf.py -C … monitor` | |
| `size` | `idf.py -C … size size-components` | Fused so the per-component breakdown is never skipped |
| `clean` | `idf.py -C … clean` | |
| `menuconfig` | `idf.py -C … menuconfig` | |
| `format` | `clang-format -i` over our sources | With `--check`: `--dry-run --Werror` instead |
| `test` | Configure + build `test/host`, then `ctest` | No board needed; see the note below |
| `analyse` | `cppcheck` over our `.c`, `clang-tidy` over the host compile database | Needs `test/host` configured first |

| Flag | Applies to | Meaning |
|------|-----------|---------|
| `-p` / `--port` | the `idf.py` commands | Serial port, e.g. `COM7` or `/dev/ttyUSB0`. Omit and esptool picks one. |
| `--check` | `format` | Report instead of rewriting |

## Behaviour worth knowing

- **It resolves the repo root from its own file location**, then points every
  `idf.py` at `workspace/0xF001`. The repo root has no `CMakeLists.txt`, so
  `idf.py build` typed at the root finds no project — that `-C` is the reason
  this wrapper exists. A second product means changing that one constant.
- If `IDF_PATH` is unset it exits immediately with a readable message rather
  than letting the failure surface deep inside CMake.
- If `clang-format` is not on PATH, `format` exits with a message.
- The file list for `format` is `application/`, `middleware/`, `driver/` only —
  never the SDK, never `build/`. **The pre-commit hook applies the same rule**,
  so a local format and the hook can never disagree.
- `test` configures into `build/host` and **asks for the Ninja generator on the
  first configure when `ninja` is on PATH**. On Windows the CMake default is
  MSVC, which rejects the firmware's warning flags outright. CMake will not
  change the generator of an existing tree, so a stale `build/host` must be
  deleted rather than reconfigured.
- Unity is located by the harness, not by this script: from `$IDF_PATH`, or from
  a `UNITY_DIR` passed once at configure time and then cached.
- `analyse` skips a tool that is not installed and says so, but **fails** when
  the compile database is missing — a silent partial analysis would read as a
  clean one.
- Exit status is whatever the underlying tool returned.

## Contract rules

- The ESP-IDF environment must already be exported; this script does not do it.
- The script is a CLI entry point, never imported. Its filename contains a
  hyphen, so it is not importable as a Python module.
- New developer commands belong here, not in README prose: a command with flags
  nobody remembers belongs in a script with a name.

## See also

- [../rule/formatting-and-hooks.md](../rule/formatting-and-hooks.md) — the format rule and the hook
- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — what the build actually does
