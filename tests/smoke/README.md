# Smoke projects

Projects the install test opens, plays and checks on each OS (#2781), one
feature cluster each. `scripts/smoke.py` drives them over the WebSocket Remote
API against the installed build; nothing here runs in CI except the check that
every expectation file is well formed.

## The core set and the optional tier

A project whose `requires` is empty is in the core set. It uses only MAGDA's own
devices and audio kept in its own project folder under a relative path, so it loads and
plays the same on macOS, Windows and Linux. A project that names a plugin or
hardware is in the optional tier: it runs where the machine has what it needs
and is reported as skipped where it does not.

Unlike the legacy corpus, these files may be resaved: a smoke project is
designed material, and one that needs another bar or another device gets it.

## Expectation files

Each project is the folder MAGDA saves, `<name>/<name>.mgd`, with a
`<name>.smoke.json` beside the `.mgd`:

```json
{
  "version": 1,
  "project": "faust-devices.mgd",
  "cluster": "faust",
  "range": { "startBeat": 0, "endBeat": 8 },
  "requires": { "plugins": [], "hardware": [] },
  "tracks": [
    { "name": "Kick", "sound": true, "peakDb": { "min": -48, "max": 0 } },
    { "name": "Muted Marimba", "sound": false }
  ],
  "listen": "Kick on every beat; the muted Marimba stays out."
}
```

| Key | Meaning |
| --- | --- |
| `version` | Format version, `1`. |
| `project` | The `.mgd` beside this file. |
| `cluster` | The feature cluster the project exercises. |
| `range` | The beats to play, `startBeat` < `endBeat`. |
| `requires.plugins` | Plugin names the machine must have scanned. Empty for the core set. |
| `requires.hardware` | Hardware the run needs: `insert` (an external insert with a loopback cable) or `loopback`. Empty for the core set. |
| `tracks` | Tracks checked by name. A track not listed is not checked. |
| `tracks[].sound` | `true`: the track must sound in the range. `false`: it must stay silent. |
| `tracks[].peakDb` | Bounds on the track meter's peak over the range, in dBFS. Required when `sound` is `true`. A silent track's peak must stay below -90 dBFS unless it gives its own `max`. |
| `scenario` | Optional. Remote API calls made during playback, each `{ "beat", "call", "input" }` with the beat inside `range`, such as launching a scene. |
| `listen` | One line on what a listener would check, printed in the report. |

`tests/project/test_smoke_expectations.cpp` loads every expectation file and
fails on a missing key, a wrong type, a project that does not load, a track
name the project does not have, or a core project that hosts a plugin or points
at audio outside its folder.
