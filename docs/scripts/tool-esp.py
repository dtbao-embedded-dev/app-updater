#!/usr/bin/env python3
"""Developer commands for app-updater (R-RPO-06).

One entry point rather than six scripts: every ESP-IDF command is idf.py with
the workspace, the environment and the serial port already filled in, which is
the part nobody remembers.

    python docs/scripts/tool-esp.py              build + flash + monitor, port found for you
    python docs/scripts/tool-esp.py build
    python docs/scripts/tool-esp.py flash [-p COM7]
    python docs/scripts/tool-esp.py monitor [-p COM7]
    python docs/scripts/tool-esp.py size
    python docs/scripts/tool-esp.py clean
    python docs/scripts/tool-esp.py menuconfig
    python docs/scripts/tool-esp.py format [--check]
    python docs/scripts/tool-esp.py test
    python docs/scripts/tool-esp.py analyse

ESP-IDF is found through `.env.esp` at the repo root, which holds the path to
the checkout that has export.bat / export.sh. The file is per-machine and
gitignored; the first run writes a template and stops so it can be filled in.

Python 3 so one set of commands runs on Windows and Linux without a shell port.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO / "workspace" / "0xF001"
SOURCE_DIRS = ("application", "middleware", "driver")
ENV_FILE = REPO / ".env.esp"

# Commands that talk to a board, so a port is meaningful. Everything else
# rejects -p rather than accepting it and quietly doing nothing with it.
PORT_COMMANDS = ("flash", "monitor")
IDF_COMMANDS = ("build", "flash", "monitor", "size", "clean", "menuconfig")

ENV_TEMPLATE = """# Where ESP-IDF lives on THIS machine.
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
"""


# ------------------------------------------------- finding ESP-IDF ---

def read_env_file() -> str:
    """The IDF_PATH line from .env.esp, or '' when it is not usable yet."""
    if not ENV_FILE.exists():
        return ""
    for line in ENV_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if key.strip() == "IDF_PATH":
            return value.strip().strip('"').strip("'")
    return ""


def idf_root() -> Path:
    """Locate ESP-IDF, writing the template and stopping on the first run."""
    raw = read_env_file()

    if not ENV_FILE.exists():
        ENV_FILE.write_text(ENV_TEMPLATE, encoding="utf-8")
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


# ------------------------------------------------------ commands ---

def idf(args: list[str], port: str | None, detect: bool = False) -> int:
    """Run idf.py against the 0xF001 workspace.

    `detect` asks for the port to be found, but only after the environment is
    known good. Order matters: resolving the port first meant a machine with no
    ESP-IDF installed and no board plugged in reported "No serial port found",
    which is true and useless - the port was never the problem.
    """
    env = idf_env()
    if detect and not port:
        port = detect_port()
    # Windows resolves an executable against the PARENT process PATH, not the
    # env= handed to the child, so bare `idf.py` is not found however correct
    # the exported PATH is. Going through the exported interpreter sidesteps
    # both that and the .py / PATHEXT association.
    # The interpreter has to be the one from ESP-IDF's own virtual environment;
    # a system python cannot import esp_idf_monitor. idf_env() has already
    # refused if that environment is missing.
    venv_bin = Path(env["IDF_PYTHON_ENV_PATH"]) / ("Scripts" if os.name == "nt" else "bin")
    python = (shutil.which("python", path=str(venv_bin))
              or shutil.which("python", path=env["PATH"])
              or sys.executable)
    entry = Path(env["IDF_PATH"]) / "tools" / "idf.py"

    cmd = [python, str(entry), "-C", str(WORKSPACE)]
    if port:
        cmd += ["-p", port]
    cmd += args
    print("idf.py", " ".join(cmd[3:]), flush=True)
    return subprocess.call(cmd, env=env)


def source_files() -> list[Path]:
    """Every .c and .h we own. third_party/ and workspace/ hold none."""
    found: list[Path] = []
    for top in SOURCE_DIRS:
        for pattern in ("*.c", "*.h"):
            found += sorted((REPO / top).rglob(pattern))
    return found


def run_tests() -> int:
    """R-TST-02: the pure logic runs on a PC, with no board attached.

    Unity comes from the ESP-IDF checkout, so a developer who can build the
    firmware can run the tests with nothing else installed.
    """
    if shutil.which("cmake") is None:
        sys.exit("cmake is not on PATH.")

    src = REPO / "test" / "host"
    out = REPO / "build" / "host"

    # Ninja rather than the platform default: on Windows the default is MSVC,
    # which cannot take the firmware's warning flags. Only on the first
    # configure - CMake refuses to change the generator of an existing tree.
    configure = ["cmake", "-S", str(src), "-B", str(out)]
    if not (out / "CMakeCache.txt").exists() and shutil.which("ninja"):
        configure += ["-G", "Ninja"]

    # The harness needs exactly ONE thing from ESP-IDF - the path, so its
    # CMakeLists can find Unity. Handing over the whole exported environment is
    # actively wrong: that environment puts esp-clang first on PATH and CMake
    # then tries to build the HOST tests with a cross compiler for the target.
    # A tree already configured with -DUNITY_DIR needs none of this.
    env = dict(os.environ)
    if not env.get("IDF_PATH"):
        configured = read_env_file()
        if configured:
            env["IDF_PATH"] = configured
    for step in (configure, ["cmake", "--build", str(out)]):
        print(" ".join(step), flush=True)
        rc = subprocess.call(step, env=env)
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

    db = REPO / "build" / "host" / "compile_commands.json"
    if shutil.which("clang-tidy") is None:
        print("clang-tidy not on PATH - skipping")
    elif not db.exists():
        print(f"no compile database at {db} - run `tool-esp.py test` first")
        rc |= 1
    else:
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
    parser.add_argument("--check", action="store_true",
                        help="format only: report instead of rewriting")
    args = parser.parse_args()
    clear_screen()

    # A flag that does nothing is a flag someone will believe in. Reject rather
    # than ignore.
    if args.port and args.command not in (None, *PORT_COMMANDS):
        parser.error(f"-p/--port means nothing for `{args.command}`; it applies "
                     f"to {', '.join(PORT_COMMANDS)} or to no command at all")
    if args.check and args.command != "format":
        parser.error("--check applies to `format` only")

    if args.command is None:
        # The whole loop in one idf.py call: a second invocation would rebuild
        # nothing but would re-export the environment.
        return idf(["build", "flash", "monitor"], args.port, detect=True)
    if args.command == "format":
        return run_format(args.check)
    if args.command == "test":
        return run_tests()
    if args.command == "analyse":
        return run_analyse()
    if args.command == "flash":
        # Flash and stay attached: the boot log is what says whether it worked.
        return idf(["flash", "monitor"], args.port, detect=True)
    if args.command == "monitor":
        return idf(["monitor"], args.port, detect=True)
    if args.command == "size":
        # R-BLD-04: the number nobody looks at is the number that runs out.
        return idf(["size", "size-components"], None)
    return idf([args.command], None)


if __name__ == "__main__":
    sys.exit(main())
