#!/usr/bin/env python3
"""Developer commands for App Updater (R-RPO-06).

One entry point rather than six scripts: every ESP-IDF command is idf.py with
the workspace, the environment and the serial port already filled in, which is
the part nobody remembers.

    python docs/scripts/tool-esp.py              build + flash + monitor, port found for you
    python docs/scripts/tool-esp.py build
    python docs/scripts/tool-esp.py flash [-p COM7]
    python docs/scripts/tool-esp.py monitor [-p COM7]
    python docs/scripts/tool-esp.py erase-flash [-p COM7]
    python docs/scripts/tool-esp.py erase-flash --address 0x19000 --size 0x4000
    python docs/scripts/tool-esp.py erase-flash --address 0x220000 --size all
    python docs/scripts/tool-esp.py size
    python docs/scripts/tool-esp.py merge                one image, flashed at 0x0
    python docs/scripts/tool-esp.py clean
    python docs/scripts/tool-esp.py menuconfig
    python docs/scripts/tool-esp.py format [--check]
    python docs/scripts/tool-esp.py test [--sanitize]
    python docs/scripts/tool-esp.py analyse

Every command that reaches the build needs the product workspace it acts on, a
directory under workspace/, and never infers it - not even when the repo holds
exactly one. `WORKSPACE=` in .env.esp says which, once per machine; `-w NAME`
overrides that for a single run, which is how CI names the product it builds.

CI and the release workflow call these same commands, so the workspace lookup,
the fused `size size-components`, and the factory image name live in exactly
one place. A second copy in a workflow is one that drifts while staying green.

ESP-IDF and the workspace both come from `.env.esp` at the repo root, which
holds the path to the checkout that has export.bat / export.sh and the product
to build. The file is per-machine and gitignored; the first run writes a
template and stops so it can be filled in.

Python 3 so one set of commands runs on Windows and Linux without a shell port.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = REPO / "workspace"
SOURCE_DIRS = ("application", "middleware", "driver")
ENV_FILE = REPO / ".env.esp"

# The product workspace this run acts on, resolved in main() and only for the
# commands that reach the build. format/test/analyse have nothing to do with a
# workspace, so on a repo with several of them they must not start by demanding
# to be told which one.
WORKSPACE: Path | None = None

# Commands that talk to a board, so a port is meaningful. Everything else
# rejects -p rather than accepting it and quietly doing nothing with it.
PORT_COMMANDS = ("flash", "monitor", "erase-flash")
# fullclean, not just clean: `clean` keeps the CMake cache, so a new component,
# a new managed dependency or an edited sdkconfig.defaults is silently ignored -
# the build reuses the configuration it already has. Deleting the build tree is
# what the workspace CMakeLists says to do, and this is that command.
IDF_COMMANDS = ("build", "flash", "monitor", "size", "clean", "fullclean", "menuconfig",
                "merge", "erase-flash")

# Flash erases a sector at a time; a partial erase has to line up with one.
SECTOR_SIZE = 0x1000

# strftime("%b") follows LC_TIME, so on a machine with a non-English locale it
# would spell September something else and the artifact name would depend on
# whose laptop built it. A fixed table is what makes the name the same
# everywhere, CI included.
MONTH_ABBR = ("Jan", "Feb", "Mar", "Apr", "May", "Jun",
              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


def cmake_project_name(workspace: Path) -> str:
    """The project() token from a workspace's CMakeLists, which names its image.

    Read rather than restated: ESP-IDF derives app_updater.bin/.elf/.map from
    that one token, so a copy here would be a second answer able to disagree
    with the first.
    """
    text = (workspace / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"^\s*project\(([A-Za-z0-9_.+-]+)", text, re.MULTILINE)
    if not found:
        sys.exit(f"\n{workspace / 'CMakeLists.txt'} has no "
                 f"project(<name>) call, so the image name cannot be "
                 f"derived from it.\n")
    return found.group(1)


def factory_image_name(project: str, pid: str, when: datetime.datetime) -> str:
    """bl_<project>_<pid>_<MonDDYY>.bin - the one image that flashes a blank board.

    `bl_` because it starts at the bootloader: this is the merged image written
    at offset 0, not the OTA payload. The product id is in the name because two
    products build two different images and a downloads folder is where they
    meet. The date is what tells two builds of the same version apart, which a
    version alone cannot - a bench sees several a day.

    ponytail: date only, no time of day, so two builds on the same day produce
    the same name and the second overwrites the first without a word. Add
    -%H%M to `stamp` if that starts costing anyone an afternoon.
    """
    stamp = f"{MONTH_ABBR[when.month - 1]}{when.day:02d}{when.year % 100:02d}"
    return f"bl_{project}_{pid}_{stamp}.bin"


def workspace_refusal(names: list[str]) -> str:
    """The nothing-chose-a-product refusal, listing the workspaces on disk.

    Called twice: by resolve_workspace() for real, and once below with a
    two-product example that ENV_TEMPLATE quotes. So the comment telling a
    developer to fill WORKSPACE= in cannot end up quoting a sentence this script
    no longer prints - rewording it here rewords both.

    Says nothing about how many were found, so the one-workspace repo and the
    six-workspace one get the same sentence.
    """
    listing = "\n".join(f"  {n}" for n in names)
    return (f"Nothing says which product to build. Put one of these in "
            f"{ENV_FILE.name}\nas WORKSPACE=, or pass -w NAME:\n{listing}")


# Commented to the depth the template indents its shell transcript to.
WORKSPACE_EXAMPLE = textwrap.indent(workspace_refusal(["0xF001", "0xF002"]),
                                    "#   ")

ENV_TEMPLATE = f"""# What THIS machine needs to build: where ESP-IDF lives, and which product.
#
# Per-machine, so this file is gitignored - committing it would point everyone
# else at a path that does not exist for them.
#
# Set IDF_PATH to the directory that holds export.bat / export.sh:
#
#   Windows   IDF_PATH=E:\\.espressif\\v6.1\\esp-idf
#   Linux     IDF_PATH=/home/you/esp/v6.1/esp-idf
#
IDF_PATH=

