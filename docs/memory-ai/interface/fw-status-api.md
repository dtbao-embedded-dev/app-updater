---
title: Project Status API (fw)
category: interface
order: 1
purpose: The contract of the project-wide status type and its name lookup.
status: active
updated: 2026-09-06
source: middleware/fw/include/fw.h, middleware/fw/src/fw.c:25-56
confidence: confirmed
keywords: fw.h, fw_err_t, fw_err_str, FW_OK, FW_ERR_IO, fw_err_t codes
---

# Project Status API (fw)

> One enum and one lookup function; no state, no lifecycle, nothing to claim or release.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `fw_err_str` | `const char *fw_err_str(fw_err_t err)` | Maps a status to its constant name | A string literal with static lifetime. Never NULL, never freed by the caller. An unrecognised value yields `"FW_ERR_UNKNOWN"`. |


## Parameters & config

None. This module takes no configuration and holds no instance.

## Contract rules

- `fw_err_str()` is reentrant and callable from any task. It must not be called
  from an ISR, because its only purpose is to feed a log line and logging from
  an ISR is forbidden.
- The returned pointer is a literal in flash; the caller must not free, modify,
  or assume it is unique per call.
- This module has **no** `init` / `deinit`. The five lifecycle verbs apply to
  modules that claim something; this one claims nothing.

## Reproduction note

A rebuild must keep the switch in `fw_err_str()` free of a `default` label, so
the compiler's `-Wswitch-enum` fails the build when a code is added without a
name. The unknown-value fallback lives **after** the switch, not inside it.

## See also

- [../data/error-code-model.md](../data/error-code-model.md) — the values and their meanings
