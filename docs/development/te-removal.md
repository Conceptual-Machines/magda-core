# Tracktion Engine removal

Done. Tracktion Engine (TE) is removed and v1 runs only on the native engine
(`docs/architecture/native-engine.md`). Tracked in #2557 and #2918-#2921; landed in
#2928, with the follow-up cleanup and docs in #2929 and #2943.

What went: the TE submodule and CMake link dependencies, the TE wrapper and transport
adapter, the engine preference and startup prompt, the compatibility-device archive,
the TE-only tests and the dual-engine parity benchmark. Live 4OSC editing is retired.

What stayed: the v0-to-v1 project translator, 4OSC-to-Poly-Synth conversion,
`SavedDeviceState` and legacy aliases. `TracktionAudioSettings` reads old audio
preferences with JUCE only. Groove assets keep their own provenance.

Check for regressions:

```sh
rg -n '#include.*tracktion|tracktion::' magda tests
rg -n 'tracktion_engine|tracktion_graph' CMakeLists.txt .gitmodules magda/*/CMakeLists.txt tests/CMakeLists.txt .github scripts
```

Historical comments and legacy fixture spellings may match. Executable TE branches
and build, include or link dependencies may not.