# Which product workspace commands act on: a directory name under workspace/.
#
# REQUIRED. Nothing is inferred, not even when the repo holds exactly one
# workspace: a product nobody chose is a product nobody checked, and a build
# that succeeded against the wrong one looks exactly like one that succeeded.
#
#   $ ls workspace/
#   0xF001/   0xF002/
#
#   $ python docs/scripts/tool-esp.py build
#
{WORKSPACE_EXAMPLE}
#
# So fill the line in, once per machine:
#
#   WORKSPACE=0xF001
#
# `-w 0xF002` overrides it for a single run - the day you touch the other
# product without wanting to edit this file. CI and the release workflow pass
# -w on every build step, because a runner has no .env.esp to read.
#
WORKSPACE=
"""


# --------------------------------------------------- .env.esp ---

def env_value(key: str) -> str:
    """One KEY= value from .env.esp, or '' when the file or the key is absent.

    Deliberately not a dotenv parser: the file is a handful of KEY=VALUE lines a
    developer edits by hand, and a dependency to read six of them is a
    dependency to install before the first build.
    """
    if not ENV_FILE.exists():
        return ""
    for line in ENV_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#") or "=" not in line:
            continue
        name, _, value = line.partition("=")
        if name.strip() == key:
            return value.strip().strip('"').strip("'")
    return ""


# ------------------------------------------------- finding ESP-IDF ---

def write_env_template() -> bool:
    """Put the template on disk. True when it was this call that wrote it.

    Both the missing-IDF_PATH and the which-product refusals send a developer to
    a line in this file, so both have to be sure the file is there to open.
    """
    if ENV_FILE.exists():
        return False
    ENV_FILE.write_text(ENV_TEMPLATE, encoding="utf-8")
    return True


def idf_root() -> Path:
    """Locate ESP-IDF, writing the template and stopping on the first run."""
    raw = env_value("IDF_PATH")

    if write_env_template():
        sys.exit(
            f"\nCreated {ENV_FILE.name}. Open it, paste the path to your ESP-IDF\n"
            f"checkout after IDF_PATH=, then run this command again.\n\n"
            f"  {ENV_FILE}\n")

    if not raw:
        sys.exit(f"\n{ENV_FILE.name} has no IDF_PATH yet. Paste the path to the\n"
                 f"ESP-IDF checkout after IDF_PATH= and run this again.\n\n"
                 f"  {ENV_FILE}\n")

    root = Path(raw)
    export = root / ("export.bat" if os.name == "nt" else "export.sh")
    if not export.exists():
        sys.exit(f"\n{ENV_FILE.name} points at {root}\nbut there is no "
                 f"{export.name} there. That is not an ESP-IDF checkout.\n")
    return root


def idf_env() -> dict[str, str]:
    """The environment idf.py needs.

    This is for cross-compiling and nothing else. The host test build must not
    use it - see run_tests().

    Exporting ESP-IDF does far more than set IDF_PATH: it puts the cross
    compiler, the python venv, cmake and ninja on PATH. Setting one variable
    would leave idf.py unrunnable, so the export script is run in a subshell and
    the environment it produces is captured whole.

    A shell that already exported ESP-IDF is used as-is, which is both faster
    and what a developer who ran export.sh once expects.
    """
    if os.environ.get("IDF_PATH"):
        return dict(os.environ)

    root = idf_root()
    print(f"exporting ESP-IDF from {root} ...", flush=True)

    parent = dict(os.environ)
    if os.name == "nt":
        # export.bat refuses to run when MSYSTEM is defined, and Git Bash always
        # defines it. The variable is inherited all the way down into cmd, so a
        # developer working from Git Bash gets a silent no-op unless it is
        # dropped here. Nothing in the ESP-IDF export needs it.
        parent.pop("MSYSTEM", None)
        # Path builds the separator, so no backslash escaping in the f-string.
        # stdout is redirected because `set` has to be the only thing on it.
        # stderr is deliberately NOT: that is where the export script explains
        # why it gave up, and swallowing it turns a one-line diagnosis into an
        # afternoon. `&` not `&&` so `set` runs whatever the export returned.
        cmd = ["cmd", "/c", f'call {root / "export.bat"} >nul & set']
    else:
        cmd = ["bash", "-c", f'. "{root / "export.sh"}" >/dev/null; env']

    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                          env=parent)
    env = dict(parent)
    for line in proc.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value

    # IDF_PATH alone proves nothing: export.bat sets it from its own location
    # before doing any of the work, so a checkout whose tools were never
    # installed still reports one. Whether idf.py can actually run is the test.
    # IDF_PYTHON_ENV_PATH is the honest signal: only a completed export defines
    # it, because only then does the virtual environment it names exist. A
    # generic python on PATH proves nothing - this machine has one, and idf.py
    # still died on a missing esp_idf_monitor.
    entry = Path(env.get("IDF_PATH", root)) / "tools" / "idf.py"
    if not entry.exists() or not env.get("IDF_PYTHON_ENV_PATH"):
        detail = proc.stderr.strip()
        installer = root / ("install.bat" if os.name == "nt" else "install.sh")
        sys.exit(f"\nESP-IDF at {root} is not usable.\n\n"
                 + (f"The export script said:\n\n{detail}\n\n" if detail else "")
                 + f"A checkout is not enough; its tools are installed once:\n"
                   f"  {installer} esp32s3\n")
    return env


def clear_screen() -> None:
    """Start each run on a clean screen.

    Only when attached to a terminal: in CI the escape codes are noise in a log
    nobody can scroll back through anyway.
    """
    if sys.stdout.isatty():
        os.system("cls" if os.name == "nt" else "clear")


# --------------------------------------------------- serial port ---

def detect_port() -> str:
    """The one serial port on this machine, or a refusal that names the choices.

    Guessing between two boards is how firmware ends up on the wrong one, so
    more than one candidate is an error rather than a coin flip.
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        print("pyserial not available - letting esptool pick the port")
        return ""

    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        sys.exit("\nNo serial port found. Plug the board in, or pass -p PORT.\n")
    if len(ports) > 1:
        listing = "\n".join(f"  {p.device}  {p.description}" for p in ports)
        sys.exit(f"\n{len(ports)} serial ports - say which one with -p PORT:\n"
                 f"{listing}\n")

    print(f"port: {ports[0].device}  ({ports[0].description})")
    return ports[0].device


