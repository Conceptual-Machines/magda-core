---
name: release
description: Tag and push a release. Use when the user says "release X.Y.Z". Does NOT merge any branches — just tags the current HEAD and pushes.
user_invocable: true
argument: version (e.g. 0.2.2 or 0.5.3-rc0)
---

# Release

A release is **only** a git tag. It does **NOT** involve merging feature branches.

## Version rules

- **Stable releases** (e.g. `0.2.2`, `1.0.0`) MUST be tagged on `main`.
- **Release candidates and pre-releases** (any version containing `-rc`, `-alpha`, `-beta`, `-pre`) MAY be tagged on any branch — useful for cutting an RC off a feature branch before it merges.

A version is a pre-release if it contains a hyphen followed by `rc`, `alpha`, `beta`, or `pre` (e.g. `0.5.3-rc0`, `1.0.0-beta.2`).

## Prerequisites

- For a **stable release**: you MUST be on `main`, and `main` must be up to date with `origin/main`.
- For a **pre-release**: any branch is fine, but it must be clean (no uncommitted changes) and pushed to its remote.

## Steps

**Stable release:**

```bash
git checkout main
git pull origin main
git tag v<VERSION>
git push origin v<VERSION>
```

**Pre-release (RC/alpha/beta) from current branch:**

```bash
# Verify branch is clean and pushed
git status
git push            # ensure remote is up to date

git tag v<VERSION>
git push origin v<VERSION>
```

## Release gate

A pre-push hook (`make hooks`, `scripts/release_gate.py`) refuses a `v*` tag push while local
work is missing from the tag. It looks at work newer than the previous release: local branches
whose commits the tag lacks and no PR covers, and worktrees with uncommitted edits. Run it
before tagging:

```bash
python3 scripts/release_gate.py --check HEAD
```

When it reports anything, show the user the list and wait. Set `MAGDA_RELEASE_OK=1` only when
the user says to push anyway. Never set it on your own.

## Rules

- **NEVER** merge a feature branch into main as part of a release. Feature branches go through PRs.
- The version argument should be prefixed with `v` in the tag (e.g. `v0.2.2`, `v0.5.3-rc0`).
- If the user provides the version with a `v` prefix already, don't double it.
- If the tag already exists, ask the user before overwriting.
- Before tagging a pre-release on a feature branch, confirm the branch name back to the user so they can catch mistakes.
