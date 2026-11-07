#!/usr/bin/env python3
"""
Refuses a v* tag push while local work is missing from the tag.

Blocks on local branches with commits the tag lacks and no PR covering them, and on
worktrees with uncommitted changes. Only work newer than the previous v* tag counts: older
work was that release's to catch. Branches with an open PR are listed but don't block.
MAGDA_RELEASE_OK=1 pushes anyway.

Usage:
  scripts/release_gate.py --check REF    # dry run against a commit or tag
  (as pre-push hook)                     # installed by `make hooks`, reads git's ref lines
"""

import json
import os
import subprocess
import sys

ZERO_SHA = "0" * 40


def git(*args, cwd=None, check=True):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result


def pushed_tags(lines):
    """(tag, sha) for each v* tag in git's pre-push stdin; deletions are skipped."""
    for line in lines:
        parts = line.split()
        if len(parts) == 4 and parts[2].startswith("refs/tags/v") and parts[1] != ZERO_SHA:
            yield parts[2][len("refs/tags/"):], parts[1]


def pull_requests():
    out = subprocess.run(
        ["gh", "pr", "list", "--state", "all", "--limit", "1000",
         "--json", "number,state,headRefName,headRefOid"],
        capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(f"gh pr list: {out.stderr.strip()}")
    by_branch = {}
    for pr in json.loads(out.stdout):
        by_branch.setdefault(pr["headRefName"], []).append(pr)
    return by_branch


def is_covered_by(tip, pr):
    """A merged or closed PR covers the branch if it holds the branch tip."""
    if pr["state"] not in ("MERGED", "CLOSED"):
        return False
    oid = pr["headRefOid"]
    return tip == oid or git("merge-base", "--is-ancestor", tip, oid, check=False).returncode == 0


def previous_release_time(tag):
    """Creation time of the newest v* tag other than @p tag, or 0."""
    refs = git("for-each-ref", "--sort=-creatordate", "--format=%(refname:short) %(creatordate:unix)",
               "refs/tags/v*").stdout
    for line in refs.splitlines():
        name, created = line.split()
        if name != tag:
            return int(created)
    return 0


def branch_findings(commit, prs, since):
    blocking, open_prs = [], []
    refs = git("for-each-ref", "refs/heads",
               "--format=%(refname:short) %(objectname) %(committerdate:unix)").stdout
    for line in refs.splitlines():
        branch, tip, committed = line.split()
        if int(committed) <= since:
            continue
        if git("rev-list", "--count", f"{commit}..{tip}").stdout.strip() == "0":
            continue
        # Patch ids, so commits cherry-picked into the tag don't count.
        missing = sum(1 for c in git("cherry", commit, tip).stdout.splitlines() if c.startswith("+"))
        if missing == 0:
            continue
        branch_prs = prs.get(branch, [])
        if any(is_covered_by(tip, pr) for pr in branch_prs):
            continue
        open_pr = next((pr for pr in branch_prs if pr["state"] == "OPEN"), None)
        entry = f"{branch}: {missing} commit(s)"
        if open_pr:
            open_prs.append(f"{entry}, open PR #{open_pr['number']}")
        elif branch_prs:
            blocking.append(f"{entry}, newer than its PR #{branch_prs[0]['number']}")
        else:
            blocking.append(f"{entry}, no PR")
    return blocking, open_prs


def dirty_worktrees(since):
    """Worktrees with tracked changes, any of them edited after @p since."""
    found = []
    paths = [l[len("worktree "):] for l in git("worktree", "list", "--porcelain").stdout.splitlines()
             if l.startswith("worktree ")]
    for path in paths:
        if not os.path.isdir(path):
            continue
        status = git("status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all",
                     cwd=path, check=False).stdout
        changed = [l[3:].split(" -> ")[-1] for l in status.splitlines() if l.strip()]
        # A deleted file has no mtime; count it as recent.
        mtimes = [os.path.getmtime(os.path.join(path, f)) if os.path.exists(os.path.join(path, f))
                  else float("inf") for f in changed]
        if changed and max(mtimes) > since:
            branch = git("branch", "--show-current", cwd=path, check=False).stdout.strip() or "detached"
            found.append(f"{path} ({branch}): {len(changed)} file(s)")
    return found


def check(tag, ref):
    commit = git("rev-parse", f"{ref}^{{commit}}").stdout.strip()
    since = previous_release_time(tag)
    blocking, open_prs = branch_findings(commit, pull_requests(), since)
    dirty = dirty_worktrees(since)

    if open_prs:
        print(f"release gate: open PRs not in {tag} (not blocking):", file=sys.stderr)
        for entry in open_prs:
            print(f"  {entry}", file=sys.stderr)
    if not blocking and not dirty:
        return 0

    print(f"\nrelease gate: {tag} is missing local work.", file=sys.stderr)
    if blocking:
        print("Local branches with commits the tag lacks:", file=sys.stderr)
        for entry in blocking:
            print(f"  {entry}", file=sys.stderr)
    if dirty:
        print("Worktrees with uncommitted changes:", file=sys.stderr)
        for entry in dirty:
            print(f"  {entry}", file=sys.stderr)
    print("\nLand or delete them, or push anyway with MAGDA_RELEASE_OK=1.", file=sys.stderr)
    return 1


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        tags = [(sys.argv[2], sys.argv[2])]
    else:
        tags = list(pushed_tags(sys.stdin.read().splitlines()))
    if not tags:
        return 0
    if os.environ.get("MAGDA_RELEASE_OK") == "1":
        print("release gate: skipped (MAGDA_RELEASE_OK=1)", file=sys.stderr)
        return 0
    try:
        return max(check(tag, sha) for tag, sha in tags)
    except RuntimeError as error:
        print(f"release gate: {error}\nFix that, or push anyway with MAGDA_RELEASE_OK=1.",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
