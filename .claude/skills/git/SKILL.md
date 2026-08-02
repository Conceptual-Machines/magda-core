---
name: git
description: Git workflow rules for this repository. MUST be followed for all git operations, including branching, committing and pushing.
---

# Git Workflow Rules

## Creating Branches

The usual base is the current `dev/0.X.0` branch. `main` only moves at release time, so it can
trail dev by a long way. Other bases are sometimes right, so check the situation rather than
applying a fixed recipe, and ask if it is unclear.

```bash
git fetch --prune
git checkout -b <branch-name> origin/dev/0.X.0
```

**NEVER** let a feature branch track `origin/main`. This causes accidental pushes to main.

**NEVER** push directly to `origin/main`. All changes go through PRs.

If `git status` shows `third_party/faust` and `third_party/tracktion_engine` modified plus an
untracked `third_party/JUCE/`, the submodules are not actually dirty. The branch is just based
on a commit that predates JUCE becoming a first-class submodule. Rebasing onto the intended base
clears all three, so do not reset or re-sync the submodules.

## Branch Naming

Use the pattern: `feat/<short-description>` or `fix/<short-description>`

Examples:
- `feat/midi-cc-pitchbend`
- `fix/sampler-load-icon`

## Committing

- Only commit when the user explicitly asks
- Use descriptive commit messages with a short summary line
- If pre-commit hooks fail (e.g. clang-format), re-stage the formatted files and create a **new** commit. Do NOT amend
- Never use `--no-verify`

## Dangerous Commands — NEVER Run Without Explicit User Request

- `git push --force`
- `git reset --hard`
- `git checkout .` / `git restore .`
- `git clean -f`
- `git branch -D`