# ------------------------------------------- product workspace ---

def workspaces() -> list[Path]:
    """Every product workspace: a directory under workspace/ with a CMakeLists.

    The directory listing is the list. A second copy in .env.esp would be a
    second thing to edit when a product is added, and the one that goes stale.
    """
    if not WORKSPACE_ROOT.is_dir():
        return []
    return sorted(d for d in WORKSPACE_ROOT.iterdir()
                  if (d / "CMakeLists.txt").is_file())


def resolve_workspace(named: str | None) -> Path:
    """Which product to act on: -w, else WORKSPACE in .env.esp. No default.

    Nothing is inferred, not even when the repo holds exactly one workspace.
    That inference was written first and was wrong: it made the answer optional
    on the machine where a mistake is cheap and mandatory on the machine where
    it is not, and a product nobody chose is a product nobody checked. The whole
    cost of requiring it is one line per machine and an explicit -w in CI, both
    of which then say out loud what they build.

    The process environment is deliberately not a third source: Jenkins and
    friends set WORKSPACE to the job directory, and inheriting that would pick a
    product from a variable nobody wrote for us.
    """
    found = workspaces()
    if not found:
        sys.exit(f"\nNo product workspace under {WORKSPACE_ROOT}.\nOne is a "
                 f"directory there holding a CMakeLists.txt.\n")

    choice = named or env_value("WORKSPACE")
    if not choice:
        # The file has to exist before being told to edit a line in it. On a
        # fresh clone this refusal is the first thing that runs, before anything
        # has asked about ESP-IDF.
        created = ("\nThat file did not exist; it has been created from the "
                   "template, and the\nline to fill in is the last one.\n"
                   if write_env_template() else "")
        sys.exit(f"\n{workspace_refusal([d.name for d in found])}\n{created}")

    # Membership in the listing rather than a bare existence probe: that is what
    # stops a WORKSPACE=../../somewhere from pointing idf.py outside the repo.
    picked = WORKSPACE_ROOT / choice
    if picked not in found:
        source = "-w" if named else f"WORKSPACE in {ENV_FILE.name}"
        listing = "\n".join(f"  {d.name}" for d in found)
        sys.exit(f"\n{source} names `{choice}`, which is not a product "
                 f"workspace here. What is:\n{listing}\n")
    return picked


