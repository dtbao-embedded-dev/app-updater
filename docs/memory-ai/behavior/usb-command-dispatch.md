---
title: USB Command Dispatch
category: behavior
order: 6
purpose: The order the dispatcher decides things in, and what each served handler does.
status: active
updated: 2026-09-07
source: middleware/command/src/command.c, middleware/command/include/command.h, middleware/command/test/test_command.c
confidence: confirmed
keywords: command_on_frame, command_init, command_deinit, serve, handle_restart_app, handle_set_boot_slot, handle_get_version, handle_get_boot_slot, handle_get_mac, command_reply_cb_t, command_busy_cb_t, COMMAND_RESTART_GRACE_MS
---

# USB Command Dispatch

> One decoded request in, exactly one response out. The order of the checks is
> the contract, not an implementation detail.

## The order, and why it is fixed

`command_on_frame()` decides in this sequence, and stops at the first answer:

1. **Not in the map** → `PROTOCOL_ERR_BAD_CMD` (`-2`). Decided by the opcode
   alone, before anything reads a payload.
2. **In the map but not served** → `PROTOCOL_ERR_UNSUPPORTED` (`-7`). Also
   opcode-only.
3. **`LENGTH` does not match the row's width** → `PROTOCOL_ERR_BAD_LEN`
   (`-3`), and **the handler never runs**.
4. Only now does a handler see the request, and only now can a *value* be
   rejected with `PROTOCOL_ERR_BAD_ARG` (`-4`).

Steps 3 and 4 are separate so a tool knows which half of its request to fix.
Merging them would leave it guessing.

**Every input except a NULL argument gets an answer.** A host that receives
silence cannot tell a refused command from a dead cable — the one exception is
a request whose own CRC failed, which the parser drops before the dispatcher
ever sees it.

## Shape

```c
typedef fw_err_t (*command_reply_cb_t)(void *ctx, const uint8_t *data, size_t len);
typedef bool (*command_busy_cb_t)(void *ctx);

fw_err_t command_init(command_t *cmd, const command_cfg_t *cfg);
fw_err_t command_deinit(command_t *cmd);
fw_err_t command_on_frame(command_t *cmd, const protocol_req_t *req);
```

`command_cfg_t` carries `on_reply` (required), `reply_ctx`, `is_busy`
(optional; NULL means never busy) and `busy_ctx`.

`is_busy` is a **callback rather than an `updater_t *`** for a layering reason:
the thing that knows whether a slot is already being written is the update cycle
in `application/`, and `middleware/command` may not include a header from a
layer above it (R-LAY-01). The same pattern `ota_http` uses to report progress
upward without knowing who it notifies.

`command_t` carries the response frame buffer — the second of the channel's two
`PROTOCOL_MAX_FRAME` buffers — and the upgrade session. A response body is built
straight into that frame rather than into a third buffer.

## Dispatch

One **flat switch** over the ten served opcodes, not the two-level
range-then-item switch the source spec describes. At ten cases the extra level
is ceremony: the compiler builds the same jump table either way, and a reader
looking up `0x0202` finds it in one place. The `default:` label logs and answers
`-7`, and is unreachable — the map already said the opcode is served, so
reaching it means a row was added without a case.

## The handlers

| Opcode | What it does |
|--------|--------------|
| `0x0006` PING | Points the reply builder at the request's own bytes, so they are copied once, straight to the wire. Refuses a payload past `PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN` with `-3` (see the gap below) |
| `0x0001` RESTART_APP | Replies `PROTOCOL_OK` **first**, logs, waits `COMMAND_RESTART_GRACE_MS` (100 ms), then `esp_restart()`. Returns the never-transmitted `PROTOCOL_ERR_CRC` as an internal "already answered", so the caller does not send a second reply |
| `0x0101` Set BOOT_SLOT | Rejects a slot that is neither 0 nor 1 with `-4`; otherwise `esp_ota_set_boot_partition()`. A failure is `-5`, not `-6`: a slot whose image does not validate is a state a host can fix by transferring one |
| `0x0201` VERSION | `[0:16]` from `esp_app_get_description()`, `[16:32]` from `esp_ota_get_partition_description()` on the `app_firmware` slot. A slot with no image leaves **sixteen zero bytes and answers OK** — "unset" is an answer, not a failure, so no second command is needed to tell them apart |
| `0x0202` Get BOOT_SLOT | Maps the running partition's subtype to the wire encoding. Neither OTA slot is `-6`, which no `partitions.csv` in this repo can produce |
| `0x0203`/`0x0204` | `esp_read_mac()` for `ESP_MAC_WIFI_STA` / `ESP_MAC_BT`; a failure is `-6` |
| `0x0601`/`0x0602`/`0x0603` | Routed to [usb-upgrade-session.md](usb-upgrade-session.md) |

**Set and Get BOOT_SLOT are asymmetric on purpose.** Set writes `otadata` for
the next boot; Get reports what is running now. A Set followed by a Get reads
the old value until the unit restarts, so a tool must say "will boot X after
restart" rather than read it back.

`command_deinit()` aborts an open transfer rather than finalising it: a slot
half written is a slot nothing should boot.

## The RESTART_APP ordering, and how it is proven

Replying before resetting is the whole contract — a host that never sees the OK
cannot tell "restarting" from "the cable fell out". Flushed is not the same as
read, so the 100 ms covers the host still having to be scheduled; a 16-byte
frame is gone in well under a millisecond, so the wait is generous by two orders
of magnitude and costs nothing before a reboot.

On target `esp_restart()` never returns, so the only place this can be checked
is the host suite: the fake counts resets instead of performing one, and the test
asserts the reply was captured while that count was still zero.

## The gap in the source spec

It says PING echoes 0 to `PROTOCOL_MAX_DATA` bytes **and** that the reply
carries `4 + REQ.LENGTH` — which at the cap asks for a reply of 32776 bytes,
past that same cap. The real ceiling is
`PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN` = **32768**; 32769..32772 answer `-3`
rather than a truncated echo. `UPG_WRITE` is unaffected, because its reply
carries a status and nothing else — which is exactly why `PROTOCOL_MAX_DATA` is
32772 rather than 32768.

## See also

- [../interface/command-map.md](../interface/command-map.md) — every opcode
- [usb-upgrade-session.md](usb-upgrade-session.md) — the `0x06` range
- [usb-host-flow.md](usb-host-flow.md) — the sequence a host runs
