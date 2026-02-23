---
name: git
description: Git workflow rules for this repository. MUST be followed for all git operations — branching, committing, pushing.
---

# Git Workflow Rules

## Creating Branches

**NEVER** let a feature branch track `origin/main`. This causes accidental pushes to main.

```bash
git checkout main
git pull
git checkout -b <branch-name>
git push -u origin <branch-name>
```

**NEVER** push directly to `origin/main`. All changes go through PRs.

## Branch Naming

Use the pattern: `feat/<short-description>` or `fix/<short-description>`

Examples:
- `feat/midi-cc-pitchbend`
- `fix/sampler-load-icon`

## Committing

- Only commit when the user explicitly asks
- Use descriptive commit messages with a short summary line
- If pre-commit hooks fail (e.g. clang-format), re-stage the formatted files and create a **new** commit — do NOT amend
- Never use `--no-verify`

## Dangerous Commands — NEVER Run Without Explicit User Request

- `git push --force`
- `git reset --hard`
- `git checkout .` / `git restore .`
- `git clean -f`
- `git branch -D`
