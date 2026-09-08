#!/usr/bin/env python3
"""Host side of the USB command channel for App Updater (R-RPO-06).

The firmware answers a binary request/response protocol on a USB CDC-ACM port;
this speaks it, so the wire format can be exercised against a real board
instead of only against the host tests.

    python docs/scripts/tool-usb.py selftest             no board needed
    python docs/scripts/tool-usb.py ping [-p COM7] [--bytes N]
    python docs/scripts/tool-usb.py version [-p COM7]
    python docs/scripts/tool-usb.py boot-slot SLOT [-p COM7]
    python docs/scripts/tool-usb.py restart [-p COM7]
    python docs/scripts/tool-usb.py upgrade FILE [-p COM7] [--target 1]
                                                 [--chunk 32768] [--arm]

`middleware/protocol/include/protocol.h` is the authority on every number
below; the copies here exist because this script has to run without the
firmware's build. When the two disagree, the header is right.

Omit -p and the port is found by the device's own VID/PID, so a bench with
other serial adapters plugged in still works.

Python 3, no dependency beyond pyserial - and none at all for `selftest`,
which is why the import is deferred.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

# --------------------------------------------------------- protocol ---

VID = 0xA331
PID = 0xF001

HDR_REQ = 0x3E3E5152  # "RQ>>" on the wire
HDR_RSP = 0x3C3C5352  # "RS<<" on the wire

PREFIX_LEN = 12
OVERHEAD_LEN = 16
MAX_DATA = 32772
STATUS_LEN = 4

UPG_CHUNK_MIN = 4096
UPG_CHUNK_MAX = 32768
UPG_CHUNK_STEP = 1024

# The read chunk is a flash sector, not the upgrade band: a dump is at most
# 64 KB and read once in a unit's life, so 16 round-trips cost nothing while a
# 32 KB answer would cost 32 KB of permanent .bss in the dispatcher.
DUMP_CHUNK_MAX = 4096

CMD_RESTART_APP = 0x0001
CMD_PING = 0x0006
CMD_SET_BOOT_SLOT = 0x0101
CMD_GET_VERSION = 0x0201
CMD_GET_BOOT_SLOT = 0x0202
CMD_UPG_BEGIN = 0x0601
CMD_UPG_WRITE = 0x0602
CMD_UPG_END = 0x0603
CMD_DUMP_INFO = 0x0701
CMD_DUMP_READ = 0x0702
CMD_DUMP_ERASE = 0x0703

SLOT_UPDATER = 0
SLOT_FIRMWARE = 1
SLOT_NAMES = {SLOT_UPDATER: "app_updater", SLOT_FIRMWARE: "app_firmware"}

VERSION_BLOCK_LEN = 32
VERSION_FIELD_LEN = 16

DUMP_INFO_LEN = 8
DUMP_STATE_ABSENT = 0
DUMP_STATE_VALID = 1
DUMP_STATE_CORRUPT = 2
DUMP_STATE_NAMES = {
    DUMP_STATE_ABSENT: "absent",
    DUMP_STATE_VALID: "valid",
    DUMP_STATE_CORRUPT: "CORRUPT (stored, but its checksum does not match)",
}

STATUS_NAMES = {
    0: "OK",
    -1: "ERR_CRC",
    -2: "ERR_BAD_CMD (this opcode is not in the spec)",
    -3: "ERR_BAD_LEN (wrong LENGTH for this command)",
    -4: "ERR_BAD_ARG (payload value rejected)",
    -5: "ERR_STATE (right command, wrong device state - retryable)",
    -6: "ERR_HW (the driver underneath failed)",
    -7: "ERR_UNSUPPORTED (defined, but not built into this firmware)",
}

# The canonical check value of the reflected CRC-32 the wire format uses. The
# firmware pins the same number in middleware/protocol/test/test_protocol.c, so
# both implementations are held to one fixed value rather than to each other.
KNOWN_ANSWER_INPUT = b"123456789"
KNOWN_ANSWER_CRC = 0xCBF43926


def round4(n: int) -> int:
    """DATA is padded so every field stays 4-byte aligned."""
    return (n + 3) & ~3


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_req(command: int, payload: bytes = b"") -> bytes:
    """One request frame: HEADER, COMMAND, LENGTH, padded DATA, CRC32."""
    if len(payload) > MAX_DATA:
        sys.exit(f"\npayload of {len(payload)} bytes is past the {MAX_DATA}-byte cap.\n")

    body = struct.pack("<III", HDR_REQ, command, len(payload))
    body += payload + (b"\x00" * (round4(len(payload)) - len(payload)))
    return body + struct.pack("<I", crc32(body))


def build_rsp(command: int, status: int, payload: bytes = b"") -> bytes:
    """The response the device would send. Only `selftest` needs this."""
    data = struct.pack("<i", status) + payload
    body = struct.pack("<III", HDR_RSP, command, len(data))
    body += data + (b"\x00" * (round4(len(data)) - len(data)))
    return body + struct.pack("<I", crc32(body))


def parse_rsp(frame: bytes) -> tuple[int, int, bytes]:
    """Returns (command, status, payload), or exits saying what was wrong."""
    if len(frame) < OVERHEAD_LEN:
        sys.exit(f"\nresponse is {len(frame)} bytes, shorter than a frame.\n")

    header, command, length = struct.unpack("<III", frame[:PREFIX_LEN])
    if header != HDR_RSP:
        sys.exit(f"\nresponse header is 0x{header:08X}, expected 0x{HDR_RSP:08X}.\n")

    padded = round4(length)
    if len(frame) != OVERHEAD_LEN + padded:
        sys.exit(f"\nresponse says {length} data bytes but carries "
                 f"{len(frame) - OVERHEAD_LEN}.\n")

    want = struct.unpack("<I", frame[PREFIX_LEN + padded:])[0]
    got = crc32(frame[:PREFIX_LEN + padded])
    if got != want:
        sys.exit(f"\nresponse CRC is 0x{got:08X}, frame says 0x{want:08X}.\n")

    if length < STATUS_LEN:
        sys.exit(f"\nresponse carries {length} bytes, too few for a status.\n")

    status = struct.unpack("<i", frame[PREFIX_LEN:PREFIX_LEN + STATUS_LEN])[0]
    return command, status, frame[PREFIX_LEN + STATUS_LEN:PREFIX_LEN + length]


def status_text(status: int) -> str:
    return STATUS_NAMES.get(status, f"unknown status {status}")


# ------------------------------------------------------ the channel ---

class Channel:
    """One open port, one command in flight - which is all the protocol allows."""

    def __init__(self, port: str, timeout: float) -> None:
        try:
            import serial
        except ImportError:
            sys.exit("\npyserial is not installed. It is only needed to talk to a\n"
                     "board; `selftest` runs without it.\n\n  pip install pyserial\n")

        try:
            self._port = serial.Serial(port, timeout=timeout, write_timeout=timeout)
        except Exception as exc:  # SerialException, OSError, ValueError
            sys.exit(f"\ncannot open {port}: {exc}\n")
        self._name = port

    def close(self) -> None:
        self._port.close()

    def _read_exact(self, count: int) -> bytes:
        data = self._port.read(count)
        if len(data) != count:
            sys.exit(f"\n{self._name}: wanted {count} bytes, got {len(data)}. "
                     f"Is the firmware running?\n")
        return data

    def ask(self, command: int, payload: bytes = b"") -> tuple[int, bytes]:
        """Sends one request and returns its (status, payload)."""
        self._port.write(build_req(command, payload))
        self._port.flush()

        prefix = self._read_exact(PREFIX_LEN)
        length = struct.unpack("<III", prefix)[2]
        if length > MAX_DATA:
            sys.exit(f"\nresponse claims {length} data bytes, past the "
                     f"{MAX_DATA}-byte cap.\n")

        frame = prefix + self._read_exact(round4(length) + 4)
        echoed, status, body = parse_rsp(frame)
        if echoed != command:
            sys.exit(f"\nresponse echoes 0x{echoed:04X}, asked 0x{command:04X}.\n")
        return status, body

    def expect_ok(self, command: int, payload: bytes = b"") -> bytes:
        status, body = self.ask(command, payload)
        if status != 0:
            sys.exit(f"\n0x{command:04X} answered {status}: {status_text(status)}\n")
        return body


def find_port() -> str:
    """The board's own port, found by VID and PID rather than by guessing."""
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("\npyserial is not installed, so the port cannot be detected.\n"
                 "Pass -p PORT, or:  pip install pyserial\n")

    every = sorted(list_ports.comports(), key=lambda p: p.device)
    ours = [p for p in every if p.vid == VID and p.pid == PID]

    if len(ours) == 1:
        print(f"port: {ours[0].device}  ({ours[0].description})")
        return ours[0].device

    if not ours:
        seen = "\n".join(f"  {p.device}  {p.description}" for p in every)
        sys.exit(f"\nNo port with VID:PID {VID:04X}:{PID:04X} found.\n"
                 f"Ports on this machine:\n{seen or '  (none)'}\n\n"
                 f"Plug the USB cable into the OTG port, or pass -p PORT.\n")

    # Guessing between two boards is how an image lands on the wrong one.
    listing = "\n".join(f"  {p.device}  {p.description}" for p in ours)
    sys.exit(f"\n{len(ours)} boards with VID:PID {VID:04X}:{PID:04X} - say which "
             f"one with -p PORT:\n{listing}\n")


