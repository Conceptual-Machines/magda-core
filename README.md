<p align="center">
  <img src="assets/Banner.png" alt="MAGDA" width="400">
</p>

<p align="center">
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=Linux&logo=linux&logoColor=white" alt="Linux Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=macOS&logo=apple" alt="macOS Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Conceptual-Machines/magda-core/ci.yml?label=Windows&logo=windows" alt="Windows Build"></a>
  <a href="https://github.com/Conceptual-Machines/magda-core/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
</p>

<p align="center">
  Multi-Agent Digital Audio
</p>
<p align="center"><img src="assets/treaktion-engine-logo.png" alt="Powered by Tracktion Engine" width="250" height="80"></p>

---

MAGDA is a free, open-source DAW with AI integrated from the ground up. Built on C++20, JUCE, and Tracktion Engine.

### Features

- **Hybrid tracks**: every track hosts both audio and MIDI clips
- **Three views**: Arrangement, Session, and Mix
- **AI chat**: natural language commands that generate and execute a custom DSL directly in the app (BYOAK, Bring Your Own API Keys)
- **Modulation system**: 16 LFOs (with bezier curve editor) and 16 macro knobs per device and rack
- **Racks**: parallel processing chains with volume, pan, mute, solo per chain, fully nestable
- **Piano roll** with pitchbend and MIDI CC lanes
- **Drum grid** device
- **Session view** with clip launching
- **Mixer** with faders, pan, mute, solo, sends, and I/O routing
- **Collapsible, resizable panels**: Inspector + AI Chat (left), Plugin Browser + Sample Browser (right), context-sensitive editor (bottom)

### Links

- **Website**: [magda.land](https://magda.land/)
- **YouTube**: [Introduction Video](https://www.youtube.com/watch?v=momhIo5vOSc)
- **KVR**: [Product Page](https://www.kvraudio.com/product/magda-by-conceptual-machines)
- **Issues**: [Bug Reports & Feature Requests](https://github.com/Conceptual-Machines/magda-core/issues)

## Status

See [Issues](https://github.com/Conceptual-Machines/magda-core/issues) for known bugs and planned features.

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
make lint       # Run clang-tidy analysis
```

## Automated Workflows

The project includes automated GitHub Actions workflows:

- **CI Workflow**: Runs on every push to validate builds and code quality
- **Security Scanning**: CodeQL analysis, secret detection, and vulnerability scanning
- **Periodic Code Analysis**: Weekly scans for TODOs, FIXMEs, and code smells
- **Refactoring Scanner**: Bi-weekly analysis of code complexity and technical debt

See [docs/AUTOMATED_WORKFLOWS.md](docs/AUTOMATED_WORKFLOWS.md) for details on automated analysis and periodic workflows.

## Security

MAGDA takes security seriously. The repository implements comprehensive security measures:

- 🔒 **Branch Protection**: Main branch protected with required reviews and status checks
- 🔍 **Automated Scanning**: CodeQL security analysis for C++ vulnerabilities
- 🔐 **Secret Detection**: Automated scanning to prevent credential leaks
- 🛡️ **Dependency Monitoring**: Dependabot for security updates
- ⚡ **CI/CD Security**: All security checks must pass before merge

**Found a security issue?** Please review our [Security Policy](SECURITY.md) for responsible disclosure.

For detailed information about branch protection and security architecture, see [docs/BRANCH_PROTECTION.md](docs/BRANCH_PROTECTION.md).

## Architecture

```
magda/
├── daw/        # DAW application (C++/JUCE)
│   ├── audio/      # Audio processing
│   ├── core/       # Track, clip, selection management
│   ├── engine/     # Tracktion Engine wrapper
│   ├── interfaces/ # Abstract interfaces
│   ├── profiling/  # Performance profiling
│   ├── project/    # Project management and serialization
│   ├── ui/         # User interface components
│   └── utils/      # Utility helpers
└── agents/     # Agent system (C++)
tests/          # Test suite
scripts/        # Development and build scripts
docs/           # Documentation
```

## Dependencies

- [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) - Audio engine
- [JUCE](https://juce.com/) - C++ application framework (GUI, audio I/O, plugin hosting, MIDI, DSP)
- [juce-llm](https://github.com/Conceptual-Machines/juce-llm) - LLM API client module
- [llama.cpp](https://github.com/ggml-org/llama.cpp) - Embedded local LLM inference
- [Catch2](https://github.com/catchorg/Catch2) - Testing (fetched via CMake)

## Issues

Found a bug or have a feature request? Please [open an issue](https://github.com/Conceptual-Machines/magda-core/issues/new) on GitHub.

## License

GPL v3 - see [LICENSE](LICENSE) for details.