# ------------------------------------------------------ commands ---

def configured_flash_bytes() -> int:
    """Total flash, from the one knob that already decides it.

    `--size all` has to know where the chip ends, and R-BLD-05 says that number
    lives in sdkconfig.defaults next to the partition table it is checked
    against. Reading it back is how this stays one number rather than two that
    agree until someone edits one.
    """
    text = (WORKSPACE / "sdkconfig.defaults").read_text(encoding="utf-8")
    found = re.search(r"^CONFIG_ESPTOOLPY_FLASHSIZE_(\d+)MB=y", text, re.M)
    if not found:
        sys.exit(f"{WORKSPACE / 'sdkconfig.defaults'} sets no "
                 f"CONFIG_ESPTOOLPY_FLASHSIZE_*MB, so `--size all` has no end "
                 f"to erase up to.")
    return int(found.group(1)) * 1024 * 1024


def venv_python(env: dict[str, str]) -> str:
    """The interpreter from ESP-IDF's own virtual environment.

    Windows resolves an executable against the PARENT process PATH, not the
    env= handed to the child, so bare `idf.py` is not found however correct the
    exported PATH is. Going through the exported interpreter sidesteps both that
    and the .py / PATHEXT association. It has to be ESP-IDF's own environment: a
    system python cannot import esp_idf_monitor or esptool. idf_env() has
    already refused if that environment is missing.
    """
    venv_bin = Path(env["IDF_PYTHON_ENV_PATH"]) / ("Scripts" if os.name == "nt" else "bin")
    return (shutil.which("python", path=str(venv_bin))
            or shutil.which("python", path=env["PATH"])
            or sys.executable)


