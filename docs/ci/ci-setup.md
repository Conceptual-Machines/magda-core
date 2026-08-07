# CI/CD Setup

## Overview

MAGDA uses GitHub Actions for continuous integration. The CI pipeline runs on Linux to minimize costs while ensuring code quality and test coverage.

## Workflows

### Main CI Pipeline (`.github/workflows/ci.yml`)

Runs on every push to `main` and `develop` branches, and on all pull requests.

**Jobs:**

1. **build-and-test-linux**
   - Platform: Ubuntu Latest
   - Installs JUCE dependencies (ALSA, JACK, X11, etc.)
   - Builds with Ninja generator
   - Runs all 87 tests via Catch2
   - Validates test executable exists

2. **code-quality**
   - Platform: Ubuntu Latest
   - Checks code formatting with clang-format
   - Reports formatting violations

## Running CI Checks Locally

### Build and Test
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y \
  build-essential cmake ninja-build \
  libgtk-3-dev \
  libasound2-dev libjack-jackd2-dev \
  libfreetype6-dev libx11-dev \
  libxcomposite-dev libxcursor-dev \
  libxinerama-dev libxrandr-dev

# Configure and build
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMAGDA_BUILD_TESTS=ON

cmake --build build -j$(nproc)

# Run tests
cd build && ./tests/magda_tests
```

### Code Quality Checks
```bash
# Check formatting
make format  # or: find magda tests -name "*.cpp" | xargs clang-format -i

# Verify no changes
git diff --exit-code
```

## Test Categories

The CI runs all test categories:
- `[ui][plugin][window]` - Plugin window management (15 assertions)
- `[ui][shutdown]` - Component destruction order (7 assertions)
- `[threading]` - Thread safety tests
- `[modulation]` - Modulation system tests
- `[command]` - Command pattern tests
- `[chain_path]` - Device chain navigation
- And 80+ more test cases

Total: **581 assertions across 87 test cases**

## Why Linux Only?

GitHub Actions pricing:
- **Linux runners**: Free for public repos, cheap for private
- **macOS runners**: 10x more expensive
- **Windows runners**: 2x more expensive

Since JUCE/Tracktion code is cross-platform, Linux CI provides good coverage at minimal cost. Local development still happens on macOS.

## Future Improvements

- [ ] Add caching for dependencies (ccache, apt packages)
- [ ] Add code coverage reporting
- [ ] Add static analysis with clang-tidy
- [ ] Add binary artifact uploads for releases
- [ ] Consider optional macOS/Windows builds for releases

## Troubleshooting

### Build fails on Linux but works on macOS
- Check for platform-specific APIs (AudioUnit, CoreAudio, etc.)
- Ensure proper JUCE platform detection
- Verify all dependencies are installed

### Tests fail in CI but pass locally
- Check for absolute paths in tests
- Verify headless mode works (no display required)
- Look for timing issues in tests

### Format check fails
Run `make format` locally to auto-fix formatting issues.
