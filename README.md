<p align="center">
  <img src="assets/banner.png" alt="MAGDA" width="400">
</p>

<p align="center">
  <a href="https://github.com/Conceptual-Machines/magda-core/actions"><img src="https://github.com/Conceptual-Machines/magda-core/workflows/CI/badge.svg" alt="CI"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
</p>

<p align="center">
  Multi-Agent Generative Digital Audio
</p>
<p align="center"><img src="assets/treaktion-engine-logo.png" alt="Powered by Traktion Engine" width="250" height="80"></p>

---

## Status

Early research and prototyping. Not yet ready for production use.

## Building

### Prerequisites

- C++20 compiler (GCC 10+, Clang 12+, or Xcode)
- CMake 3.20+

### Quick Start

```bash
# Clone with submodules
git clone --recursive https://github.com/Conceptual-Machines/magda-core.git
cd magda-core

# Setup and build
make setup
make debug

# Run
make run
```

### Make Targets

```bash
make setup      # Initialize submodules and dependencies
make debug      # Debug build
make release    # Release build
make test       # Run tests
make clean      # Clean build artifacts
make format     # Format code
make quality    # Run all quality checks
```

## Architecture

```
magda/
├── daw/        # DAW application (C++/JUCE)
│   ├── core/       # Track, clip, selection management
│   ├── engine/     # Tracktion Engine wrapper
│   ├── ui/         # User interface components
│   └── interfaces/ # Abstract interfaces
├── agents/     # Agent system (C++)
└── tests/      # Test suite
```

## Dependencies

- [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) - Audio engine
- [JUCE](https://juce.com/) - GUI framework
- [Catch2](https://github.com/catchorg/Catch2) - Testing (fetched via CMake)
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library (fetched via CMake)

## License

GPL v3 - see [LICENSE](LICENSE) for details.