def esptool(args: list[str], port: str | None, detect: bool = False) -> int:
    """Run esptool directly, for the one thing idf.py cannot do.

    idf.py offers erase-flash and erase-otadata and nothing in between, so a
    partial erase has no route through it. The chip is left to esptool's own
    detection rather than named here: CONFIG_IDF_TARGET in sdkconfig.defaults
    is the single source of truth and this must not become a second one.
    """
    env = idf_env()
    if detect and not port:
        port = detect_port()

    cmd = [venv_python(env), "-m", "esptool"]
    if port:
        cmd += ["--port", port]
    cmd += args
    print("esptool", " ".join(cmd[3:]), flush=True)
    return subprocess.call(cmd, env=env)


def idf(args: list[str], port: str | None, detect: bool = False) -> int:
    """Run idf.py against the workspace main() resolved.

    `detect` asks for the port to be found, but only after the environment is
    known good. Order matters: resolving the port first meant a machine with no
    ESP-IDF installed and no board plugged in reported "No serial port found",
    which is true and useless - the port was never the problem.
    """
    env = idf_env()
    if detect and not port:
        port = detect_port()
    entry = Path(env["IDF_PATH"]) / "tools" / "idf.py"

    cmd = [venv_python(env), str(entry), "-C", str(WORKSPACE)]
    if port:
        cmd += ["-p", port]
    cmd += args
    # From the -C, not past it: with the workspace no longer a constant in this
    # file, which product just got flashed has to be readable in the log.
    print("idf.py", " ".join(cmd[2:]), flush=True)
    return subprocess.call(cmd, env=env)


def source_files() -> list[Path]:
    """Every .c and .h we own. third_party/ and workspace/ hold none."""
    found: list[Path] = []
    for top in SOURCE_DIRS:
        for pattern in ("*.c", "*.h"):
            found += sorted((REPO / top).rglob(pattern))
    return found


def host_env() -> dict[str, str]:
    """The environment the host test build wants.

    Exactly ONE thing comes from ESP-IDF - the path, so the harness CMakeLists
    can find Unity. Handing over the whole exported environment is actively
    wrong: it puts esp-clang first on PATH and CMake then tries to build the
    HOST tests with a cross compiler for the target.
    """
    env = dict(os.environ)
    if not env.get("IDF_PATH"):
        configured = env_value("IDF_PATH")
        if configured:
            env["IDF_PATH"] = configured
    return env


def configure_host(sanitize: bool) -> tuple[Path, dict[str, str], int]:
    """Configure and build the host harness. Returns (build dir, env, status)."""
    if shutil.which("cmake") is None:
        sys.exit("cmake is not on PATH.")

    src = REPO / "test" / "host"
    # Separate trees so the plain and instrumented builds can both exist; CI
    # runs them as two jobs against the same checkout.
    out = REPO / "build" / ("san" if sanitize else "host")

    # Ninja rather than the platform default: on Windows the default is MSVC,
    # which cannot take the firmware's warning flags. Only on the first
    # configure - CMake refuses to change the generator of an existing tree.
    configure = ["cmake", "-S", str(src), "-B", str(out)]
    if not (out / "CMakeCache.txt").exists() and shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    if sanitize:
        configure += ["-DSANITIZE=ON"]

    env = host_env()
    for step in (configure, ["cmake", "--build", str(out)]):
        print(" ".join(step), flush=True)
        rc = subprocess.call(step, env=env)
        if rc != 0:
            return out, env, rc
    return out, env, 0


