---
title: USB Command Dispatch
category: behavior
order: 6
purpose: The order the dispatcher decides things in, and what each served handler does.
status: active
updated: 2026-09-07
source: middleware/command/src/command.c, middleware/command/src/command_dump.c, middleware/command/src/command_priv.h, middleware/command/include/command.h, middleware/command/test/test_command.c
confidence: confirmed
keywords: command_on_frame, command_init, command_deinit, serve, handle_restart_app, handle_set_boot_slot, handle_get_version, handle_get_boot_slot, handle_get_mac, command_dump_info, command_dump_read, command_dump_erase, PAYLOAD_MAX, echo, PROTOCOL_DUMP_CHUNK_MAX, command_reply_cb_t, command_busy_cb_t, COMMAND_RESTART_GRACE_MS
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
`PROTOCOL_MAX_FRAME` buffers — the upgrade session, and one 4096-byte staging
buffer for `DUMP_READ`. A response body is built straight into the frame rather
than into a third buffer; the staging buffer exists for the one answer too big
to pass through the small payload buffer at all (see below).

## Dispatch

One **flat switch** over the thirteen served opcodes, not the two-level
range-then-item switch the source spec describes. At thirteen cases the extra
level is ceremony: the compiler builds the same jump table either way, and a reader
looking up `0x0202` finds it in one place. The `default:` label logs and answers
`-7`, and is unreachable — the map already said the opcode is served, so
reaching it means a row was added without a case.

## The handlers

| Opcode | What it does |
|--------|--------------|
| `0x0006` PING | Points the reply builder at the request's own bytes, so they are copied once, straight to the wire. Refuses a payload past `PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN` with `-3` (see the gap below) |
| `0x0001` RESTART_APP | Replies `PROTOCOL_OK` **first**, logs, then `bsp_restart(COMMAND_RESTART_GRACE_MS)`, which waits 100 ms and resets. Returns the never-transmitted `PROTOCOL_ERR_CRC` as an internal "already answered", so the caller does not send a second reply |
| `0x0101` Set BOOT_SLOT | Rejects a slot that is neither 0 nor 1 with `-4`; otherwise `ota_boot_slot_set()`. A failure is `-5`, not `-6`: a slot whose image does not validate is a state a host can fix by transferring one |
| `0x0201` VERSION | `[0:16]` from `ota_running_version_get()`, `[16:32]` from `ota_slot_version_get(PROTOCOL_SLOT_FIRMWARE, ...)`. Both write straight into the reply field, bounded by its width. A slot with no image leaves **sixteen zero bytes and answers OK** — "unset" is an answer, not a failure, so no second command is needed to tell them apart |
| `0x0202` Get BOOT_SLOT | Maps the running partition's subtype to the wire encoding. Neither OTA slot is `-6`, which no `partitions.csv` in this repo can produce |
| `0x0203`/`0x0204` | `bsp_mac_get()` for `BSP_MAC_WIFI` / `BSP_MAC_BLE`; a failure is `-6` |
| `0x0601`/`0x0602`/`0x0603` | Routed to [usb-upgrade-session.md](usb-upgrade-session.md) |
| `0x0701` DUMP_INFO | `coredump_info_get()` into `[state:1][rsv:3][size:4]`. An **absent** dump is `state 0` with status `0` — an answer, not a failure, the same doctrine as an unset version above. A driver failure is `-6` |
| `0x0702` DUMP_READ | `coredump_read(offset, cmd->chunk, len)`, then points `echo` at `cmd->chunk`. `len` of zero or over `PROTOCOL_DUMP_CHUNK_MAX` is `-4`, a range past the stored dump is `-4`, no dump at all is `-5`, flash refusing is `-6`. A dump failing its checksum **still reads** |
| `0x0703` DUMP_ERASE | `coredump_erase()`. Succeeds even with nothing to erase, so a host's read-then-erase is safe to retry after a lost reply; `-6` on failure |

**Set and Get BOOT_SLOT are asymmetric on purpose.** Set writes `otadata` for
the next boot; Get reports what is running now. A Set followed by a Get reads
the old value until the unit restarts, so a tool must say "will boot X after
restart" rather than read it back.

`command_deinit()` aborts an open transfer rather than finalising it: a slot
half written is a slot nothing should boot.

## The one answer too big for the payload buffer

`PAYLOAD_MAX` is 32 bytes — `PROTOCOL_VERSION_LEN`, the widest reply any of the
small handlers produces — and it is a **stack** array in `command_on_frame()`,
inside a task whose stack is 4096 (`APP_USB_TASK_STACK`). So a 4 KB `DUMP_READ`
answer cannot go through it, and enlarging it would overflow the task stack
rather than fix anything.

The route that already existed is `echo`: a handler sets `*echo` to bytes it
does not own, and `reply()` copies once from there straight into `cmd->frame`.
PING points it at the parser's own buffer. That buffer is only valid until the
next `protocol_parser_feed()`, so `DUMP_READ` cannot borrow it — it needs a home
that outlives `serve()`, which is why `command_t` carries `chunk`.

**That is also why the read chunk is a flash sector and not the upgrade band.**
`PROTOCOL_DUMP_CHUNK_MAX` at 4096 costs 4096 bytes of `.bss` for a 64 KB dump
read once in a unit's life; the 32768 of `PROTOCOL_UPG_CHUNK_CAP` would cost
eight times that to save fourteen round-trips. Measured: `.bss` is 75 504 bytes,
22.09 % of DRAM, and the buffer is 4096 of it.

**The tests were proved to have teeth by mutation, not assumed.** Reading from
offset 0 instead of the requested offset left two of the three content
assertions green, because the first fill pattern repeated with period 256 and
every offset under test was a multiple of 256. The pattern now folds in the
high byte of the index, and the same mutation turns all three red.


## The RESTART_APP ordering, and how it is proven

Replying before resetting is the whole contract — a host that never sees the OK
cannot tell "restarting" from "the cable fell out". Flushed is not the same as
read, so the 100 ms covers the host still having to be scheduled; a 16-byte
frame is gone in well under a millisecond, so the wait is generous by two orders
of magnitude and costs nothing before a reboot.

On target `bsp_restart()` never returns, so the only place this can be checked
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
