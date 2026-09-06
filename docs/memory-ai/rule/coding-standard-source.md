---
title: Where the R-XXX-nn Rules Come From
category: rule
order: 1
purpose: How to resolve a rule ID cited in this repo's comments, and what outranks what.
status: active
updated: 2026-09-06
source: conversation, E:/Baotd/docs/embedded-spec
confidence: confirmed
keywords: R-RPO, R-LAY, R-ERR, R-LFC, R-CFG, R-BLD, R-VER, R-FMT, embedded-spec, emb-dtbao, spec_doc, spec-verify
---

# Where the R-XXX-nn Rules Come From

> Every `R-XXX-nn` in this repo's comments cites the house embedded-C standard; resolve it there before changing a shape it mandates.

## When this applies

Any time you read a comment like `(R-RPO-10)` or `SPEC-DEVIATION(R-VER-08)` in
this repo, or are about to change a file layout, a header shape, an error code,
or a lifecycle signature.

## The rule

1. The standard lives at `E:\Baotd\docs\embedded-spec`, published as the Claude
   Code plugin `emb-dtbao`. Its 34 docs are under `spec/`, one file per family,
   named `<code>-<slug>.md`.
2. A citation `R-<CODE>-<nn>` resolves to rule `nn` inside `spec/<code>-*.md`.
   The families this repo cites most: `RPO` (repo layout), `LAY` (layering),
   `MOD` (module boundaries), `NAM` (naming), `HDR`/`SRC` (file contracts),
   `LFC` (lifecycle), `ERR` (error model), `CFG` (config and NVS), `BLD`
   (build), `VER` (versioning), `FMT` (formatting), `LOG` (logging), `TST`
   (tests), `DOX` (comments).
3. When the plugin is loaded, prefer its tools over reading files: `spec_list`
   for the map, `spec_search` for a topic, `spec_rule` to cite one, `spec_doc`
   for a whole doc **including its skeleton**.
4. **Never write a file skeleton from memory.** Header and source section order,
   the banner comments, and the changelog shape all live inside the doc that
   mandates them. Render that, not a recollection of it.
5. Precedence when they disagree: this repo's existing convention beats the
   default shape; the project's own `CLAUDE.md` beats the global one.
6. A deliberate departure is written as `SPEC-DEVIATION(R-XXX-nn)` in the source
   **and** listed in [known-deviations.md](known-deviations.md). An undocumented
   departure is a defect.

## Example

The comment in `application/app/CMakeLists.txt` explaining why there is no
`main` component cites `R-RPO-10`. To change that arrangement, read
`spec/rpo-repo-layout.md` rule 10 first — it is what forbids a source file of
ours under `workspace/`.

## Not yet done

The `spec-verify` skill has **never been run** against this repo. It is the
check that every generated file matches the standard, and it will find at least
the deviations already listed. Run it before the first release.

## See also

- [known-deviations.md](known-deviations.md) — where this repo departs on purpose
- [formatting-and-hooks.md](formatting-and-hooks.md) — the one rule that is machine-enforced today
