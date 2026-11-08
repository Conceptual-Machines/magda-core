# Installation

## System Requirements

| | Minimum | Recommended |
|---|---|---|
| **OS** | macOS 11+ (Apple Silicon or Intel), Windows 10 (64-bit), Linux with glibc 2.39 or newer (Ubuntu 24.04, Debian 13) | Latest stable release |
| **RAM** | 4 GB | 8 GB+ |
| **Disk** | 200 MB | 500 MB+ (for projects and audio files) |
| **Audio** | Built-in audio output | Dedicated audio interface |

Optional models, such as a local LLM or the stem separation weights, are separate downloads with their own sizes. See [AI](../interface/ai-settings.md#local-models).

!!! note "Intel Macs"
    The Intel Mac build doesn't include the ONNX runtime, so a few features are missing there: **Transcribe to MIDI**, the Demucs and Spleeter engines of [Split into Stems](../stem-separation.md) (HPSS still works), the Media Library's semantic search and "find similar sounds", tempo detection with the beat tracker, and the larger command model for Fast Inference. Everything else, including the embedded local LLM, works as on Apple Silicon.

## Download

Download the latest release from the [GitHub Releases](https://github.com/Conceptual-Machines/magda-core/releases) page. Each release has one file per platform:

| Platform | File |
|---|---|
| macOS, Apple Silicon | `MAGDA-<version>-macOS-arm64.dmg` |
| macOS, Intel | `MAGDA-<version>-macOS-x86_64.dmg` |
| Windows | `MAGDA-<version>-Windows-x86_64-Setup.exe` |
| Linux | `MAGDA-<version>-Linux-x86_64.AppImage` |

Each file has a `.sha256` file beside it, for checking the download.

### Updates

MAGDA checks the GitHub releases feed at most once every 24 hours on launch and shows a dialog only if a newer version is available. You can also run the check from **Help > Check for Updates…**. A pre-release build, such as a beta, is also told about newer pre-releases. No telemetry is sent.

## Building from Source

MAGDA is built with CMake and requires a C++23 compiler. See the [README](https://github.com/Conceptual-Machines/magda-core#building) for full build instructions.

## Audio Setup

On first launch, open **Settings > Audio & MIDI** (see [Audio & MIDI](../interface/audio-settings.md)) to configure:

- **Driver** and **Interface** — Select your audio interface or built-in output
- **Sample Rate** — 44100 Hz or 48000 Hz are common choices
- **Buffer Size** — Lower values reduce latency but increase CPU load
