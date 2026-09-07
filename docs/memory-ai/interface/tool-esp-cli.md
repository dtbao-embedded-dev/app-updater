---
title: Developer CLI (tool-esp.py)
category: interface
order: 6
purpose: The command surface of the repo's single developer entry point, what each command expands to, and how it picks the ESP-IDF checkout and the product workspace.
status: active
updated: 2026-09-07
source: docs/scripts/tool-esp.py, .env.esp
confidence: confirmed
keywords: tool-esp.py, .env.esp, IDF_PATH, WORKSPACE, IDF_PYTHON_ENV_PATH, MSYSTEM, build, flash, monitor, erase-flash, size, clean, fullclean, menuconfig, merge, format, test, analyse, --port, --workspace, -w, --check, UNITY_DIR, port detection, workspace resolution, resolve_workspace, workspace_refusal, ENV_TEMPLATE, env_value
---

# Developer CLI (tool-esp.py)

> Every developer command in one place: each is `idf.py` or `clang-format` with the parts nobody remembers already filled in.

## Contract

Invoked as `python docs/scripts/tool-esp.py [command] [flags]`.

**With no command it builds, flashes and monitors**, in one `idf.py`
invocation, with the port detected.

| Command | Expands to | Notes |
|---------|-----------|-------|
| `build` | `idf.py -C workspace/<ws> build` | |
| `flash` | `idf.py -C … flash monitor` | Deliberately fused: the boot log is what says whether it worked |
| `monitor` | `idf.py -C … monitor` | |
| `erase-flash` | `idf.py -C … erase-flash` | Whole chip. Port detected like the others |
| `erase-flash --address A --size N` | `esptool --port … erase-region A N` | One region. Goes through esptool: idf.py has erase-flash and erase-otadata and nothing in between |
| `size` | `idf.py -C … size size-components` | Fused so the per-component breakdown is never skipped |
| `clean` | `idf.py -C … clean` | |
| `menuconfig` | `idf.py -C … menuconfig` | |
| *(none)* | `idf.py build flash monitor` with the detected port | The default: the whole loop, one command |
| `format` | `clang-format -i` over our sources | With `--check`: `--dry-run --Werror` instead |
| `test` | Configure + build `test/host`, then `ctest` | No board needed; see the note below |
| `merge` | `idf.py merge-bin -o app-updater-v<VERSION>-factory.bin` | One image at `0x0`; the name comes from `VERSION` |
| `analyse` | `cppcheck` over our `.c`, `clang-tidy` over the host compile database | Configures the database itself when absent |

| Flag | Applies to | Meaning |
|------|-----------|---------|
| `-p` / `--port` | `flash`, `monitor`, `erase-flash`, no command | Serial port, e.g. `COM7`. Omit and it is detected. |
| `-w` / `--workspace` | every command that reaches the build, no command | Product workspace by directory name, e.g. `0xF001`. Omit and it comes from `WORKSPACE` in `.env.esp`; with neither, the run refuses. Rejected for `format`, `test`, `analyse` |
| `--address` / `--size` | `erase-flash` | Region to erase, decimal or `0x` hex. Both or neither; both must be multiples of `0x1000`. `--size all` means “to the end of the flash” |
| `--check` | `format` | Report instead of rewriting |
| `--sanitize` | `test` | ASan + UBSan, into a separate `build/san` tree |

**`erase-flash` detects the port like every other port command, and it is
destructive.** With no `-p` it erases whichever single board is plugged in,
with no confirmation step, and nothing puts back the nvs and otadata it takes.
Name the port when more than a bench with one board is involved.

`--address`/`--size` narrow it to a region — `--address 0x19000 --size 0x4000`
clears `cfg_setting` and leaves the rest alone. Both must be sector multiples;
an unaligned region is refused here rather than in esptool, because a region
that starts or ends mid-sector would take a neighbouring partition with it.

`--size all` erases from `--address` to the end of the flash, which is read
back from `CONFIG_ESPTOOLPY_FLASHSIZE_*MB` in `sdkconfig.defaults` rather than
written into the script — the same knob the partition table is checked
against, so the two cannot drift. `--address 0x220000 --size all` clears
`app_firmware` and nothing before it. An address at or past the end of the
configured flash is refused, naming the size it was measured against.

**A flag that does not apply is rejected, not ignored.** `format -p COM7` and
`build --check` both exit 2 with a message naming what the flag is for. A flag
that silently does nothing is one somebody will believe in.

