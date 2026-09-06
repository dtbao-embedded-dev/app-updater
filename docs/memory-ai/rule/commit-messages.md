---
title: Commit Messages
category: rule
order: 6
purpose: The shape of a commit message here, and the attribution trailer that must never appear in one.
status: active
updated: 2026-09-06
source: .claude/gitconfig.yml, git log
confidence: confirmed
keywords: Conventional Commits, commit message, trailer, Co-Authored-By, attribution, scope, subject, body
---

# Commit Messages

## Shape

Conventional Commits, one logical change per commit:

```
<type>(<scope>): <subject>

<body: why the change was needed, and what it does not do>
```

- `type` — one of `feat`, `fix`, `docs`, `build`, `ci`, `test`, `chore`.
- `scope` — optional, the area touched (`scripts`, `workspace`, `bsp`, `ci`).
- `subject` — imperative, lower case, no trailing period.
- `body` — prose, wrapped; state what was verified when the change is not
  self-evident (`Verified in espressif/idf:v6.1: build, test and analyse exit 0`).

## No AI attribution trailer

**A commit message must not carry an AI co-author or generation trailer.** In
particular, never append:

```
Co-Authored-By: Claude <...>
🤖 Generated with Claude Code
```

The author of a commit here is the person who reviewed and shipped it. Tooling
that offers such a trailer by default has it switched off; if a default rule
elsewhere asks for one, this rule wins.

History was rewritten once on 2026-09-06 to strip the trailer from the 17
commits that carried it, across `main`, `developing`, `release/v0.1` and tag
`v0.1.0`. Trees were unchanged — messages only.

## Automation

`.claude/gitconfig.yml` drives `skill-support-commit`: it commits, pushes once
when `auto_push` is set, and never auto-pushes `protected_branches`
(`main`, `master`). `main` and `developing` are additionally protected by GitHub
rulesets (no deletion, no force-push; `main` also requires a pull request).
