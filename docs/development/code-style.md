# Code Style Guidelines

This document outlines the coding standards and quality tools used in the MAGDA project.

## Overview

The MAGDA project uses automated code quality tools to ensure consistent, maintainable, and high-quality C++ code. Our toolchain includes:

- **clang-format**: Automatic code formatting
- **clang-tidy**: Static analysis and linting
- **pre-commit**: Automated hooks for quality checks
- **GitHub Actions**: CI integration for code quality

## Code Formatting

### clang-format Configuration

We use clang-format with a custom configuration (`.clang-format`) based on the LLVM style with audio development optimizations:

- **Line Length**: 100 characters
- **Indentation**: 4 spaces (no tabs)
- **Braces**: Attached style (`{` on same line)
- **Pointer Alignment**: Left-aligned (`int* ptr`)
- **Reference Alignment**: Left-aligned (`int& ref`)

### Using clang-format

```bash
# Format all code
make format

# Check formatting without changes
make check-format

# Format specific files
clang-format -i -style=file path/to/file.cpp
```

## Static Analysis

### clang-tidy Configuration

Our `.clang-tidy` configuration includes:

- **Comprehensive checks**: Most standard checks enabled
- **Performance optimizations**: Audio-specific performance rules
- **Readability improvements**: Consistent naming and structure
- **Bug prevention**: Common C++ pitfalls and errors

### Using clang-tidy

```bash
# Run basic static analysis
make lint

# Run comprehensive analysis (slower)
make lint-all

# Fix issues automatically
make fix
```

## Naming Conventions

### Classes and Structs
```cpp
class AudioProcessor {
    // CamelCase for classes
};

struct AudioBuffer {
    // CamelCase for structs
};
```

### Functions and Methods
```cpp
void processAudio() {
    // camelBack for functions
}

bool isPlaying() const {
    // camelBack for methods
}
```

### Variables
```cpp
int sampleRate;           // camelBack for variables
const int MAX_CHANNELS;   // UPPER_CASE for constants
```

### Member Variables
```cpp
class AudioEngine {
private:
    int bufferSize_;      // camelBack with trailing underscore
    bool isRunning_;      // for private members
};
```

### Namespaces
```cpp
namespace magda {
    // lower_case for namespaces
namespace audio_engine {
    // lower_case with underscores
}
}
```

## Code Organization

### Header Files
```cpp
#pragma once

// System headers first
#include <vector>
#include <memory>

// Third-party headers
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

// Local headers last
#include "AudioProcessor.hpp"
```

### Implementation Files
```cpp
// Always include corresponding header first
#include "AudioProcessor.hpp"

// Then other includes in order
#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioBuffer.hpp"
```

## Quality Checks

### Available Make Targets

| Target | Description |
|--------|-------------|
| `make format` | Format all code with clang-format |
| `make check-format` | Check formatting without changes |
| `make lint` | Run static analysis (sample of files) |
| `make lint-all` | Run comprehensive static analysis |
| `make quality` | Run all quality checks |
| `make fix` | Fix common issues and format code |

### Pre-commit Hooks

Install pre-commit hooks for automatic quality checks:

```bash
# Install pre-commit (if not already installed)
pip install pre-commit

# Install hooks
pre-commit install

# Run hooks on all files
pre-commit run --all-files
```

The pre-commit configuration includes:
- Automatic formatting with clang-format
- Trailing whitespace removal
- End-of-file fixes
- YAML validation
- Large file checks

## CI Integration

All pull requests automatically run:
- Code formatting checks
- Static analysis with clang-tidy
- Build verification
- Test execution

## clang-tidy Compatibility

### Known Issues

Some versions of clang-tidy may have compatibility issues on certain systems. If you encounter:

- "illegal instruction" errors
- Signal 4 (SIGILL) crashes
- clang-tidy termination issues

The build system will gracefully fall back to formatting-only mode. The following approaches can help:

1. Try using a different version of LLVM: `brew install llvm@<version>`
2. Use system clang-tidy if available
3. The code quality system will continue to work with formatting and other checks

### Alternative Installation

If Homebrew LLVM doesn't work, try:
```bash
# Alternative approaches
brew install llvm@15  # Try older version
# or build from source
# or use package manager alternatives
```

## Common Issues and Solutions

### Long Lines
```cpp
// Bad: Line too long
audioProcessor.processBuffer(inputBuffer, outputBuffer, numSamples, sampleRate, true);

// Good: Break into multiple lines
audioProcessor.processBuffer(inputBuffer,
                           outputBuffer,
                           numSamples,
                           sampleRate,
                           true);
```

### Pointer/Reference Alignment
```cpp
// Bad: Right-aligned
int *ptr;
int &ref;

// Good: Left-aligned
int* ptr;
int& ref;
```

### Include Order
```cpp
// Bad: Mixed include order
#include "LocalHeader.hpp"
#include <vector>
#include <juce_core/juce_core.h>

// Good: Proper include order
#include <vector>
#include <juce_core/juce_core.h>
#include "LocalHeader.hpp"
```

## IDE Integration

### VS Code
Install the C/C++ extension and configure:

```json
{
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_fallbackStyle": "LLVM",
    "editor.formatOnSave": true
}
```

### CLion/IntelliJ
1. Go to Settings → Editor → Code Style → C/C++
2. Set Scheme to "Project"
3. Import settings from `.clang-format`

## Performance Considerations

### Audio-Specific Guidelines
- Avoid memory allocations in real-time audio code
- Use RAII for resource management
- Prefer stack allocation over heap allocation
- Use const-correctness consistently
- Minimize function call overhead in audio callbacks

### Example: Audio Processing Function
```cpp
void processAudio(const float* input, float* output, int numSamples) {
    // Good: Stack allocation, const-correctness
    constexpr float gain = 0.5f;

    for (int i = 0; i < numSamples; ++i) {
        output[i] = input[i] * gain;
    }
}
```

## Contributing

When contributing to the project:

1. Run `make quality` before committing
2. Ensure all CI checks pass
3. Follow the naming conventions
4. Add appropriate comments for complex logic
5. Use the provided make targets for consistency

For questions about code style or tool usage, please refer to the project documentation or open an issue.
