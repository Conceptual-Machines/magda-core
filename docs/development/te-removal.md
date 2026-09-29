# Tracktion Engine removal

Tracking: #2557, #2918–#2921. Implementation: PR #2928 into `dev/1.0.0`.

V1 uses only the native engine. The existing v0-to-v1 project translator and
4OSC-to-Poly-Synth conversion remain unchanged. JUCE 9 (#2922) and unrelated
instrument features are separate work.

## Native construction and services

`createDefaultAudioEngine` always constructs `MagdaAudioEngine`. The TE wrapper,
transport adapter, factory branch, engine preferences, startup engine prompt,
and `MAGDA_AUDIO_ENGINE` override are removed. The native engine and host are
always built.

The TE submodule, CMake target/link dependencies, compatibility-device archive,
and dual-engine parity benchmark are removed. JUCE modules are explicit build
dependencies. Groove assets keep their existing independent provenance.

`EngineHost` provides playback, session launching, recording, routing, automation,
insert capture, plugin state and editors. TE object mirroring, processors,
modifiers, rack synchronization, measurement plugins and session monitors are
deleted. Drum Grid UI binds to model pad chains, rendered native devices and
native meters. Live 4OSC editing is retired; its API reports the existing
translation route.

MIDI file import uses JUCE to read channel-separated notes, controllers and
pitch bend. Preview and actual import share the same result. Transient analysis
uses the native detector and file reader; `WarpMarkerManager` schedules analysis,
debounces sensitivity changes and rejects stale completions.

## Existing project translation

`SavedDeviceState`, `DeviceState`, legacy aliases, 4OSC translation and v1 project
copies retain their existing behavior. No additional project reader or migration
layer is introduced. `TracktionAudioSettings` is an existing JUCE-only reader of
old audio preferences; it has no TE dependency.

## Regression gates

TE-only implementation tests and the live TE oracle are deleted. Existing native
device-state/corpus, parameter-order, sampler, convolution, Faust, automation,
routing, session, recording and insert suites remain. The corpus still runs
through native block-size invariance tests. Direct expected waveform checks for
trimmed session launches are retained as native tests.

The shared JUCE test fixture now uses a headless native engine. Consumer tests
that only needed an AudioEngine test double use an engine-neutral fixture.
Native transport API and saved-state standalone targets remain available:

```sh
ctest --test-dir cmake-build-debug -R 'transport_api_native|saved_device_state_native'
```

Local app and full test-target qualification must pass before this cutover is
reported complete. CI covers supported macOS, Windows and Linux builds; existing
installed-app smoke projects and performance gates remain part of qualification.

## Dependency audit

```sh
rg -n '#include.*tracktion|tracktion::' magda tests
rg -n 'tracktion_engine|tracktion_graph' CMakeLists.txt .gitmodules magda/*/CMakeLists.txt tests/CMakeLists.txt .github scripts
```

Historical comments, documents, provenance notices and legacy fixture spellings
may remain. Executable TE branches and build/include/link dependencies may not.