def run_tests(sanitize: bool = False) -> int:
    """R-TST-02: the pure logic runs on a PC, with no board attached.

    Unity comes from the ESP-IDF checkout, so a developer who can build the
    firmware can run the tests with nothing else installed. `sanitize` adds
    ASan and UBSan (R-SAN-08).
    """
    out, env, rc = configure_host(sanitize)
    if rc != 0:
        return rc
    return subprocess.call(["ctest", "--test-dir", str(out), "--output-on-failure"],
                           env=env)


def run_analyse() -> int:
    """R-SAN: cppcheck over everything we own, clang-tidy over what has a
    compile database.

    clang-tidy needs the exact compile flags, and the only build here that
    produces them for a compiler clang understands is the host test build - the
    firmware targets Xtensa, which upstream clang cannot parse. So clang-tidy
    covers the pure-logic modules and cppcheck covers the rest. Say so rather
    than implying the whole tree is analysed.
    """
    rc = 0

    if shutil.which("cppcheck") is None:
        print("cppcheck not on PATH - skipping")
    else:
        files = [str(f) for f in source_files() if f.suffix == ".c"]
        includes = [f"-I{d}" for d in sorted(
            {str(f.parent) for f in source_files() if f.suffix == ".h"})]
        cmd = ["cppcheck", "--enable=warning,style", "--std=c11",
               "--inline-suppr", "--quiet", "--error-exitcode=1",
               "--suppress=missingInclude", "--suppress=missingIncludeSystem",
               "--suppress=checkersReport", *includes, *files]
        print("cppcheck over", len(files), "file(s)", flush=True)
        rc |= subprocess.call(cmd)

    # Configure the harness if it has not been: a compile database is something
    # this command can produce, so making the caller remember to run `test`
    # first was a trap rather than a contract.
    db = REPO / "build" / "host" / "compile_commands.json"
    if shutil.which("clang-tidy") is None:
        print("clang-tidy not on PATH - skipping")
    else:
        if not db.exists():
            _, _, configure_rc = configure_host(sanitize=False)
            if configure_rc != 0:
                return rc | configure_rc
        ours = [e["file"] for e in json.loads(db.read_text())
                if any(f"/{top}/" in e["file"].replace("\\", "/")
                       for top in SOURCE_DIRS)]
        print("clang-tidy over", len(ours), "file(s)", flush=True)
        rc |= subprocess.call(["clang-tidy", "--quiet", "-p", str(db.parent), *ours])

    return rc


def run_format(check_only: bool) -> int:
    """R-FMT-01: clang-format decides, and the config file is the rule."""
    if shutil.which("clang-format") is None:
        sys.exit("clang-format is not on PATH.")

    files = source_files()
    if not files:
        print("no source files found")
        return 0

    flags = ["--dry-run", "--Werror"] if check_only else ["-i"]
    return subprocess.call(["clang-format", *flags, *[str(f) for f in files]])


# ---------------------------------------------------------- main ---

