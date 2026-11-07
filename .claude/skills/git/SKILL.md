---
name: git
description: Git workflow rules for this repository. MUST be followed for all git operations, including branching, committing and pushing.
---

# Git Workflow Rules

## Creating Branches

Branch from `main`, the only long-lived branch. Other bases are sometimes right (a fix on top of
an unmerged PR's branch), so ask if it is unclear. `--no-track` keeps the branch off `origin/main`.

```bash
git fetch --prune
git checkout -b <branch-name> --no-track origin/main
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

## Issues and PRs

- One issue, one PR: scope an issue to what a single self-contained PR delivers. Group related
  issues with an `epic:*` label, not an umbrella issue that stays open.
- The PR closes its issue in full. Leftovers go in a new issue (or get dropped); they are never
  a reason to keep the original open.
- Put `Closes #N` in the PR body; GitHub closes the issue when the PR merges into main.
- PRs into main merge as a squash: main's ruleset allows nothing else.

## Dangerous Commands — NEVER Run Without Explicit User Request

- `git push --force`
- `git reset --hard`
- `git checkout .` / `git restore .`
- `git clean -f`
- `git branch -D`
