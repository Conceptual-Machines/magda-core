# Native controller and sound-designer parameters (#2655)

The native engine has no `AudioBridge`. Controller and OSC initialization must
therefore use the shared model/engine parameter API without requiring that
bridge. The old startup condition dropped MIDI binding writes and skipped the
entire OSC service, including feedback.

## Ownership and units

An addressed hosted parameter has a model-owned base value. Controller writes
update that base through `TrackManager`; OSC feedback reads the same base,
rather than echoing a value moving under modulation. The base is a normalized
position even when the configured display range is in Hz or dB. Internal
devices retain their existing model-unit conversions.

An ordinary hosted parameter may have no `DeviceInfo::parameters` entry.
`deviceParameterList` enumerates the live instance for these parameters. A
one-off edit uses the described-parameter `TrackManager` overload and the
native hosted-edit path, without making the parameter part of the document.
Never fill the whole model just to make a control visible or writable.

Static binding additions and removals notify the host that its parameter set
may have changed. Dynamic name resolution queries the live parameter catalog
through `ChainContext`. Both paths preserve `paramIndex`; vector positions are
not hosted parameter identities.

The sound designer enumerates that same catalog, filters it to the user's
opted-in parameter slots, and describes values in display units. Its returned
values are converted back to model units before applying them. Enumeration and
conversion tests do not by themselves establish delivery to a plugin.

## App verification

Use `make run-console` and a hosted plugin. Learn a MIDI knob onto a plugin
parameter, move it, and check both the plugin and MAGDA control. Repeat with a
configured display range. Check an OSC binding and its feedback with OSC
enabled. For sound design, select a parameter not already automated or bound
and confirm a generated change reaches the plugin. Also check a track fader
and an internal-device parameter through the controller.

Automated parameter and binding tests can check routing and units; hardware
MIDI/OSC setup and the live user workflow still need an app check.

## Automated validation (2026-09-17)

The Debug app, Catch test binary, and JUCE test binary build successfully.
The focused Catch selection (`[2655],[controllers],[aliases],[osc],
[param-config-store]`) passes 329 cases / 15,188 assertions. The JUCE suites
Native Controller Parameter Tests, Controller Track Level Write Tests,
Engine Host Publish Tests, Hosted Parameter Edits, and OSC Service Tests pass
65 cases / 328 assertions. Configuration-file and OSC socket tests require
access beyond the restricted execution sandbox.

The native controller fixture has no AudioBridge and uses an actual
EngineExternalDevice wrapping a test plugin. It checks MIDI routing and OSC
writes to an addressed normalized parameter with a configured Hz range, plus
OSC and sound-designer edits to an unaddressed parameter. Those one-off edits
must reach the plugin when it processes audio without populating the model.
Separate host tests check that binding changes request a parameter refresh.
These fixtures do not replace a manual check with a real controller/plugin.

The branch was rebased onto `dev/0.20.0` at `e8524496`, then `make debug` and
the same 394 focused cases passed again. `make release` also passes with
`MAGDA_BUILD_TESTS=OFF` (the release workflow setting) and
`MAGDA_FAUST_BACKEND=interp`, matching the local Debug backend. The initial
Release configurations hit an assertion-only Tracktion variable when tests
were enabled, then missing WASM factory symbols in the local Faust library;
neither configuration failure required a source change in this fix.
