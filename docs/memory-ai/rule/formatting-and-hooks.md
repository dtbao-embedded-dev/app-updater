---
title: Formatting and Git Hooks
category: rule
order: 2
purpose: How code layout is decided and enforced, and how to install the hook that enforces it.
status: active
updated: 2026-09-06
source: .clang-format, docs/.githooks/pre-commit, docs/scripts/tool-esp.py
confidence: confirmed
keywords: clang-format, pre-commit, core.hooksPath, InsertBraces, BreakBeforeBraces, format --check
---

# Formatting and Git Hooks

> `clang-format` decides layout, the config file at the repo root is the rule, and a hook stops an unformatted commit reaching review.

## When this applies

Before every commit that touches a `.c` or `.h` file, and whenever a layout
question comes up in review.

## The rule

1. **Never hand-format.** Run `python docs/scripts/tool-esp.py format`. Layout is
   not a review topic; time spent on a brace is time not spent on the logic
   under it.
2. Install the hook once per clone:
   `git config core.hooksPath docs/.githooks`
3. The hook refuses a commit whose **staged** `.c`/`.h` files under
   `application/`, `middleware/` or `driver/` are not clean, and prints the fix
   command. It skips silently when `clang-format` is not on PATH, so a machine
   without it is not blocked — which also means the check is not a guarantee.
4. Do not reformat lines your change does not touch. A whitespace-only diff
   hides the two real lines inside it. A repo-wide reformat is its own commit.
5. `clang-format 15+` is required — the config uses `InsertBraces`, which
   younger versions reject.

## The settled values

4-space indent, never a tab. 100 columns. Attached braces, always present even
on a one-line body. Pointer binds to the name. Case labels indented one level.
Consecutive assignments and trailing comments aligned by the tool.

`SortIncludes` is **off on purpose**: the include order is own header, project,
SDK, C standard, separated by blank lines, and that grouping is meaningful — the
module's own header first is the cheapest proof it is self-contained.

## Example

```text
python docs/scripts/tool-esp.py format          # rewrite in place
python docs/scripts/tool-esp.py format --check  # report only, what the hook runs
```

The two share one file-selection rule, so a clean local format and a passing
hook can never disagree.

## See also

- [../interface/tool-esp-cli.md](../interface/tool-esp-cli.md) — the full command surface
- [coding-standard-source.md](coding-standard-source.md) — the standard behind these values