# ---------------------------------------------------- pre-flight ---

def read_image(path: Path, chunk: int) -> bytes:
    """Everything about an upgrade that can be judged before the port opens.

    Checked here rather than inside cmd_upgrade for the same reason the
    firmware checks every UPG_BEGIN argument before it erases anything: a
    request that was never going to work should cost nothing, and a bad
    --chunk reported as a port error sends the reader looking in the wrong
    place.
    """
    if chunk % UPG_CHUNK_STEP != 0 or not UPG_CHUNK_MIN <= chunk <= UPG_CHUNK_MAX:
        sys.exit(f"\n--chunk must be a multiple of {UPG_CHUNK_STEP} between "
                 f"{UPG_CHUNK_MIN} and {UPG_CHUNK_MAX}; the device refuses "
                 f"anything else.\n")
    try:
        image = path.read_bytes()
    except OSError as exc:
        sys.exit(f"\ncannot read {path}: {exc}\n")
    if not image:
        sys.exit(f"\n{path} is empty.\n")
    return image


def dump_target(path: Path) -> Path:
    """Where the dump will be written, judged before the port opens.

    Only the directory is checked, and nothing is created: the file is written
    once, at the end, from a complete transfer. A half-written crash.bin that
    `esp-coredump` then refuses is worse than no file at all, and creating it
    up front would also destroy a previous dump before knowing there is a new
    one to replace it with.
    """
    parent = path.parent if str(path.parent) else Path(".")
    if not parent.is_dir():
        sys.exit(f"\n{parent} is not a directory, so {path.name} cannot be "
                 f"written there.\n")
    if path.is_dir():
        sys.exit(f"\n{path} is a directory; give a file name.\n")
    return path


