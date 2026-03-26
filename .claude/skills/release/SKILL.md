---
name: release
description: Tag and push a release. Use when the user says "release X.Y.Z". Does NOT merge any branches — just tags current main and pushes.
user_invocable: true
argument: version (e.g. 0.2.2)
---

# Release

A release is **only** a git tag on `main`. It does **NOT** involve merging feature branches.

## Prerequisites

- You MUST be on the `main` branch. If you are on a feature branch, **switch to main first**.
- `main` must be up to date with `origin/main`.

## Steps

```bash
# 1. Switch to main (if not already)
git checkout main

# 2. Pull latest
git pull origin main

# 3. Tag
git tag v<VERSION>

# 4. Push the tag
git push origin v<VERSION>
```

## Rules

- **NEVER** merge a feature branch into main as part of a release. Feature branches go through PRs.
- The version argument should be prefixed with `v` in the tag (e.g. `v0.2.2`).
- If the user provides the version with a `v` prefix already, don't double it.
- If the tag already exists, ask the user before overwriting.
