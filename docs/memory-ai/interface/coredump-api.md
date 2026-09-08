---
title: Core Dump API
category: interface
purpose: The contract driver/coredump offers for the panic record in flash — whether one is there and sound, its bytes, the panic reason as text, and the erase.
status: active
updated: 2026-09-07
source: driver/coredump/include/coredump.h, driver/coredump/src/coredump.c, test/host/fake/coredump_fake.h
confidence: confirmed
keywords: coredump_err_t, coredump_state_t, coredump_info_get, coredump_read, coredump_erase, coredump_reason_get, coredump_err_str, COREDUMP_REASON_MAX, COREDUMP_ABSENT, COREDUMP_VALID, COREDUMP_CORRUPT, esp_core_dump_image_get, esp_core_dump_image_check, esp_core_dump_get_panic_reason, coredump_fake
---

# Core Dump API

> The only module in the repo naming `esp_core_dump_*`. Four calls: what is in the `coredump` partition, its bytes, the panic reason as text, and the erase. No lifecycle — there is nothing to initialise, because the partition is written by the panic handler before the reset and simply found afterwards.

## Why it exists

`espcoredump` writes an ELF core dump into the `coredump` partition from inside
the panic handler, **before** `panic_restart()`. Every consumer of that record
sits in `middleware/` or `application/`, and neither may name a vendor symbol
(R-LAY-01, [../rule/layer-boundaries.md](../rule/layer-boundaries.md)). This
driver is the mapped boundary, the same role `driver/ota` plays for `esp_ota_*`.

Unusually for this repo, it has **no `init`/`deinit` pair and no instance
struct**. There is no hardware to claim and no state to carry: the partition is
located by subtype on every call. That also means every function is safe to
call from the first line of `app_run()`, before any module is up — which is
exactly where the boot-time panic report needs it.

## Types

| Type | Values | Meaning |
|------|--------|---------|
| `coredump_err_t` | `COREDUMP_OK 0`, `COREDUMP_ERR_PARAM -1`, `COREDUMP_ERR_NOT_FOUND -6`, `COREDUMP_ERR_IO -7` | Driver-local status, generic values per R-ERR-03 so a caller maps them onto `fw_err_t` or `protocol_status_t` one for one. |
| `coredump_state_t` | `COREDUMP_ABSENT 0`, `COREDUMP_VALID 1`, `COREDUMP_CORRUPT 2` | What the partition holds. **These go on the wire unchanged** as the `state` byte of a `DUMP_INFO` response — one encoding for one fact. |

`COREDUMP_REASON_MAX` is `200U`: capacity a panic-reason buffer needs, NUL
included.

**There is deliberately no `COREDUMP_ERR_CORRUPT`.** A dump that fails its
checksum is not a failed call, it is an answer — and its bytes stay readable,
because a dump nobody can checksum is exactly the one worth looking at. The
distinction is carried by `coredump_state_t`, never by an error code.

## Contract

| Function | Returns | Notes |
|----------|---------|-------|
| `const char *coredump_err_str(coredump_err_t)` | a string literal, never NULL | Reentrant; unknown value yields `"COREDUMP_ERR_UNKNOWN"`. |
| `coredump_err_t coredump_info_get(coredump_state_t *out_state, uint32_t *out_size)` | `OK`, `ERR_PARAM` on NULL, `ERR_IO` when the partition is unreachable | `OK` **includes an absent dump** — that is an answer, so a caller needs no second call to tell absence from failure. `*out_size` is the stored length with checksum included, `0` when absent. |
| `coredump_err_t coredump_read(uint32_t offset, void *out, uint32_t len)` | `OK`, `ERR_PARAM` on NULL / zero `len` / a range past the **stored dump**, `ERR_NOT_FOUND` when nothing is stored, `ERR_IO` on a failed read | Does **not** verify the checksum, so a dump that fails it reads back fine. Bounds to the stored length, and that length comes free with the probe deciding `ERR_NOT_FOUND` — which is what lets a chunked read skip `coredump_info_get()` per chunk. |
| `coredump_err_t coredump_erase(void)` | `OK`, `ERR_NOT_FOUND` when the table has no coredump row, `ERR_IO` on failure | `OK` **including when there was nothing to erase**, which is what makes a host's read-then-erase safe to retry after a lost reply. |
| `coredump_err_t coredump_reason_get(char *out, size_t cap)` | `OK`, `ERR_PARAM` on NULL / zero `cap`, `ERR_NOT_FOUND` when nothing is stored or the dump carries no reason, `ERR_IO` on a failed read | Already-readable text — nothing needs symbolising to log it. `out[0]` is set to `'\0'` before anything else, so a failed call never leaves stale bytes. |

Every call blocks on flash and none is callable from an ISR.

## Cost of each call

`coredump_info_get()` is **not** constant-time. `esp_core_dump_image_get()`
reads only the 4-byte length field, but `esp_core_dump_image_check()` re-reads
every stored byte to recompute the SHA256. So the cheap question — is anything
stored — is answered cheaply, and the expensive half runs only once a dump is
actually there. A caller reading a dump in chunks must therefore call
`coredump_info_get()` **once** and then loop on `coredump_read()`, never call
`info_get` per chunk.

`coredump_read()` costs one `esp_partition_read` plus the 4-byte length probe
that decides `ERR_NOT_FOUND` — and that probe is also what bounds the read, so a
caller reading in chunks never has to ask the size itself. `coredump_erase()`
erases every sector of the partition, so it costs a full 64 KB erase.

There is one flavour of corruption whose bytes stay unreachable: a length field
that is neither blank nor plausible. `coredump_info_get()` reports it as
`COREDUMP_CORRUPT` with size `0`, and `coredump_read()` refuses, because nothing
says how much of the partition was written and so no read can be bounded. A
dump with a good length field and a bad checksum reads back normally.

## Build dependency, and the refusal

`driver/coredump/CMakeLists.txt` registers
`PRIV_REQUIRES espcoredump esp_partition`; the component is found through
`EXTRA_COMPONENT_DIRS` in `workspace/0xF001/CMakeLists.txt`, so adding the
directory is the whole registration.

Every `esp_core_dump_*` prototype this driver calls lives behind
`#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` in `esp_core_dump.h`. With that option
off the errors would all read "undeclared function" and name neither the cause
nor the fix, so `coredump.c` carries an `#error` naming the option and the file
to edit. **There is no stub alternative:** a driver that always answered "no
dump" would be a unit that silently never captures a panic.

## Host fake

`test/host/fake/coredump_fake.c` implements this contract in memory — a fake of
**the driver**, not of the SDK, so a test drives the same contract the target
implementation has to satisfy ([../rule/testing.md](../rule/testing.md)).

Control surface: `coredump_fake_reset()`,
`coredump_fake_set_dump(state, bytes, len)`, `coredump_fake_set_reason()`,
`coredump_fake_fail_info/_fail_read/_fail_erase()`,
`coredump_fake_read_count()`, `coredump_fake_erase_count()`,
`coredump_fake_is_erased()`.

`COREDUMP_FAKE_MAX` is `8192U`, deliberately larger than one `DUMP_READ` chunk,
so a test can prove a handler reads across chunk boundaries instead of only ever
answering the first one. `coredump_fake_read_count()` is what proves the
chunking actually happened.

## See also

- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — the 64 KB `coredump` partition this reads
- [command-map.md](command-map.md) — the three opcodes that expose it over USB
- [../rule/layer-boundaries.md](../rule/layer-boundaries.md) — why the vendor call sits here and nowhere above
