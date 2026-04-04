# Contributing to MAGDA

Thanks for your interest in MAGDA! This document covers how to contribute effectively.

## Contributor License Agreement

All contributions require signing our [Contributor License Agreement (CLA)](CLA.md). This grants the maintainer the right to use your contributions under any license, including commercial licenses. The CLA bot will prompt you to sign when you open your first pull request.

## Before You Start

**Open an issue first.** Don't spend time on a PR that might get rejected. Describe what you want to change and why. Wait for a green light before writing code. PRs for features that have been discussed and declined will be closed immediately.

## What We Accept

- Bug fixes with a clear reproduction case
- Documentation improvements
- Performance improvements with benchmarks
- Small, focused features that have been discussed and approved via an issue

## What We Don't Accept

- Unsolicited large features or integrations
- Changes that add new third-party service dependencies
- PRs that rewrite or restructure existing code without prior discussion
- AI/LLM provider integrations (we maintain these ourselves)

## Development Setup

### Prerequisites

- CMake 3.24+
- C++20 compiler (Clang 15+ or GCC 13+)
- macOS: Xcode Command Line Tools
- Linux: ALSA and X11 development headers

### Building

```bash
git clone --recursive https://github.com/Conceptual-Machines/magda-core.git
cd magda-core
make debug    # Debug build
make release  # Release build
```

### Running Tests

```bash
make test
```

## PR Guidelines

1. **One concern per PR.** Don't mix bug fixes with refactoring or features.
2. **Keep it small.** Large PRs are hard to review and will take longer to merge.
3. **Match the existing style.** We use `clang-format` — the pre-commit hook enforces this.
4. **Write a clear description.** Explain what changed and why. Include before/after if relevant.
5. **Don't break the build.** Make sure `make debug` and `make release` both pass before pushing.

## Branch Naming

- `feat/<description>` for features
- `fix/<description>` for bug fixes
- `docs/<description>` for documentation

## Code Style

- C++20
- `clang-format` with the project's `.clang-format` config
- No raw `new`/`delete` — use smart pointers
- No blocking operations on the audio thread

## License

By contributing, you agree that your contributions will be licensed under the [GNU General Public License v3.0](LICENSE).
