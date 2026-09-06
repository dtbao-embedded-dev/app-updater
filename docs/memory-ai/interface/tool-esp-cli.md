---
title: Developer CLI (tool-esp.py)
category: interface
order: 6
purpose: The command surface of the repo's single developer entry point and what each command expands to.
status: active
updated: 2026-09-06
source: docs/scripts/tool-esp.py, .env.esp
confidence: confirmed
keywords: tool-esp.py, .env.esp, IDF_PATH, IDF_PYTHON_ENV_PATH, MSYSTEM, build, flash, monitor, size, clean, menuconfig, format, test, analyse, --port, --check, UNITY_DIR, port detection
---

# Developer CLI (tool-esp.py)

> Every developer command in one place: each is `idf.py` or `clang-format` with the parts nobody remembers already filled in.

## Contract

Invoked as `python docs/scripts/tool-esp.py [command] [flags]`.

**With no command it builds, flashes and monitors**, in one `idf.py`
invocation, with the port detected.

| Command | Expands to | Notes |
|---------|-----------|-------|
| `build` | `idf.py -C workspace/0xF001 build` | |
| `flash` | `idf.py -C … flash monitor` | Deliberately fused: the boot log is what says whether it worked |
| `monitor` | `idf.py -C … monitor` | |
| `size` | `idf.py -C … size size-components` | Fused so the per-component breakdown is never skipped |
| `clean` | `idf.py -C … clean` | |
| `menuconfig` | `idf.py -C … menuconfig` | |
| *(none)* | `idf.py build flash monitor` with the detected port | The default: the whole loop, one command |
| `format` | `clang-format -i` over our sources | With `--check`: `--dry-run --Werror` instead |
| `test` | Configure + build `test/host`, then `ctest` | No board needed; see the note below |
| `analyse` | `cppcheck` over our `.c`, `clang-tidy` over the host compile database | Needs `test/host` configured first |

| Flag | Applies to | Meaning |
|------|-----------|---------|
| `-p` / `--port` | `flash`, `monitor`, no command | Serial port, e.g. `COM7`. Omit and it is detected. |
| `--check` | `format` | Report instead of rewriting |

**A flag that does not apply is rejected, not ignored.** `format -p COM7` and
`build --check` both exit 2 with a message naming what the flag is for. A flag
that silently does nothing is one somebody will believe in.

## Finding ESP-IDF

`.env.esp` at the repo root holds `IDF_PATH=<checkout>`. It is per-machine and
gitignored — committing it would point everyone else at a path that is not
theirs.

| Situation | What happens |
|-----------|--------------|
| No `.env.esp` | It is written from a commented template, and the run stops so it can be filled in |
| `IDF_PATH=` still empty | Refuses, naming the file |
| Path has no `export.bat`/`export.sh` | Refuses, saying it is not an ESP-IDF checkout |
| Checkout present but tools never installed | Refuses, **quoting the export script's own error**, and naming `install.bat esp32s3` |
| `IDF_PATH` already exported in the shell | Used as-is, no re-export |

Three things this had to get right, each found by it going wrong:

- **Setting `IDF_PATH` is not exporting ESP-IDF.** The export puts the cross
  compiler, the venv, cmake and ninja on `PATH`. So the export script is run in
  a subshell and the environment it produces is captured whole.
- **`export.bat` refuses to run when `MSYSTEM` is set**, and Git Bash always
  sets it. The variable is inherited into `cmd`, so it is dropped for that call.
- **Windows resolves an executable against the parent process `PATH`**, not the
  `env=` handed to the child. Bare `idf.py` is therefore never found, however
  correct the exported `PATH` is; it is invoked through the interpreter from
  ESP-IDF's own virtual environment instead.

The guard is `IDF_PYTHON_ENV_PATH`, not `IDF_PATH`: `export.bat` sets the latter
from its own location before doing any work, so a checkout with no tools
installed still reports one.

## Behaviour worth knowing

- **The screen is cleared on every run**, but only when stdout is a terminal.
  In CI the escape codes would be noise in a log nobody can scroll back.
- **The environment is resolved before the port is.** The other order was
  written first and was wrong: a machine with no ESP-IDF installed and no board
  plugged in reported "No serial port found", which is true and useless. The
  port is never the interesting failure.

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
