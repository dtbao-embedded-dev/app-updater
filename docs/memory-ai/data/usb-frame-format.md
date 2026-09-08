---
title: USB Frame Format
category: data
order: 4
purpose: The wire format of the USB command channel, its constants, and what those constants cost in RAM.
status: active
updated: 2026-09-07
source: middleware/protocol/include/protocol.h, middleware/protocol/src/protocol.c, middleware/protocol/test/test_protocol.c
confidence: confirmed
keywords: PROTOCOL_HDR_REQ, PROTOCOL_HDR_RSP, PROTOCOL_MAX_DATA, PROTOCOL_MAX_FRAME, round4, CRC32, protocol_status_t, PROTOCOL_UPG_CHUNK_MIN, PROTOCOL_UPG_CHUNK_CAP, little-endian
---

# USB Frame Format

> Five fields, all 4-byte aligned, little-endian, with a CRC-32 over everything
> in front of it. Adopted verbatim from
> `data-monitor/data-mirror-firmware/docs/spec/usb/usb-command-protocol.md`.

## The frame

Left to right: **HEADER · COMMAND · LENGTH · DATA · CRC32**.

| Field | Size | Meaning |
|-------|------|---------|
| HEADER | 4 B | Direction magic, different per direction so a parser knows who sent it and can resync |
| COMMAND | 4 B | Opcode `0x0000RRNN`; the response echoes it unchanged |
| LENGTH | 4 B | `n`, the **true** payload byte count. May be 0. Capped at `PROTOCOL_MAX_DATA` |
| DATA | `round4(n)` B | Payload, zero-padded to a multiple of 4. Pad bytes are 0 and **are covered by the CRC**; only the first `n` reach a handler |
| CRC32 | 4 B | IEEE CRC-32 (poly `0xEDB88320`, reflected, init and xorout `0xFFFFFFFF` — the zlib/Ethernet CRC) over HEADER..DATA |

Every field being 4-byte aligned is what makes the frame a multiple of 4 and puts
the CRC on a boundary. Minimum frame is **16 bytes** (`n = 0`); maximum is
`PROTOCOL_MAX_FRAME` = 32788.

Header corruption is caught, because the CRC covers the header too.

## Direction magic

| Direction | Name | Bytes on the wire | As a little-endian word |
|-----------|------|-------------------|-------------------------|
| host → device | `PROTOCOL_HDR_REQ` | `52 51 3E 3E` ("RQ>>") | `0x3E3E5152` |
| device → host | `PROTOCOL_HDR_RSP` | `52 53 3C 3C` ("RS<<") | `0x3C3C5352` |

ASCII-ish so a hex dump is readable, and differing in every direction-carrying
byte so a single flipped bit cannot turn one into the other.

## Responses

`RSP.DATA` is always `[STATUS:4][payload]`, so `RSP.LENGTH` is
`PROTOCOL_STATUS_LEN + payload_len`. The status is a signed little-endian word:

| Value | Name | Meaning |
|-------|------|---------|
| `0` | `PROTOCOL_OK` | success, including every "nothing is there" answer |
| `-1` | `PROTOCOL_ERR_CRC` | **never transmitted** — a bad-CRC request is dropped without a reply, because its COMMAND cannot be trusted enough to echo. The firmware reuses this value internally to mean "already answered" |
| `-2` | `PROTOCOL_ERR_BAD_CMD` | this spec does not define the opcode at all |
| `-3` | `PROTOCOL_ERR_BAD_LEN` | `LENGTH` wrong for this command |
| `-4` | `PROTOCOL_ERR_BAD_ARG` | payload value rejected |
| `-5` | `PROTOCOL_ERR_STATE` | right command, wrong device state; retryable |
| `-6` | `PROTOCOL_ERR_HW` | the driver underneath failed |
| `-7` | `PROTOCOL_ERR_UNSUPPORTED` | defined here, not built into this firmware |

`-2` versus `-7` is the difference a tool acts on: `-2` says fix the request,
`-7` says the request is fine and the build cannot serve it.

## Constants

| Constant | Value | Why |
|----------|-------|-----|
| `PROTOCOL_PREFIX_LEN` | 12 | HEADER + COMMAND + LENGTH |
| `PROTOCOL_OVERHEAD_LEN` | 16 | the prefix plus the CRC |
| `PROTOCOL_MAX_DATA` | 32772 | one 32 KB `UPG_WRITE` chunk plus its 4-byte offset |
| `PROTOCOL_MAX_FRAME` | 32788 | `PROTOCOL_OVERHEAD_LEN + PROTOCOL_MAX_DATA` |
| `PROTOCOL_STATUS_LEN` | 4 | the status opening every response |
| `PROTOCOL_UPG_CHUNK_MIN` | 4096 | floor on a proposed `chunk_max` |
| `PROTOCOL_UPG_CHUNK_MAX` | 32768 | protocol ceiling on a chunk |
| `PROTOCOL_UPG_CHUNK_CAP` | 32768 | largest `chunk_max` this build accepts |
| `PROTOCOL_UPG_CHUNK_STEP` | 1024 | the step the band moves in |
| `PROTOCOL_UPG_BEGIN_LEN` | 13 | `[target:1][img_size:4][img_crc32:4][chunk_max:4]` |
| `PROTOCOL_VERSION_LEN` | 32 | the VERSION block, two 16-byte fields |
| `PROTOCOL_MAC_LEN` | 6 | either MAC |

## What the size costs

`PROTOCOL_MAX_DATA` = 32772 buys the fewest frames per image: a 13.875 MB
`app_firmware` is about 425 chunks at 32 KB rather than ~3400 at 4 KB. The price
is static RAM, and this build spends **two** buffers of that order rather than
the three the source spec budgets:

| Buffer | Where | Bytes |
|--------|-------|-------|
| parser window | `protocol_parser_t` in `app_ctx_t` | 32788 + 8 |
| reply frame | `command_t` in `app_ctx_t` | 32788 |

A response body is built straight into the reply frame, which is what removes
the third. **Measured, not estimated:** `tool-esp.py size` reports `.bss` at
70,592 bytes total — 20.66 % of DRAM — of which about 65.6 KB is these two.
Roughly 270 KB of DRAM is left, against the ~40–50 KB a TLS OTA fetch wants.

Lowering `PROTOCOL_MAX_DATA` lowers the accepted chunk band with it, so the two
move together or the band stops fitting a frame.

## Invariants a reader must enforce

1. `LENGTH > PROTOCOL_MAX_DATA` is rejected **while the length is read**, and
   the hunt resumes from the byte after the failed header. Nothing is allocated
   on the receive path.
2. A CRC mismatch drops the frame silently — **no response at all**.
3. `round4(n)` pad bytes are inside the CRC and outside the handler's view.
4. `COMMAND` is never `0x00000000`: item numbering starts at `0x01` in every
   range, so the value a zeroed buffer produces cannot be a command.

## See also

- [../interface/protocol-api.md](../interface/protocol-api.md) — the codec contract
- [../behavior/usb-command-dispatch.md](../behavior/usb-command-dispatch.md) — what happens to a decoded frame
- [../behavior/usb-host-flow.md](../behavior/usb-host-flow.md) — the host's sequence
