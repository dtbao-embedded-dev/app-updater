---
title: Config Load and Save
category: behavior
order: 4
purpose: How the persisted record is validated on read and how a write avoids wearing the flash out.
status: active
updated: 2026-09-06
source: middleware/storage/src/storage.c:103-174, middleware/storage/src/storage.c:176-207
confidence: confirmed
keywords: storage_record_load, storage_record_save, record_validate, record_crc, read-compare-write, nvs_set_blob, nvs_commit, esp_rom_crc32_le
---

# Config Load and Save

> Every read is validated before it is believed, and every write is skipped unless the bytes actually changed.

## Load

1. Read the blob at the configured key into a local record.
2. "Not found" is returned as such, distinctly from a failure — the caller uses
   it to mean "first boot".
3. Any other vendor failure is logged and converted.
4. Validate: stored length, CRC, record version, and that the URL string is
   NUL-terminated inside its buffer. See
   [../data/storage-record.md](../data/storage-record.md) for the verdict table.
5. Only on a clean verdict is the caller's output written. A failed validation
   leaves the caller's buffer untouched.

The CRC covers every byte before the trailing CRC field, using the ROM CRC-32
routine seeded with zero so the result is the standard CRC-32 of the buffer.

## Save

1. Copy the caller's record, then **overwrite** its version, length and CRC. The
   caller cannot set them wrong.
2. **Read back the current record and compare.** If the stored bytes already
   equal what is about to be written, return success without touching flash.
3. Otherwise write the blob and commit.

Step 2 is what makes the function safe to call every cycle. NVS wear is measured
in sector erases; the compare is cheap and the erase is not. Without it, a
caller saving on every loop iteration wears a sector out in weeks.

Power-fail safety is delegated: NVS commits the new copy before dropping the old
one, so a cut here leaves the previous record readable. The module does not
implement its own two-slot scheme.

## Inputs → outputs

| Reads | Produces |
|-------|----------|
| NVS blob at the configured namespace/key | A validated record, or a verdict |
| The caller's record | A stamped, CRC-correct blob — or nothing, if unchanged |

## Edge cases & error handling

- A record written by a **newer** firmware (unknown version) is reported as
  "not found" rather than reinterpreted, so the device falls back to defaults
  instead of guessing at a layout it does not know.
- 🔴 **Open hole (code, not knowledge):** there is no migration. `record_validate()` carries the TODO. Today,
  changing the struct silently costs every deployed unit its stored settings.
- The compare in step 2 uses the load path, so a **damaged** stored record makes
  the compare fail and the write proceed — which is the right repair behaviour.
- Both calls block on flash and are not callable from an ISR.

## See also

- [../data/storage-record.md](../data/storage-record.md) — the shape and its invariants
- [../interface/storage-api.md](../interface/storage-api.md) — the contract
