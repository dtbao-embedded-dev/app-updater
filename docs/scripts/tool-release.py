#!/usr/bin/env python3
"""Cut a release, from a release/* branch all the way to a published tag.

    python docs/scripts/tool-release.py 0.1.0 --summary "First scaffold"
    python docs/scripts/tool-release.py 0.1.0 --dry-run     # rehearse, change nothing

Seven phases, each gated on the one before it:

    0  pre-flight        every check that can refuse before anything is touched
    1  files             docs/CHANGELOG/v<version>.md, CHANGELOG.md, VERSION, README
    2  commit + push     chore(release): v<version>, onto the current release/* branch
    3  wait for CI       a red release branch is not a release
    4  PR -> developing  created and merged
    5  PR -> main        created and merged; main only accepts a pull request
    6  tag + publish     annotated tag on main, pushed, then watch release.yml

Why the two pull requests rather than a push to main: `main` carries a ruleset
with no bypass actor, so a direct push is refused (R-VER-04 still wants the tag
on main). Work therefore travels release/* -> developing -> main, and the tag
lands on the release commit once it is an ancestor of main.

Nothing after phase 2 is undoable by this script. Phase 2 prints how to undo
itself if a later phase fails.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REPO_URL = "https://github.com/dtbao-embedded-dev/app-updater"

CHANGELOG = REPO / "CHANGELOG.md"
VERSION_FILE = REPO / "VERSION"
README = REPO / "README.md"
RELEASE_DIR = REPO / "docs" / "CHANGELOG"

SEMVER = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$")

# The paragraph the first release makes untrue. Dropped when it is still there.
NOTHING_RELEASED = (
    "**Nothing is released yet.** There is no git tag and no `docs/CHANGELOG/`\n"
    "directory; both appear with the first release.\n\n"
)


# ---------------------------------------------------------------- helpers ---

class Abort(SystemExit):
    """A refusal with a reason the caller can act on."""

    def __init__(self, message: str, fix: str = "") -> None:
        text = f"\nrelease aborted: {message}"
        if fix:
            text += f"\n  fix: {fix}"
        super().__init__(text)


def run(*args: str, capture: bool = True, check: bool = True) -> str:
    """Run a command, returning its stdout."""
    proc = subprocess.run(args, capture_output=capture, text=True)
    if check and proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip()
        raise Abort(f"`{' '.join(args)}` failed\n  {detail}")
    return (proc.stdout or "").strip()


def git(*args: str, **kw) -> str:
    return run("git", "-C", str(REPO), *args, **kw)


def gh(*args: str, **kw) -> str:
    return run("gh", *args, **kw)


def step(n: int, title: str) -> None:
    print(f"\n[{n}] {title}", flush=True)


def confirm(question: str, assume_yes: bool) -> None:
    if assume_yes:
        return
    if input(f"{question} [y/N] ").strip().lower() not in ("y", "yes"):
        raise Abort("declined at the prompt")


# ------------------------------------------------------- 0. pre-flight ---

def unreleased_body(text: str) -> str:
    """The entries under [Unreleased], or '' when there are none."""
    match = re.search(r"^## \[Unreleased\]\s*\n(.*?)(?=^## |^\[Unreleased\]:)",
                      text, re.S | re.M)
    return match.group(1).strip() if match else ""


def preflight(version: str) -> tuple[str, str]:
    """Every refusal that can happen before a single byte changes."""
    step(0, "pre-flight")

    for tool in ("git", "gh"):
        if shutil.which(tool) is None:
            raise Abort(f"{tool} is not on PATH")

    if not SEMVER.match(version):
        raise Abort(f"{version!r} is not SemVer", "use MAJOR.MINOR.PATCH")

    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    if not branch.startswith("release/"):
        raise Abort(
            f"on branch {branch!r}; a release is cut from a release/* branch",
            f"git switch -c release/v{version}")

    if git("status", "--porcelain"):
        raise Abort("the working tree is dirty",
                    "commit or stash first - a release commit must carry "
                    "nothing but the release")

    git("fetch", "--quiet", "origin", capture=False)

    upstream = git("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}",
                   check=False)
    if not upstream:
        raise Abort(f"{branch} has no upstream",
                    f"git push -u origin {branch}")
    ahead_behind = git("rev-list", "--left-right", "--count", f"{upstream}...HEAD")
    behind, ahead = (int(x) for x in ahead_behind.split())
    if behind or ahead:
        raise Abort(f"{branch} is {ahead} ahead / {behind} behind {upstream}",
                    "push or pull until they match")

    tag = f"v{version}"
    if git("tag", "--list", tag):
        raise Abort(f"tag {tag} already exists locally",
                    "a tag never moves - pick the next version")
    if git("ls-remote", "--tags", "origin", tag):
        raise Abort(f"tag {tag} already exists on origin",
                    "a tag never moves - pick the next version")

    notes = RELEASE_DIR / f"{tag}.md"
    if notes.exists():
        raise Abort(f"{notes.relative_to(REPO)} already exists")

    body = unreleased_body(CHANGELOG.read_text(encoding="utf-8"))
    if not body:
        raise Abort("[Unreleased] is empty - there is nothing to release",
                    "write the entry the day the change is made (R-VER-05)")

    print(f"    branch   {branch} (in sync with {upstream})")
    print(f"    version  {VERSION_FILE.read_text().strip()} -> {version}")
    print(f"    tag      {tag} (free, locally and on origin)")
    print(f"    notes    {len(body.splitlines())} line(s) under [Unreleased]")
    return branch, body


# ----------------------------------------------------------- 1. files ---

def write_release_notes(version: str, body: str, date: str, summary: str) -> Path:
    RELEASE_DIR.mkdir(parents=True, exist_ok=True)
    notes = RELEASE_DIR / f"v{version}.md"
    notes.write_text(f"# {version} — {date}\n\n{summary}\n\n{body}\n", encoding="utf-8")
    return notes


def rewrite_changelog(version: str, date: str, summary: str) -> None:
    """Empty [Unreleased], add the index row, repoint the compare link."""
    text = CHANGELOG.read_text(encoding="utf-8")
    text = text.replace(NOTHING_RELEASED, "")

    text = re.sub(r"^## \[Unreleased\]\s*\n.*?(?=^## |^\[Unreleased\]:)",
                  "## [Unreleased]\n\n_Nothing yet._\n\n",
                  text, count=1, flags=re.S | re.M)

    row = f"| [{version}](docs/CHANGELOG/v{version}.md) | {date} | {summary} |\n"
    header = "| Version | Date | Summary |\n|---------|------|---------|\n"
    if header in text:
        text = text.replace(header, header + row, 1)
    else:
        # First release: the index table does not exist yet.
        section = ("## Released\n\nNewest first. One file per version.\n\n"
                   + header + row + "\n")
        text = re.sub(r"(?=^\[Unreleased\]:)", section, text, count=1, flags=re.M)

    text = re.sub(r"^\[Unreleased\]: .*$",
                  f"[Unreleased]: {REPO_URL}/compare/v{version}...main",
                  text, count=1, flags=re.M)
    text += f"[{version}]: {REPO_URL}/releases/tag/v{version}\n"
    CHANGELOG.write_text(text, encoding="utf-8")


def rewrite_version_copies(version: str) -> None:
    """VERSION is the single source; every other copy is derived (R-VER-01)."""
    VERSION_FILE.write_text(f"{version}\n", encoding="utf-8")

    text = README.read_text(encoding="utf-8")
    text = re.sub(r"/badge/version-[^-]+-blue",
                  f"/badge/version-{version}-blue", text, count=1)
    text = re.sub(
        r"^- \*\*Version\*\*: .*?are why\.$",
        f"- **Version**: {version} — see [CHANGELOG.md](CHANGELOG.md)",
        text, count=1, flags=re.S | re.M)
    text = re.sub(r"^- \*\*Version\*\*: [0-9][^ ]* —",
                  f"- **Version**: {version} —", text, count=1, flags=re.M)
    README.write_text(text, encoding="utf-8")


# ------------------------------------------------- 3. wait for a run ---

def wait_for_run(workflow: str, sha: str, what: str) -> None:
    """Block until the workflow run for `sha` finishes; abort when it fails."""
    print(f"    waiting for {what} on {sha[:8]} ...", flush=True)
    run_id = ""
    for _ in range(60):                       # ~5 minutes for the run to appear
        rows = json.loads(gh("run", "list", "--workflow", workflow, "--limit", "20",
                             "--json", "databaseId,headSha,status"))
        hit = next((r for r in rows if r["headSha"] == sha), None)
        if hit:
            run_id = str(hit["databaseId"])
            break
        time.sleep(5)
    if not run_id:
        raise Abort(f"no {workflow} run appeared for {sha[:8]}",
                    "check the Actions tab; the workflow trigger may not match")

    print(f"    run {run_id}: {REPO_URL}/actions/runs/{run_id}", flush=True)
    failed = subprocess.run(["gh", "run", "watch", run_id, "--exit-status",
                             "--interval", "15"],
                            capture_output=True, text=True).returncode != 0
    jobs = json.loads(gh("run", "view", run_id, "--json", "jobs"))["jobs"]
    for job in jobs:
        print(f"      {job['conclusion'] or job['status']:<10} {job['name']}")
    if failed:
        raise Abort(f"{what} failed", f"{REPO_URL}/actions/runs/{run_id}")
    print(f"    {what}: pass")


# ----------------------------------------------- 4/5. pull requests ---

def merge_pr(head: str, base: str, version: str) -> None:
    """Open a pull request and merge it, keeping the release commit in history."""
    existing = gh("pr", "list", "--head", head, "--base", base,
                  "--state", "open", "--json", "number", "--jq", ".[0].number")
    if existing:
        print(f"    reusing open PR #{existing}")
    else:
        gh("pr", "create", "--head", head, "--base", base,
           "--title", f"chore(release): v{version} ({head} -> {base})",
           "--body", f"Release v{version}.\n\nNotes: "
                     f"`docs/CHANGELOG/v{version}.md`.")
        print(f"    opened PR {head} -> {base}")
    # --merge, never --squash: the tag has to land on the release commit, and a
    # squash would leave that commit out of main entirely.
    gh("pr", "merge", head, "--merge", "--delete-branch=false")
    print(f"    merged {head} -> {base}")


# ------------------------------------------------------------- main ---

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("version", help="the version to cut, e.g. 0.1.0")
    parser.add_argument("--summary", default="",
                        help="one line for the changelog index row")
    parser.add_argument("--dry-run", action="store_true",
                        help="run pre-flight and print the plan, change nothing")
    parser.add_argument("--yes", action="store_true", help="skip the prompt")
    args = parser.parse_args()

    version = args.version.lstrip("v")
    tag = f"v{version}"
    date = dt.date.today().isoformat()
    branch, body = preflight(version)
    summary = args.summary or f"Release {version}."

    if args.dry_run:
        print(f"\n--- docs/CHANGELOG/{tag}.md would be ---\n")
        print(f"# {version} — {date}\n\n{summary}\n\n{body}")
        print(f"\ndry run: nothing written. Drop --dry-run to cut {tag}.")
        return 0

    print(f"\nAbout to cut {tag} from {branch}: commit, push, wait for CI, "
          f"merge into developing, then into main, tag, and publish.")
    confirm("Proceed?", args.yes)

    step(1, "files")
    notes = write_release_notes(version, body, date, summary)
    rewrite_changelog(version, date, summary)
    rewrite_version_copies(version)
    for path in (notes, CHANGELOG, VERSION_FILE, README):
        print(f"    {path.relative_to(REPO)}")

    step(2, f"commit and push to {branch}")
    git("add", str(notes.relative_to(REPO)), "CHANGELOG.md", "VERSION", "README.md")
    git("commit", "-m", f"chore(release): {tag}",
        "-m", f"Moves the [Unreleased] entries into docs/CHANGELOG/{tag}.md and "
              f"points VERSION and the README copies at {version}.\n\n"
              f"The bump is its own commit so `git log` can answer what shipped "
              f"in {version} with a range (R-VER-12).")
    sha = git("rev-parse", "HEAD")
    print(f"    {sha[:8]} chore(release): {tag}")
    print(f"    undo, if a later phase fails: "
          f"git reset --hard HEAD~1 (before pushing)")
    git("push", "origin", branch, capture=False)

    step(3, "wait for CI on the release branch")
    wait_for_run("CI", sha, "CI")

    step(4, f"pull request {branch} -> developing")
    merge_pr(branch, "developing", version)

    step(5, "pull request developing -> main")
    merge_pr("developing", "main", version)

    step(6, f"tag {tag} on main and publish")
    git("fetch", "--quiet", "origin", "main")
    # merge-base --is-ancestor communicates through its exit status, not stdout,
    # so it is the one place the git() helper is the wrong tool.
    ancestor = subprocess.run(
        ["git", "-C", str(REPO), "merge-base", "--is-ancestor", sha, "origin/main"],
        capture_output=True).returncode == 0
    if not ancestor:
        raise Abort(
            f"{sha[:8]} is not an ancestor of origin/main - it was probably "
            "squash-merged, so the release commit no longer exists there",
            "tag origin/main by hand, or set the repo to merge pull requests "
            "with a merge commit")

    git("tag", "-a", tag, sha, "-m", f"Release {tag}")
    git("push", "origin", tag, capture=False)
    print(f"    tagged {sha[:8]} and pushed {tag}")

    step(7, "wait for the release workflow")
    wait_for_run("Release", sha, "Release")

    print(f"\nreleased {tag}: {REPO_URL}/releases/tag/{tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
