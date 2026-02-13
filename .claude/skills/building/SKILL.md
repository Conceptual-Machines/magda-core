---
name: building
description: Build, test, and run MAGDA DAW. Use when compiling, running tests, or launching the app. Always use Makefile commands, never raw cmake/ninja.
---

# Building MAGDA

Always use Makefile targets. Never use `cmake` or `ninja` directly.

## Build Commands

| Command | Purpose |
|---------|---------|
| `make debug` | Debug build (default) |
| `make release` | Release build |
| `make configure` | Force CMake reconfigure |
| `make clean` | Remove all build artifacts |
| `make rebuild` | Clean + debug build |

## Test Commands

| Command | Purpose |
|---------|---------|
| `make test` | Build and run all tests |
| `make test-build` | Build tests only |
| `make test-verbose` | Run tests with verbose output |
| `make test-list` | List all available tests |
| `make test-window` | Plugin window tests only |
| `make test-shutdown` | Shutdown sequence tests only |
| `make test-threading` | Thread safety tests only |

## Run Commands

| Command | Purpose |
|---------|---------|
| `make run` | Build and launch app (GUI) |
| `make run-console` | Build and run with stdout visible |
| `make run-profile` | Run with performance profiling |

## Code Quality

| Command | Purpose |
|---------|---------|
| `make format` | clang-format all source files |
| `make lint` | clang-tidy on all sources |
| `make lint-changed` | clang-tidy on modified files only |
| `make lint-file FILE=path` | clang-tidy on a specific file |

## Build Directories

- Debug: `cmake-build-debug/`
- Release: `cmake-build-release/`

## Key Notes

- Debug build directory is `cmake-build-debug`, NOT `build/`
- Test binary is at `cmake-build-debug/tests/magda_tests`
- App bundle is at `cmake-build-debug/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app`
- Pre-commit hooks run clang-format automatically; if commit fails due to formatting, re-stage and commit again
