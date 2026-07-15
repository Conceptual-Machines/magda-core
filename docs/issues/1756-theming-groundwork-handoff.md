# Issue #1756 — theming groundwork handoff

Branch: `issue/1756-theming-groundwork`

## Current state

The runtime palette foundation is in place and the current Dark appearance is
preserved for every converted role. `DarkTheme` now exposes `ColourRole`, an
active `Palette`, Dark and High Contrast preview palettes, and lookup helpers
that retain the existing `DarkTheme::getColour(...)` call style.

Theme selection is persisted in `Config`, exposed in Preferences, applied at
startup, and refreshed live from `MainWindow` through `ConfigListener` and a
recursive look-and-feel change. The High Contrast choice is deliberately a
preview palette, not the final light theme.

Converted areas include the priority automation and piano-roll controls,
chain slots, modulation and curve editors, panel content, SVG buttons and
other shared icons, Gate/Multiband visualisers, sampler markers, and spectrum
overlays. The Nimbus, Halo, Materia, and struck-instrument faceplates now use
live roles for their shared background, panel, border, and text layers. Their
instrument-specific accent colours remain intentionally distinct.

The shared-control cleanup is also complete: text sliders, toasts, QWERTY
keyboard labels, EQ band identities, code-editor syntax colours, and remaining
shared SVG source keys have dedicated runtime roles. Syntax highlighting uses
its own palette rather than application-surface roles.

`ThemedColour` in `DarkTheme.hpp` is the preferred lightweight bridge for a
custom UI that currently has file-local named `juce::Colour` constants. It
resolves the role at paint time. For cached label or slider colours, add a
`lookAndFeelChanged()` override that reapplies the role-derived colours.

## SVG policy

Do not request re-exported transparent or colourless SVGs. Existing source
fills are stable implementation keys:

- `#B3B3B3` — neutral glyph
- `#BCBCBC` — transport glyph
- `#1A1A1A` / `#1E1E1E` — background layers
- `#444444` / `#555555` — frames and guides
- known blue, purple, red, and orange fills — semantic states

`DarkTheme::applyToSvgIcon()` maps those keys to active roles. `SvgButton`
also supports per-state source-key replacement for both single- and dual-icon
controls. Background and glyph layers can therefore resolve independently on
every paint, including when a light palette inverts their contrast.

Colour-only state pairs have been consolidated to one SVG. This includes all
transport controls plus solo, track record, chord audition, note slice, time
bend, and resume. The remaining `master`, `monitor`, and `toggle` pairs are
intentional: their state changes geometry or control position, while their
colours are still supplied by semantic roles in code.

## Keep these colours user-owned

Do not route these through the global palette:

- stored track and clip colours (`deriveTrackSwatch` must remain based on the
  user-selected hue)
- selected analyzer trace colours from `AnalyzerColours.hpp`
- any explicit user colour picker value

For device-specific identity accents, make a deliberate call per device. The
shared faceplate surfaces should theme; the accent may stay device-specific if
it encodes a synthesis source or band identity.

## Important build detail

`DarkTheme.cpp` must remain a source of the shared `magda_daw` library in
`magda/daw/ui/themes/CMakeLists.txt`. It was originally only in the app target,
which caused CLI and test link failures after runtime lookups were introduced.
Do not add it back to the JUCE test source list separately, or it will create
duplicate definitions.

## Validation

From the repository root:

```sh
cmake --build cmake-build-debug -j 6
cmake-build-debug/tests/magda_juce_tests_artefacts/Debug/magda_juce_tests "Runtime Theme Tests"
git diff --check
```

The full build and focused runtime-theme suite pass at this handoff. In the
sandbox, the build may need permission to write ccache temporary files under
the user cache; that is an environment restriction, not a project error.

## Merge state

The groundwork scope is complete. A future light theme only needs to provide
values for the existing semantic application and syntax palettes; controls do
not need new SVG exports or duplicated state assets. Before merging, run the
validation commands above and smoke-test the Preferences theme switch in the
application when a desktop session is available.

## Files to orient from

- `magda/daw/ui/themes/DarkTheme.hpp/.cpp`
- `magda/daw/ui/components/common/SvgButton.hpp/.cpp`
- `magda/daw/ui/windows/MainWindow.hpp/.cpp`
- `magda/daw/core/Config.hpp/.cpp`
- `tests/test_runtime_theme_juce.cpp`