# ------------------------------------------------------- commands ---

def cmd_selftest() -> int:
    """Checks this script against the pinned CRC vector and against itself."""
    got = crc32(KNOWN_ANSWER_INPUT)
    if got != KNOWN_ANSWER_CRC:
        print(f"FAIL: CRC-32 of {KNOWN_ANSWER_INPUT!r} is 0x{got:08X}, "
              f"expected 0x{KNOWN_ANSWER_CRC:08X}")
        return 1

    for payload in (b"", b"A", b"ABC", b"ABCD", bytes(range(200))):
        request = build_req(CMD_PING, payload)
        if len(request) != OVERHEAD_LEN + round4(len(payload)):
            print(f"FAIL: a {len(payload)}-byte payload built a "
                  f"{len(request)}-byte frame")
            return 1
        if len(request) % 4 != 0:
            print(f"FAIL: {len(request)}-byte frame is not 4-byte aligned")
            return 1

        command, status, echoed = parse_rsp(build_rsp(CMD_PING, 0, payload))
        if (command, status, echoed) != (CMD_PING, 0, payload):
            print(f"FAIL: round trip of {len(payload)} bytes gave "
                  f"(0x{command:04X}, {status}, {echoed!r})")
            return 1

    print(f"selftest: CRC vector 0x{KNOWN_ANSWER_CRC:08X} and 5 frame round "
          f"trips OK")
    return 0


def cmd_ping(chan: Channel, count: int) -> int:
    payload = bytes((i * 31) & 0xFF for i in range(count))
    status, echoed = chan.ask(CMD_PING, payload)

    if status != 0:
        print(f"ping: {status_text(status)}")
        return 1
    if echoed != payload:
        print(f"ping: {len(payload)} bytes out, {len(echoed)} back, and they differ")
        return 1

    print(f"ping: {count} bytes echoed byte for byte")
    return 0


