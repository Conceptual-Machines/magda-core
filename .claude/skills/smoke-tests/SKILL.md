---
name: smoke-tests
description: Build, add to, or change MAGDA's smoke projects in tests/smoke (#2781), the projects the install test opens, plays and checks on each OS. Use when adding a smoke project for a feature cluster, rebuilding the set after a format or device change, editing an expectation file, or wiring the smoke run.
---

# Smoke projects

`tests/smoke/<name>/<name>.mgd` plus `<name>.smoke.json` beside it, one feature cluster each.
The expectation format is documented in `tests/smoke/README.md`;
`tests/project/test_smoke_expectations.cpp` (tag `[smoke]`) validates every file. The run itself
is manual, per OS, against the installed build, never in CI.

## Running the set

```
python3 scripts/smoke.py                       # launches the installed MAGDA on the magda engine
python3 scripts/smoke.py --app <binary>        # a dev build
python3 scripts/smoke.py --attach sidechain    # the running MAGDA, left running
```

The client is `magda-smoke`; its first run asks the user once for edit, transport and session.
Results and audio land in `~/.magda-smoke/<machine>/<time>/`, compared with the last run of the
same engine. Capture and render never null (free-running oscillators), so they are compared on
energy envelopes: digital-silence runs, envelope drift, and the render's length against
`range.seconds`.

## Projects are built in code, never by hand

`scripts/build_smoke_projects.py` builds each project in a running MAGDA over its MCP endpoint and
saves it in place with `project.saveAs`. A project is a recipe function in `PROJECTS`; the
`.mgd` is its output and may be regenerated freely (unlike the legacy corpus).

```
make run-console                                   # in the background; wait for "Remote API listening"
python3 scripts/build_smoke_projects.py            # every project
python3 scripts/build_smoke_projects.py drum-grid  # just the named ones
python3 scripts/build_smoke_projects.py --discard sidechain   # after a failed run left a dirty project
```

MCP must be on with the edit scope granted to `claude-code` (both persist in MAGDA's config). The
script uses the repo's own client (`scripts/build_synthstack_session.py`), so it sees every
operation the running build has, even when this session's MCP tool list is stale.

The one exception is state only a plugin can set: `plugin-state` holds a Dexed patch chosen by
hand in the plugin. Its `.mgd` is its own seed; the recipe opens it and rebuilds the clip, and
the expectation's `parameters` block checks the patch comes back.

## Adding a project

1. Add a recipe to `scripts/build_smoke_projects.py` and register it in `PROJECTS`.
2. Run it for that project only, then decode the saved `.mgd` (zlib JSON) and check the feature
   actually landed: the link, the slot, the sidechain source. A save that succeeded is not proof.
3. Write `<name>.smoke.json`: range in beats, tracks that must sound or stay silent, `requires`,
   `parameters` for restored plugin state,
   one `listen` line, and a `scenario` if nothing sounds until something is launched.
4. `make test-build`, then `./tests/magda_tests "[smoke]"` from `cmake-build-debug`.

The core set uses only MAGDA's own devices and audio inside the project folder; anything naming a
plugin or hardware goes in `requires` and becomes the optional tier.

## API facts recipes rely on

- Look parameters up by name with `parameter_index`, never by a hard-coded index.
- The tempo lane is linear over 20..300 BPM: value = (bpm - 20) / 280.
- A new project already has scenes; read their ids from `session.get`. Session clips go in with
  `clips.createMidi` and a `session` placement naming `sceneId`.
- `pads.create` returns the pad's `midiNote`; write drum notes to that, not to GM numbers.
- Sidechain sources are routing endpoints such as `track:<id>`; `sidechains.set` takes the
  owning device's path.
- Devices inside a rack chain are added with `parentPath` = the chain's `nodePath` from
  `devices.list`.
- Audio goes in with `clips.loadSample`, then `clips.updateAudio` for playback, source BPM,
  stretch, reverse and fades. Copy the file into `<name>/<name>_Media/imported/` first (see
  `load_sample`): a source inside the project folder is saved relative, so the project moves.
  Test audio is generated, not sampled, and lives in `tests/smoke/assets/`.
- `project.saveAs` is a job; poll `jobs.get` until it completes. Save As wraps the file in a folder
  named after it, and the job result reports the path actually written.

## Not buildable yet

Freeze (it renders behind a modal progress window; the API needs a job path for it). Hardware
inserts are left to the hands-on checklist: a loopback run is too fragile for a smoke project.
