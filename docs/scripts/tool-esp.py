#!/usr/bin/env python3
"""Developer commands for app-updater (R-RPO-06).

One entry point rather than four scripts: every command is idf.py with the
workspace and the target already filled in, which is the part nobody remembers.

    python docs/scripts/tool-esp.py build
    python docs/scripts/tool-esp.py flash --port COM7
    python docs/scripts/tool-esp.py monitor --port COM7
    python docs/scripts/tool-esp.py size
    python docs/scripts/tool-esp.py test
    python docs/scripts/tool-esp.py analyse
    python docs/scripts/tool-esp.py format --check

Python 3 so one set of commands runs on Windows and Linux without a shell port.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO / "workspace" / "0xF001"
SOURCE_DIRS = ("application", "middleware", "driver")


def idf(args: list[str], port: str | None) -> int:
    """Run idf.py against the 0xF001 workspace."""
    if not os.environ.get("IDF_PATH"):
        sys.exit("IDF_PATH is not set - run the ESP-IDF export script first.")

    cmd = ["idf.py", "-C", str(WORKSPACE)]
    if port:
        cmd += ["-p", port]
    cmd += args
    print(" ".join(cmd), flush=True)
    return subprocess.call(cmd)


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

    for step in (configure,
                 ["cmake", "--build", str(out)]):
        print(" ".join(step), flush=True)
        rc = subprocess.call(step)
        if rc != 0:
            return rc

    return subprocess.call(["ctest", "--test-dir", str(out), "--output-on-failure"])


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
        import json
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command",
                        choices=("build", "flash", "monitor", "size", "clean",
                                 "menuconfig", "format", "test", "analyse"))
    parser.add_argument("-p", "--port", help="serial port, e.g. COM7 or /dev/ttyUSB0")
    parser.add_argument("--check", action="store_true",
                        help="format: report instead of rewriting")
    args = parser.parse_args()

    if args.command == "format":
        return run_format(args.check)
    if args.command == "test":
        return run_tests()
    if args.command == "analyse":
        return run_analyse()
    if args.command == "flash":
        # Flash and stay attached: the boot log is what says whether it worked.
        return idf(["flash", "monitor"], args.port)
    if args.command == "size":
        # R-BLD-04: the number nobody looks at is the number that runs out.
        return idf(["size", "size-components"], args.port)
    return idf([args.command], args.port)


if __name__ == "__main__":
    sys.exit(main())
