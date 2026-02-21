# Faust DSP Integration — Design Document

## Overview

MAGDA integrates the [Faust](https://faust.grame.fr/) DSP programming language to allow users to write custom audio processors — effects, instruments, and analysis tools — using a high-level functional language that compiles to native code. Faust devices sit alongside built-in plugins and VST/AU plugins in MAGDA's signal chain, with full access to the parameter system, automation, modulation, and the DSL/API layer.

The integration is designed around a key principle: **the Faust compiler is an external tool, not a linked dependency.** MAGDA's binary never links against `libfaust` or LLVM. Instead, the Faust compiler runs as a separate process that produces dynamic libraries (`.dylib` on macOS, `.so` on Linux, `.dll` on Windows), which MAGDA loads at runtime.

---

## Architecture

```
User writes .dsp file
        │
        ▼
┌─────────────────────┐
│  MAGDA Script Editor │  (syntax highlighting, error display)
└────────┬────────────┘
         │ "Compile" action
         ▼
┌─────────────────────┐
│  faust (external)    │  Faust compiler with LLVM — installed separately
│                      │  Runs as a subprocess, not linked into MAGDA
│  Input:  .dsp file   │
│  Output: .dylib/.so  │
└────────┬────────────┘
         │ compiled shared library
         ▼
┌─────────────────────┐
│  MAGDA runtime       │  dlopen() / LoadLibrary()
│  FaustDevice wrapper │  Calls standardised C interface
│                      │  Exposes params to automation, DSL, UI
└─────────────────────┘
```

### Why External Compilation to Dynamic Library?

Several approaches were considered:

**Approach 1: `juce_faustllvm` (in-process JIT)**
Links `libfaust` + LLVM directly into MAGDA. Enables instant hot-reload (edit → hear in milliseconds). However, LLVM adds ~50-100MB to the binary and significantly increases build times. A crash in the JIT compiler takes down the host. This approach may be revisited in a future version if instant hot-reload becomes a priority.

**Approach 2: `faust2juce` (ahead-of-time C++ generation)**
The Faust compiler generates C++ source files, which are then compiled as part of MAGDA's build. No runtime dependencies, but requires recompilation of MAGDA itself (or a separate plugin build step). Tight coupling between Faust output and MAGDA's build system.

**Approach 3: External compiler → dynamic library (chosen)**
The Faust compiler runs as a separate process and produces a standard shared library. MAGDA loads it at runtime via `dlopen`. Zero impact on MAGDA's build time and binary size. The Faust compiler (with its LLVM machinery) is installed separately by the user. Compilation takes a few seconds — not instant, but fast enough to feel interactive.

The dynamic library approach was chosen because it keeps MAGDA's build clean, isolates any compiler crashes from the host process, and produces portable artifacts that can be shared between users without requiring the Faust toolchain.

---

## C Interface (FaustDevice ABI)

Every compiled Faust dynamic library must export a standardised C interface. This is the contract between the compiled DSP and MAGDA's `FaustDevice` wrapper.

```cpp
// faust_device_abi.h — shipped with MAGDA, used by the compilation wrapper

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle
void* faust_create(int sample_rate);
void  faust_destroy(void* dsp);

// Audio processing
void  faust_compute(void* dsp, int count, float** inputs, float** outputs);
int   faust_get_num_inputs(void* dsp);
int   faust_get_num_outputs(void* dsp);

// Parameters
int         faust_get_num_params(void* dsp);
const char* faust_get_param_name(void* dsp, int index);
const char* faust_get_param_label(void* dsp, int index);   // unit label (dB, Hz, etc.)
float       faust_get_param_min(void* dsp, int index);
float       faust_get_param_max(void* dsp, int index);
float       faust_get_param_default(void* dsp, int index);
float       faust_get_param_step(void* dsp, int index);
float       faust_get_param_value(void* dsp, int index);
void        faust_set_param_value(void* dsp, int index, float value);

// UI metadata (for generating MAGDA UI)
// Returns JSON describing the UI layout extracted from Faust's UI declarations
const char* faust_get_ui_json(void* dsp);

// Metadata
const char* faust_get_name(void* dsp);
const char* faust_get_author(void* dsp);
const char* faust_get_version(void* dsp);

#ifdef __cplusplus
}
#endif
```

The `faust_get_ui_json()` function returns the parameter layout as declared in the Faust source (hslider, vslider, button, hgroup, vgroup, etc.) serialised as JSON. MAGDA uses this to auto-generate a matching UI.

---

## Compilation Pipeline

When the user triggers compilation from the script editor, MAGDA runs the following pipeline:

### Step 1: Invoke the Faust Compiler

```bash
faust -lang c -a /path/to/magda_faust_arch.c \
      -o /tmp/faust_build/MyEffect.c \
      MyEffect.dsp
```

The `-a` flag specifies an architecture file — a C wrapper template shipped with MAGDA that implements the ABI above. The architecture file includes the Faust-generated DSP code and wraps it with the `faust_create`, `faust_compute`, etc. functions.

### Step 2: Compile to Shared Library

```bash
# macOS
cc -shared -O2 -fPIC -o MyEffect.dylib /tmp/faust_build/MyEffect.c

# Linux
cc -shared -O2 -fPIC -o MyEffect.so /tmp/faust_build/MyEffect.c

# Windows
cl /LD /O2 /tmp/faust_build/MyEffect.c /Fe:MyEffect.dll
```

This uses the system C compiler — no special toolchain required beyond what's already available on a developer's machine (Xcode command line tools on macOS, gcc/clang on Linux, MSVC on Windows).

### Step 3: Load into MAGDA

MAGDA loads the resulting `.dylib`/`.so`/`.dll` via `dlopen` and resolves the ABI symbols. The `FaustDevice` wrapper maps these to MAGDA's internal parameter system.

### Error Handling

If the Faust compiler reports errors (syntax errors, type errors), MAGDA captures stderr and displays the errors inline in the script editor with line numbers highlighted. The previous compiled version (if any) continues running — a failed compilation never interrupts audio.

---

## FaustDevice Integration with MAGDA

### As a Device in the Signal Chain

A `FaustDevice` is a subclass of `DeviceProcessor` and behaves like any other device in MAGDA's chain panel. It appears in the effects chain alongside built-in devices (EQ, Compressor, etc.) and external plugins (VST/AU). Users add it via:

- The chain panel "add device" menu (under a "Faust" category)
- The DSL: `track("Bass").add_fx(type="faust", file="warmfilter.dsp")`

### Parameter Mapping

When a Faust library is loaded, the `FaustDevice` reads all parameters via the C ABI and registers them with MAGDA's `ParameterManager`. This means:

- **Automation**: Every Faust parameter can be automated on the timeline with curves and breakpoints, just like any built-in parameter.
- **Modulation**: LFOs and modulators can target Faust parameters through the existing modulation system.
- **DSL access**: `track("Bass").device("WarmFilter").set(cutoff=800, resonance=0.7)` works the same as for any device.
- **Presets**: Parameter states can be saved and recalled via MAGDA's preset system.

### UI Generation

The JSON returned by `faust_get_ui_json()` describes the parameter layout as the Faust author intended it — groups, sliders, knobs, buttons, with labels and ranges. MAGDA's `FaustDeviceUI` component reads this JSON and generates JUCE components that match MAGDA's visual style:

- `hslider` / `vslider` → MAGDA's `TextSlider` component
- `button` / `checkbox` → MAGDA toggle button
- `hgroup` / `vgroup` / `tgroup` → collapsible parameter groups
- `bargraph` → level meter / display
- `[style:knob]` metadata → knob-style control

The generated UI is fully functional but follows MAGDA's design language, not Faust's default look. This ensures visual consistency across built-in devices, external plugins, and Faust devices.

### Hot-Reload (Without LLVM in Process)

When the user modifies a `.dsp` file and recompiles:

1. MAGDA compiles the new version to a new `.dylib` (with a unique temp path).
2. A new `faust_create()` instance is initialised at the current sample rate.
3. Parameter values from the old instance are mapped to the new instance (by name).
4. Audio processing crossfades from the old instance to the new one over ~10ms.
5. The old library is unloaded.

This provides near-instant feedback (the few-second compilation delay is the bottleneck, not the swap). There is no audio interruption during the swap.

---

## File Organisation

### User Faust Files

```
MyProject.magda/
├── project.json
├── faust/                    # User's Faust source files
│   ├── warmfilter.dsp
│   ├── granular_delay.dsp
│   └── spectral_verb.dsp
└── faust_cache/              # Compiled libraries (gitignored, rebuilt on demand)
    ├── warmfilter.dylib
    ├── warmfilter.json       # Cached UI metadata
    └── warmfilter.hash       # SHA256 of source, for cache invalidation
```

### MAGDA-Shipped Files

```
MAGDA.app/
└── Resources/
    └── faust/
        ├── magda_faust_arch.c       # Architecture file (ABI wrapper template)
        ├── faust_device_abi.h       # C header for the ABI
        └── examples/                # Example .dsp files
            ├── simple_filter.dsp
            ├── stereo_delay.dsp
            └── analog_saturation.dsp
```

---

## Distribution of Faust Devices

Compiled Faust devices are standard shared libraries with a known ABI. This enables several distribution patterns:

**User-compiled**: The author has Faust installed, writes `.dsp` files, and compiles them within MAGDA. The compiled libraries live in the project's `faust_cache/` directory.

**Pre-compiled sharing**: A user compiles a Faust device and shares the `.dylib`/`.so` along with the `.dsp` source. Recipients without the Faust toolchain can use the pre-compiled library directly. Recipients with Faust can recompile from source for their platform.

**Community library** (future): A curated collection of Faust devices that ship as pre-compiled binaries for each platform, installable from within MAGDA.

---

## Prerequisites

The Faust compiler must be installed separately by the user. It is not bundled with MAGDA.

**macOS**: `brew install faust`
**Linux**: Available via package managers or built from source ([github.com/grame-cncm/faust](https://github.com/grame-cncm/faust))
**Windows**: Installer available from [faust.grame.fr](https://faust.grame.fr/)

A system C compiler is also required for the shared library compilation step. On macOS this is provided by Xcode command line tools (already required for most development). On Linux, `gcc` or `clang` is typically pre-installed.

MAGDA should detect whether `faust` is available on the PATH and display a clear message in the script editor if it isn't, with installation instructions for the current platform.

---

## DSL Integration

Faust devices are fully addressable through MAGDA's DSL:

```
# Add a Faust effect to a track
track("Vocals").add_fx(type="faust", file="deesser.dsp")

# Set parameters by name
track("Vocals").device("deesser").set(threshold=-20, frequency=6000)

# Query parameter values
track("Vocals").device("deesser").get(threshold)

# Recompile after editing
faust.compile("deesser.dsp")

# List available Faust devices in the project
faust.list()
```

Since agents also communicate via the DSL, an AI agent can generate, modify, and insert Faust devices into the signal chain programmatically. For example, a user could say "add a gentle high-shelf boost to the vocals" and the agent could generate a Faust `.dsp` file, compile it, and insert it — all via DSL commands.

---

## Future Considerations

**In-process JIT (optional upgrade)**: If the few-second compilation delay becomes a pain point for sound designers doing rapid iteration, MAGDA could optionally link against `libfaust` for true instant hot-reload. This would be an opt-in build flag, not the default, so the base binary stays lean.

**Faust + OSC**: Faust has built-in OSC support via its architecture files. If MAGDA adds an OSC control layer in a future version, Faust devices could expose their parameters directly via OSC with minimal extra work.

**MIDI processing in Faust**: Faust supports MIDI input for polyphonic instruments and parameter control. A Faust instrument device (as opposed to an effect device) could receive MIDI from MAGDA's MIDI routing and act as a synthesizer in the signal chain.

**WebAssembly compilation**: Faust can compile to WebAssembly. If MAGDA ever ships a web-based preview or collaborative editing feature, Faust devices could run in the browser using the same `.dsp` source files.
