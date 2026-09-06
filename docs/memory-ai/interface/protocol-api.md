---
title: Protocol Codec API
category: interface
order: 8
purpose: The contract for parsing USB request frames, building response frames, and looking an opcode up.
status: active
updated: 2026-09-07
source: middleware/protocol/include/protocol.h
confidence: confirmed
keywords: protocol.h, protocol_parser_feed, protocol_parser_reset, protocol_rsp_build, protocol_cmd_lookup, protocol_status_str, protocol_round4, protocol_parser_t, protocol_req_t, protocol_cmd_info_t
---

# Protocol Codec API

> A byte-fed request parser, a response builder, and the opcode map. No SDK
> dependency beyond the ROM CRC, so the whole thing runs on a host.

## Responsibility

`middleware/protocol` owns the wire format and nothing else: it neither knows
what a command means nor how bytes reach it. That is what makes it the one part
of the channel the host suite can exercise completely.

## Types

| Type | Role |
|------|------|
| `protocol_status_t` | The wire status, `0` and `-1`..`-7`. See [../data/usb-frame-format.md](../data/usb-frame-format.md) |
| `protocol_cmd_kind_t` | `PROTOCOL_CMD_SERVED` or `PROTOCOL_CMD_UNSUPPORTED` |
| `protocol_cmd_info_t` | One map row: `command`, `req_len`, `kind` |
| `protocol_req_t` | An accepted request: `command`, `len`, `data` |
| `protocol_parser_t` | Caller-allocated parser, `PROTOCOL_MAX_FRAME` + two lengths |

`req_len` is the exact REQ `LENGTH` a command takes, or `PROTOCOL_LEN_ANY`
(`UINT32_MAX`) when only the handler can judge it.

`protocol_req_t.data` points **into the parser** and is valid only until the
next `protocol_parser_feed()` on that instance. A handler needing the bytes
longer copies them.

## Signatures

```c
static inline uint32_t protocol_round4(uint32_t n);
const char *protocol_status_str(protocol_status_t status);
const protocol_cmd_info_t *protocol_cmd_lookup(uint32_t command);
void protocol_parser_reset(protocol_parser_t *parser);
bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_req_t *out_req);
uint32_t protocol_rsp_build(uint8_t *out, uint32_t out_cap, uint32_t command,
                            protocol_status_t status, const uint8_t *payload,
                            uint32_t payload_len);
```

| Function | Returns | Notes |
|----------|---------|-------|
| `protocol_round4` | `n` rounded up to a multiple of 4 | header-inline; used by tests and by any host tool |
| `protocol_status_str` | a string literal, never NULL | unknown values give `"PROTOCOL_ERR_UNKNOWN"`; no `default:` label, so `-Wswitch-enum` fails the build when a status is added without a name |
| `protocol_cmd_lookup` | the row, or **NULL** | NULL means the spec never defined this opcode, which the caller answers `-2`. A row whose `kind` is `UNSUPPORTED` is answered `-7` |
| `protocol_parser_reset` | — | clears the two lengths only; the 32 KB buffer is not wiped, because nothing reads past `len` |
| `protocol_parser_feed` | `true` when this byte completed a CRC-valid request | one caller per instance |
| `protocol_rsp_build` | bytes written, or **0** | 0 means an impossible argument or a buffer too small, and `out` is left untouched, so a caller can send nothing rather than a truncated frame |

## Parser behaviour

The receiver never trusts `LENGTH` blindly:

1. Hunt for `PROTOCOL_HDR_REQ`, sliding one byte at a time over anything else.
2. Read COMMAND and LENGTH. `LENGTH > PROTOCOL_MAX_DATA` abandons the frame and
   resumes hunting **from the byte after the failed header** — not from behind
   the frame that header described, or one corrupt length would swallow every
   frame behind it.
3. Collect `round4(n)` DATA bytes plus the CRC.
4. Verify the CRC over HEADER..DATA, padding included. A mismatch drops the
   frame **with no response** and resumes the hunt the same way.

A rejected candidate is resynced with **one forward scan and one move**, not a
shift per byte: a 32 KB frame failing its CRC would otherwise cost a 32 KB
`memmove` for every byte behind it, which is a denial of service a host can
trigger with garbage.

An accepted frame is left in the buffer so `req.data` stays readable; the next
feed drops it and re-examines whatever followed, so a second request already in
the buffer surfaces one byte later at worst.

## CRC

`esp_rom_crc32_le(0, buf, len)` on target — the same call
`middleware/storage` uses. The host suite supplies a **deliberately independent**
bitwise implementation in `test/host/stub/esp_rom_crc.h`, and a known-answer
test pins both to `crc32("123456789") == 0xCBF43926`. Without that vector a
wrong CRC would agree with itself and pass everything.

## See also

- [../data/usb-frame-format.md](../data/usb-frame-format.md) — the format itself
- [command-map.md](command-map.md) — every opcode and its verdict
- [../rule/testing.md](../rule/testing.md) — how the host suite runs
