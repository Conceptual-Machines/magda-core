# MAGDA Debug Plugin

A VST3 instrument that fails on request, for reproducing a hosting bug without the plugin
that showed it (#3155). It plays a sine on the held note, so a working instance is audible.
Built with the tests, never shipped.

Faults come from `MAGDA_DEBUG_PLUGIN_FAULTS`, a comma-separated list read when each
instance is created:

| Fault | Effect |
|---|---|
| `max-instances=N` | The factory refuses a new instance while N are alive, so the host's load fails (#3150) |
| `slow-load=MS` | The constructor sleeps for MS milliseconds |
| `latency-change=N` | Reports N samples of latency from the first block on |
| `no-editor` | `hasEditor()` is false |
| `silent` | Outputs silence |
| `throw-on-state` | `setStateInformation` throws |
| `crash-on-editor` | Opening the editor aborts the process (manual runs only) |

## Manual repro

Build the tests (`make test-juce-build`), add
`cmake-build-debug/tests/debug_plugin/magda_debug_plugin_artefacts/Debug/VST3` to the
plugin scan folders, then launch with the faults set:

```sh
MAGDA_DEBUG_PLUGIN_FAULTS=max-instances=1 make run-console
```

Load "MAGDA Debug Plugin" on two tracks: the second slot should read "Failed to load".