## Finding ESP-IDF

`.env.esp` at the repo root holds the two per-machine answers,
`IDF_PATH=<checkout>` and `WORKSPACE=<product>`. It is gitignored — committing
it would point everyone else at a path that is not theirs. The parser is six
lines of `KEY=VALUE` splitting, not a dotenv library: a dependency to read two
settings is a dependency to install before the first build.

| Situation | What happens |
|-----------|--------------|
| No `.env.esp` | It is written from a commented template, and the run stops so it can be filled in. The which-product refusal writes the same template for the same reason |
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

## Choosing the product workspace

The workspace is **not** a constant in the script. A product workspace is any
directory under `workspace/` that holds a `CMakeLists.txt`, and that directory
listing *is* the list of products — a second copy in `.env.esp` would be a
second thing to edit when a product is added, and the one that goes stale.
**Which one to act on is required**, from `-w` or from `.env.esp`.

Resolution, first answer wins:

| Source | When it applies |
|--------|-----------------|
| `-w NAME` | Named on the command line; overrides `.env.esp` for that one run |
| `WORKSPACE=` in `.env.esp` | The answer for this machine, filled in once |
| — | Neither: **refuses**, listing the workspaces on disk |

**There is no default, and one workspace in the repo is not one either.** An
earlier version inferred it when the repo held exactly one — which made the
answer optional on a developer's machine and mandatory in CI, the wrong way
round: a product nobody chose is a product nobody checked, and a build that
succeeded against the wrong one looks exactly like one that succeeded. The
process environment is deliberately not a third source, because Jenkins and
friends set `WORKSPACE` to the job directory.

```
$ ls workspace/
0xF001/   0xF002/

$ python docs/scripts/tool-esp.py build

Nothing says which product to build. Put one of these in .env.esp
as WORKSPACE=, or pass -w NAME:
  0xF001
  0xF002
```

The fix is one line in `.env.esp` — `WORKSPACE=0xF001` — and `build` stops
asking. **CI and the release workflow pass `-w 0xF001` on every build step**,
since a runner has no `.env.esp`; that flag is also the only place those
workflows say which product they build. **The `.env.esp` template carries that whole exchange as a comment**,
so the file that has to be filled in is also the file that shows why and with
what; a developer meeting the refusal does not have to find this document.

**The template does not quote that refusal, it interpolates it.**
`workspace_refusal(names)` formats it once; `resolve_workspace()` calls it with
the workspaces it found, and `ENV_TEMPLATE` calls it with a `["0xF001",
"0xF002"]` example indented to comment depth by `textwrap.indent`. Rewording the
message therefore rewords the comment, which a second copy of the sentence would
not have done — and nothing would have gone red to say the file was quoting a
sentence the script no longer prints.

**The refusal creates `.env.esp` when it is missing**, from the same template
`idf_root()` uses, and says so. On a fresh clone this refusal runs before
anything has asked about ESP-IDF, so without that it would send a developer to
edit a line in a file that does not exist yet. `WORKSPACE=` is deliberately the
last line of the template, which is what the message can then point at.

A name that is not in that listing is refused, naming where it came from
(`-w`, or `WORKSPACE in .env.esp`) and what the real names are. The check is
membership in the listing rather than "does the path exist", which is also what
stops a `WORKSPACE=../../somewhere` from pointing `idf.py` outside the repo.

**Resolution is lazy: only the commands that reach the build ask for it.**
`format`, `test` and `analyse` cover the whole repo, so on a machine with
several workspaces they must not open by demanding to be told which one — and
`-w` is rejected for them outright.

## Behaviour worth knowing

- **The screen is cleared on every run**, but only when stdout is a terminal.
  In CI the escape codes would be noise in a log nobody can scroll back.
- **The environment is resolved before the port is.** The other order was
  written first and was wrong: a machine with no ESP-IDF installed and no board
  plugged in reported "No serial port found", which is true and useless. The
  port is never the interesting failure.

- **It resolves the repo root from its own file location**, then points every
  `idf.py` at the workspace it was told to use. The repo root has no `CMakeLists.txt`, so
  `idf.py build` typed at the root finds no project — that `-C` is the reason
  this wrapper exists. A second product needs no change here, only a directory
  under `workspace/`.
- **The echoed command starts at the `-C`**, so the log line says which product
  was just built or flashed. With the workspace no longer a constant in the
  file, that is the only place a reader can see it.
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
