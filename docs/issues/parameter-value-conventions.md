# Explicit parameter value conventions (#2623)

`ParameterInfo::valueConvention` states what `currentValue` and `defaultValue`
mean. `Real` stores authored/display units, as internal devices do;
`Normalized` stores a plugin position in `[0, 1]`. The display range, scale,
choices, and text formatter do not select this convention.

This distinction matters even when two parameters have identical metadata.
A hosted cutoff with a configured 100–1100 Hz range and stored value `0.5`
is at the midpoint. An internal cutoff stores `600` for that same displayed
value. Detaching a text formatter, saving a project, or changing the display
range must never reinterpret either stored value. That previously caused
hosted instruments to receive incorrect values and led to format-specific
workarounds in the parameter table and controller code.

## Boundaries to preserve

- Producers declare the convention when constructing parameter metadata.
  Internal catalogs use real values; hosted descriptions use normalized
  values. Wrapper controls keep their own declared convention.
- Copies and project persistence preserve the declaration. Legacy records
  without it are interpreted at the owning device's load boundary, where the
  plugin format is known, rather than guessed by a general converter.
- Display configuration changes presentation metadata, not stored units.
- Controllers, automation, sound design, and parameter-table compilation use
  the shared model/normalized/real conversions.
- A legacy Tracktion raw value is a separate boundary. Internal parameters
  whose native range is already in real units must retain that value, including
  nonlinear slider scales. The model declaration does not redefine a
  Tracktion parameter's native range.

## Regression checks

Exercise a hosted parameter with a configured Hz or dB range both with and
without its text formatter. Verify model values, controller feedback, defaults,
and plugin delivery. Repeat with an internal parameter using real units and a
nonlinear scale. Discrete hosted choices must still span the full normalized
range. Round-trip new projects and load legacy records without the declaration.

The native controller integration fixture and sound-designer tests also cover
the provider-free case introduced by #2655. Live checks should use
`make run-console`: load a configured plugin, move its control and a learned
MIDI control, then save/reopen and check the value and sound again. Automated
tests do not establish audible correctness for a particular third-party plugin.

## Validation (2026-09-17)

`make debug` and `make release` pass. Release uses `MAGDA_BUILD_TESTS=OFF`
and `MAGDA_FAUST_BACKEND=interp`, matching the local Debug Faust backend.
The focused Catch selection covering parameters,
automation, device state, project serialization, controllers, and remote APIs
passes 900 cases / 28,217 assertions. Five JUCE suites (Native Controller
Parameter Tests, Hosted Parameter Edits, Controller Track Level Write Tests,
Param Slot Discrete Combo Tests, and Param Slot Boolean Sync Tests) pass
35 cases / 130 assertions. Formatting and diff checks pass.

The separate legacy External Plugin State Restore suite is environment-dependent:
its sandboxed run selected Splice Sounds and failed two voice comparisons;
outside the sandbox it selected DLSMusicDevice and skipped the substantive
restore checks because that instrument had too few programs. That suite does
not provide restore validation for this change. Stub-plugin state restore is
covered by the native engine Catch tests; a live third-party save/reopen check
remains part of manual verification.
