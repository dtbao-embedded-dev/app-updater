---
title: Release CLI (tool-release.py)
category: interface
order: 7
purpose: The contract of the release script - what it refuses, what it changes, and the order it does things in.
status: active
updated: 2026-09-06
source: docs/scripts/tool-release.py
confidence: confirmed
keywords: tool-release.py, --dry-run, --summary, --yes, chore(release), release branch, annotated tag, preflight
---

# Release CLI (tool-release.py)

> One command takes a `release/*` branch to a published tag, refusing at the first step that does not add up rather than half-way through.

## Contract

```text
python docs/scripts/tool-release.py <version> [--summary S] [--dry-run] [--yes]
```

| Argument | Meaning |
|----------|---------|
| `version` | The version to cut, `MAJOR.MINOR.PATCH`, with or without a leading `v` |
| `--summary` | One line for the changelog index row and the notes file. Defaults to `Release <version>.` |
| `--dry-run` | Run pre-flight, print the notes file it would write, change nothing |
| `--yes` | Skip the confirmation prompt |

Exit status is 0 on a published release, non-zero on any refusal or failure,
with the reason and a `fix:` line.

## The seven phases

| # | Phase | Gate |
|---|-------|------|
| 0 | Pre-flight | Everything below. Nothing is written until all of it passes. |
| 1 | Files | Writes `docs/CHANGELOG/v<version>.md`, empties `[Unreleased]`, adds the index row, sets `VERSION` and both README copies |
| 2 | Commit + push | `chore(release): v<version>` onto the current branch |
| 3 | Wait for CI | A red release branch is not a release |
| 4 | PR into `developing` | Created and merged |
| 5 | PR into `main` | Created and merged — `main` accepts nothing else |
| 6 | Tag + publish | Annotated tag on the release commit, pushed, then `release.yml` is watched |

## What pre-flight refuses

Each of these stops the run before a byte changes, and each names its fix:

- `git` or `gh` missing.
- A version that is not SemVer.
- A current branch that is not `release/*`.
- A dirty working tree — a release commit must carry nothing but the release.
- No upstream, or a branch ahead of or behind it.
- A tag `v<version>` that already exists, locally **or** on origin. A tag never
  moves.
- A `docs/CHANGELOG/v<version>.md` that already exists.
- An empty `[Unreleased]` — there is nothing to release.

## Contract rules

- **The route is `release/*` → `developing` → `main`, not a push to `main`.**
  `main` carries a ruleset with no bypass actor and refuses a direct push;
  R-VER-04 still wants the tag on `main`, so the commit travels by pull
  request and the tag lands once it is an ancestor.
- Pull requests are merged with `--merge`, **never squashed**. A squash would
  leave the release commit out of `main`, and there would be nothing on `main`
  to tag.
- Phase 6 verifies the release commit really is an ancestor of `origin/main`
  before tagging, and refuses with an explanation if it is not.
- The tag is annotated, never lightweight.
- Nothing after phase 2 is undone by the script. Phase 2 prints the
  `git reset --hard HEAD~1` that undoes it while the push has not happened.
- Re-running after a partial failure is safe: an existing open pull request is
  reused rather than duplicated, and the tag checks stop a second attempt at a
  version that already shipped.

## See also

- [../rule/versioning-and-release.md](../rule/versioning-and-release.md) — the procedure this automates
- [../architecture/ci-pipeline.md](../architecture/ci-pipeline.md) — the two workflows it waits on
