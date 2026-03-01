# Installation

## System Requirements

| | Minimum | Recommended |
|---|---|---|
| **OS** | macOS 11+, Windows 10, Ubuntu 22.04 | Latest stable release |
| **RAM** | 4 GB | 8 GB+ |
| **Disk** | 200 MB | 500 MB+ (for projects and audio files) |
| **Audio** | Built-in audio output | Dedicated audio interface |

## Download

Download the latest release from the [GitHub Releases](https://github.com/Conceptual-Machines/magda-core/releases) page.

## Building from Source

MAGDA is built with CMake and requires a C++17 compiler. See the [README](https://github.com/Conceptual-Machines/magda-core#building) for full build instructions.

## Audio Setup

On first launch, go to **Settings > Audio** to configure:

- **Audio device** — Select your audio interface or built-in output
- **Sample rate** — 44100 Hz or 48000 Hz are common choices
- **Buffer size** — Lower values reduce latency but increase CPU load

!!! tip
    MAGDA automatically optimizes buffer sizes when switching views: Live mode uses the lowest latency, while Mix and Master modes use larger buffers for stability.