def cmd_version(chan: Channel) -> int:
    block = chan.expect_ok(CMD_GET_VERSION)
    if len(block) != VERSION_BLOCK_LEN:
        print(f"version: {len(block)} bytes, expected {VERSION_BLOCK_LEN}")
        return 1

    def field(raw: bytes) -> str:
        text = raw.split(b"\x00", 1)[0].decode("ascii", "replace")
        # Sixteen zero bytes mean the slot was never written, which is an
        # answer and not a failure.
        return text if text else "(unset)"

    print(f"app_updater  (running): {field(block[:VERSION_FIELD_LEN])}")
    print(f"app_firmware (slot)   : {field(block[VERSION_FIELD_LEN:])}")

    slot = chan.expect_ok(CMD_GET_BOOT_SLOT)
    print(f"running slot          : {slot[0]} ({SLOT_NAMES.get(slot[0], '?')})")
    return 0


def cmd_boot_slot(chan: Channel, slot: int) -> int:
    chan.expect_ok(CMD_SET_BOOT_SLOT, bytes([slot]))
    # Set arms the NEXT boot; Get reports what is running now. Reading it back
    # here would show the old value and look like a failure.
    print(f"armed slot {slot} ({SLOT_NAMES[slot]}) for the next boot - "
          f"restart to run it")
    return 0


def cmd_restart(chan: Channel) -> int:
    chan.expect_ok(CMD_RESTART_APP)
    print("restart accepted; the port will re-enumerate")
    return 0