def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="With no command: build, flash and monitor, finding the port itself.")
    parser.add_argument("command", nargs="?",
                        choices=(*IDF_COMMANDS, "format", "test", "analyse"),
                        help="omit to build, flash and monitor")
    parser.add_argument("-p", "--port",
                        help=f"serial port, e.g. COM7. Only for: "
                             f"{', '.join(PORT_COMMANDS)}, or no command")
    parser.add_argument("-w", "--workspace", metavar="NAME",
                        help="product workspace under workspace/, e.g. 0xF001. "
                             "Required: falls back to WORKSPACE in .env.esp and "
                             "to nothing else, not even when the repo holds one "
                             "workspace. Not for: format, test, analyse")
    parser.add_argument("--address",
                        help="erase-flash only: start of the region to erase, "
                             "e.g. 0x19000. Erases the whole chip when omitted")
    parser.add_argument("--size",
                        help="erase-flash only: bytes to erase, e.g. 0x4000, "
                             "or `all` for everything from --address to the end "
                             "of the flash")
    parser.add_argument("--check", action="store_true",
                        help="format only: report instead of rewriting")
    parser.add_argument("--sanitize", action="store_true",
                        help="test only: build with ASan and UBSan")
    args = parser.parse_args()
    clear_screen()

    # A flag that does nothing is a flag someone will believe in. Reject rather
    # than ignore.
    if args.port and args.command not in (None, *PORT_COMMANDS):
        parser.error(f"-p/--port means nothing for `{args.command}`; it applies "
                     f"to {', '.join(PORT_COMMANDS)} or to no command at all")
    if args.workspace and args.command not in (None, *IDF_COMMANDS):
        parser.error(f"-w/--workspace means nothing for `{args.command}`; it "
                     f"formats, tests and analyses the whole repo, not one "
                     f"product")
    if (args.address or args.size) and args.command != "erase-flash":
        parser.error("--address/--size apply to `erase-flash` only")
    if bool(args.address) != bool(args.size):
        parser.error("--address and --size go together; one without the other "
                     "would erase a length or a place nobody named")
    # Before configured_flash_bytes(), which reads the resolved workspace's
    # sdkconfig.defaults, and only for the commands that reach the build.
    if args.command in (None, *IDF_COMMANDS):
        global WORKSPACE
        WORKSPACE = resolve_workspace(args.workspace)

    region = None
    if args.address:
        try:
            start = int(args.address, 0)
            length = (configured_flash_bytes() - start if args.size == "all"
                      else int(args.size, 0))
        except ValueError:
            parser.error("--address and --size must be integers, decimal or "
                         "0x-prefixed hex; --size also takes `all`")
        if length <= 0:
            parser.error(f"--address {args.address} is at or past the end of "
                         f"the {configured_flash_bytes() // (1024 * 1024)} MB "
                         f"flash sdkconfig.defaults configures")
        region = [start, length]
        # esptool refuses an unaligned region, and so should we, one step
        # earlier and naming why: flash erases a 4 KB sector at a time, so a
        # region that starts or ends mid-sector would take a neighbour with it.
        if any(v % SECTOR_SIZE for v in region):
            parser.error(f"--address and --size must be multiples of "
                         f"{SECTOR_SIZE:#x} (the flash sector)")
    if args.check and args.command != "format":
        parser.error("--check applies to `format` only")
    if args.sanitize and args.command != "test":
        parser.error("--sanitize applies to `test` only")

    if args.command is None:
        # The whole loop in one idf.py call: a second invocation would rebuild
        # nothing but would re-export the environment.
        return idf(["build", "flash", "monitor"], args.port, detect=True)
    if args.command == "format":
        return run_format(args.check)
    if args.command == "test":
        return run_tests(args.sanitize)
    if args.command == "analyse":
        return run_analyse()
    if args.command == "flash":
        # Flash and stay attached: the boot log is what says whether it worked.
        return idf(["flash", "monitor"], args.port, detect=True)
    if args.command == "monitor":
        return idf(["monitor"], args.port, detect=True)
    if args.command == "erase-flash":
        # Whole chip through idf.py; a named region through esptool, which is
        # the only one of the two that can do it. Either way this takes nvs,
        # otadata or whatever else lives there, and nothing here puts it back.
        if region is None:
            return idf(["erase-flash"], args.port, detect=True)
        return esptool(["erase-region", *(f"{v:#x}" for v in region)],
                       args.port, detect=True)
    if args.command == "size":
        # R-BLD-04: the number nobody looks at is the number that runs out.
        return idf(["size", "size-components"], None)
    if args.command == "merge":
        # One image flashed at 0x0 provisions a blank board, with no offsets to
        # get wrong at a bench. Every part of the name comes from the build
        # itself - the project() token, the workspace directory, today's date -
        # so nothing here can drift from what was actually built. The version
        # is deliberately not in it: it is in the image header via PROJECT_VER
        # (R-VER-01) and `tool-usb.py version` reads it back off a unit, which
        # a file name cannot be checked against.
        name = factory_image_name(cmake_project_name(WORKSPACE), WORKSPACE.name,
                                  datetime.datetime.now(datetime.timezone.utc))
        return idf(["merge-bin", "-o", name], None)
    return idf([args.command], None)


if __name__ == "__main__":
    sys.exit(main())
