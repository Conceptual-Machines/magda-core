# Issue #1756 — theming groundwork handoff

Branch: `dev016`

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
also resolves role-matched explicit colours during painting. Preserve the
source keys in assets so multi-layer icons, especially transport controls,
retain their state information.

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

## Suggested next pass

1. Convert remaining fixed shared-control literals in `TextSlider.hpp`,
   `Toast.cpp`, and `QwertyKeyboardPopup.cpp`. Add semantic roles only when an
   existing role would alter the Dark output.
2. Triage custom UI literals:
   - `EqualiserUI.cpp`: fixed EQ-band identity colours; decide whether to add
     four semantic EQ roles.
   - `NimbusUI.cpp`, `HaloUI.cpp`, `MateriaUI.cpp`, and
     `StruckInstrumentUI.cpp`: shared surface literals are complete; assess
     the remaining source-specific accents individually.
   - `AnalyzerColours.hpp`: leave persisted trace choices user-owned.
3. Treat code-editor/tokeniser colours (`DSLTokeniser`, `ChatPromptTokeniser`,
   AI console) as a separate syntax-highlighting palette rather than folding
   them into generic application roles.
4. Continue replacing direct SVG copies that are only tinted at construction
   time; repaint-time mapping or `lookAndFeelChanged()` reloads are required
   for a live theme switch.
5. Run the validation commands above after each palette expansion, then do a
   manual Preferences theme switch once the next broad UI layer is converted.

## Files to orient from

- `magda/daw/ui/themes/DarkTheme.hpp/.cpp`
- `magda/daw/ui/components/common/SvgButton.hpp/.cpp`
- `magda/daw/ui/windows/MainWindow.hpp/.cpp`
- `magda/daw/core/Config.hpp/.cpp`
- `tests/test_runtime_theme_juce.cpp`