def cmd_upgrade(chan: Channel, name: str, image: bytes, target: int, chunk: int,
                arm: bool) -> int:
    want = crc32(image)
    chunks = -(-len(image) // chunk)
    print(f"{name}: {len(image)} bytes, crc32 0x{want:08X}, {chunks} chunks of "
          f"up to {chunk}")

    chan.expect_ok(CMD_UPG_BEGIN, struct.pack("<BIII", target, len(image), want, chunk))

    sent = 0
    while sent < len(image):
        piece = image[sent:sent + chunk]
        chan.expect_ok(CMD_UPG_WRITE, struct.pack("<I", sent) + piece)
        sent += len(piece)
        print(f"\r  {sent}/{len(image)} bytes ({100 * sent // len(image)}%)",
              end="", flush=True)
    print()

    chan.expect_ok(CMD_UPG_END)
    print("image verified, slot is valid - and NOT armed")

    if arm:
        cmd_boot_slot(chan, target)
        return cmd_restart(chan)

    print(f"to boot it:  tool-usb.py boot-slot {target}   then   "
          f"tool-usb.py restart")
    return 0


def cmd_dump(chan: Channel, path: Path, keep: bool) -> int:
    """Pulls the stored core dump to a file, then clears it on the device."""
    info = chan.expect_ok(CMD_DUMP_INFO)
    if len(info) < DUMP_INFO_LEN:
        sys.exit(f"\nDUMP_INFO answered {len(info)} bytes, expected "
                 f"{DUMP_INFO_LEN}.\n")

    state = info[0]
    size = struct.unpack("<I", info[4:8])[0]
    name = DUMP_STATE_NAMES.get(state, f"unknown state {state}")

    if state == DUMP_STATE_ABSENT:
        # Not an error on the device's part - nothing has panicked. Still a
        # non-zero exit, because `dump crash.bin && esp-coredump ... crash.bin`
        # must not run the second half against a file that was never written.
        print("no core dump stored - nothing to fetch")
        return 1

    print(f"core dump: {name}, {size} bytes")
    if state == DUMP_STATE_CORRUPT:
        # Fetched anyway, and deliberately: a dump that fails its checksum is
        # exactly the one worth looking at. esp-coredump may still read part
        # of it, and even a partial backtrace beats none.
        print("  WARNING: the checksum does not match. Fetching it regardless "
              "- some of it may still decode.")

    got = bytearray()
    while len(got) < size:
        want = min(DUMP_CHUNK_MAX, size - len(got))
        piece = chan.expect_ok(CMD_DUMP_READ, struct.pack("<II", len(got), want))
        if len(piece) != want:
            sys.exit(f"\nasked {want} bytes at {len(got)}, got {len(piece)}.\n")
        got += piece
        print(f"\r  {len(got)}/{size} bytes ({100 * len(got) // size}%)",
              end="", flush=True)
    print()

    # Written only now, from a complete transfer, and before the erase: the
    # device keeps the only copy until the file exists on disk.
    try:
        path.write_bytes(bytes(got))
    except OSError as exc:
        sys.exit(f"\ncannot write {path}: {exc}\n"
                 f"The dump is still on the device; run this again.\n")
    print(f"wrote {path} ({len(got)} bytes)")

    if keep:
        # CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE is on, so this is not a
        # harmless choice and the tool says so rather than leaving it implied.
        print("--keep: the dump is still on the device, so the NEXT panic "
              "will NOT be captured until it is erased")
    else:
        chan.expect_ok(CMD_DUMP_ERASE)
        print("dump erased on the device; the next panic has room")

    print(f"\nto read it:  esp-coredump info_corefile --core-format raw "
          f"-c {path} <the .elf of the build that crashed>")
    return 0


# ------------------------------------------------------------ main ---

PORT_COMMANDS = ("ping", "version", "boot-slot", "restart", "upgrade", "dump")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="protocol.h is the authority on every opcode and constant.")
    parser.add_argument("command", choices=("selftest", *PORT_COMMANDS))
    parser.add_argument("argument", nargs="?",
                        help="upgrade: the image file. boot-slot: 0 or 1. "
                             "dump: the file to write")
    parser.add_argument("-p", "--port", help="serial port, e.g. COM7. Omit to detect")
    parser.add_argument("--bytes", type=int, default=64,
                        help="ping only: payload size, default 64")
    parser.add_argument("--target", type=int, choices=(0, 1), default=SLOT_FIRMWARE,
                        help="upgrade only: 1 app_firmware, 0 app_updater")
    parser.add_argument("--chunk", type=int, default=UPG_CHUNK_MAX,
                        help=f"upgrade only: {UPG_CHUNK_MIN}..{UPG_CHUNK_MAX}, "
                             f"multiple of {UPG_CHUNK_STEP}")
    parser.add_argument("--arm", action="store_true",
                        help="upgrade only: also set the boot slot and restart")
    parser.add_argument("--keep", action="store_true",
                        help="dump only: leave the dump on the device. It then "
                             "captures no further panic until erased")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="seconds to wait for one response, default 5")
    args = parser.parse_args()

    # A flag that does not apply is rejected, not ignored: one that silently
    # does nothing is one somebody will believe in. tool-esp.py does the same.
    if args.command == "selftest" and args.port:
        sys.exit("\nselftest talks to no board, so -p does not apply.\n")
    if args.command != "upgrade" and (args.arm or args.chunk != UPG_CHUNK_MAX):
        sys.exit("\n--arm and --chunk are for `upgrade` only.\n")
    if args.command != "ping" and args.bytes != 64:
        sys.exit("\n--bytes is for `ping` only.\n")
    if args.command != "dump" and args.keep:
        sys.exit("\n--keep is for `dump` only.\n")

    if args.command == "selftest":
        return cmd_selftest()

    if args.command == "upgrade" and not args.argument:
        sys.exit("\nupgrade needs an image file.\n")
    if args.command == "boot-slot" and args.argument not in ("0", "1"):
        sys.exit("\nboot-slot needs 0 (app_updater) or 1 (app_firmware).\n")
    if args.command == "dump" and not args.argument:
        sys.exit("\ndump needs a file to write, e.g. crash.bin\n")

    # Judged before the port is opened, so a bad --chunk or a missing file
    # never costs a board connection.
    image = b""
    if args.command == "upgrade":
        image = read_image(Path(args.argument), args.chunk)
    dump_path = Path()
    if args.command == "dump":
        dump_path = dump_target(Path(args.argument))

    chan = Channel(args.port or find_port(), args.timeout)
    try:
        if args.command == "ping":
            return cmd_ping(chan, args.bytes)
        if args.command == "version":
            return cmd_version(chan)
        if args.command == "boot-slot":
            return cmd_boot_slot(chan, int(args.argument))
        if args.command == "restart":
            return cmd_restart(chan)
        if args.command == "dump":
            return cmd_dump(chan, dump_path, args.keep)
        return cmd_upgrade(chan, Path(args.argument).name, image, args.target,
                           args.chunk, args.arm)
    finally:
        chan.close()


if __name__ == "__main__":
    sys.exit(main())
