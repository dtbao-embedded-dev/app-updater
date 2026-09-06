#!/usr/bin/env python3
"""Developer commands for app-updater (R-RPO-06).

One entry point rather than four scripts: every command is idf.py with the
workspace and the target already filled in, which is the part nobody remembers.

    python docs/scripts/tool-esp.py build
    python docs/scripts/tool-esp.py flash --port COM7
    python docs/scripts/tool-esp.py monitor --port COM7
    python docs/scripts/tool-esp.py size
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
                                 "menuconfig", "format"))
    parser.add_argument("-p", "--port", help="serial port, e.g. COM7 or /dev/ttyUSB0")
    parser.add_argument("--check", action="store_true",
                        help="format: report instead of rewriting")
    args = parser.parse_args()

    if args.command == "format":
        return run_format(args.check)
    if args.command == "flash":
        # Flash and stay attached: the boot log is what says whether it worked.
        return idf(["flash", "monitor"], args.port)
    if args.command == "size":
        # R-BLD-04: the number nobody looks at is the number that runs out.
        return idf(["size", "size-components"], args.port)
    return idf([args.command], args.port)


if __name__ == "__main__":
    sys.exit(main())
