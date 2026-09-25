# MAGDA Remote API contract

The remote API is a transport-neutral contract above `MagdaApi`. WebSocket,
MCP, and future adapters consume `magda::remote::OperationRegistry`; they do
not define their own operation names or schemas and do not serialize MAGDA
core model objects.

Version 1.0 uses stable, domain-qualified operation names such as
`tracks.list`, `transport.seek`, and `session.launchClip`. Inputs are closed
JSON objects: unknown fields are rejected. Numeric values must be finite and
must satisfy the range in the operation's JSON Schema. Responses use one of:

```json
{ "ok": true, "apiVersion": "1.0", "result": {} }
```

```json
{
  "ok": false,
  "apiVersion": "1.0",
  "error": {
    "code": "validation_failed",
    "message": "Operation input validation failed",
    "issues": [
      { "path": "$.trackId", "code": "minimum", "message": "..." }
    ]
  }
}
```

`system.describe` exposes the version, operation catalogue, access mode,
required scope, and shared input/output schemas. A transport may add its own
correlation or framing metadata outside these envelopes, but it must not change
the contract payload.

Every operation declares one of five scopes — `read`, `edit`, `transport`,
`session`, `hardware-midi` — and a client that has not been granted it is
refused with `permission_denied` before its input is even validated. The default
for a client MAGDA has not seen is `read` alone. What that model does and does
not protect against is
[remote-api-permissions.md](remote-api-permissions.md); adding an operation
means adding it to the scope table there, and the registry refuses to start with
a write that has not been.

Two transports carry this today. WebSocket wraps it in JSON-RPC over a socket;
MCP projects operations into tools and read operations into `magda://` resources
— see [remote-api-mcp.md](remote-api-mcp.md). Both consume this registry, and
neither declares an operation or a schema of its own.

They are independent listeners: separate ports, separate bearer tokens, and a
separate switch each, so either can run without the other and either can be
restarted or re-credentialled while the other keeps serving. What they share is
everything above the socket — one `OperationRegistry`, one dispatcher, one
subscription hub, one client registry. Two dispatchers would be two revision
counters and two undo groupings over one project; two registries would be two
answers to "may this client edit".

(OSC is not one of these transports. It does not consume this registry, carries
no token, and has no place in the scope model — it is a control-surface protocol
that happens to arrive over a socket.)

Both also want to know who is calling, so the user can grant them different
things. A WebSocket client names itself in the upgrade's query string
(`ws://127.0.0.1:51734/rpc?client=my-tool`); an MCP client uses
`clientInfo.name`. Sending nothing is allowed and means anonymous, which is
read-only.

### Current-project save

`project.get` reports `dirty` and `hasSaveTarget` without exposing the target's
path. `project.save` is edit-scoped and writes only to that existing target. It
never opens a chooser; an untitled project fails with `conflict`, leaving Save
As an explicit in-app action. Saving changes persistence state rather than
project content, so a successful save does not advance the Remote API revision
or create an undo command.

### Project loop range

`project.setLoopRange` takes `startBeat` and `endBeat`, with a non-negative
start and an end strictly after it. It changes the persisted project range and
the active engine range together without changing whether looping is enabled.
The operation is edit-scoped, undoable in one step, and revision-neutral when
the requested range already matches the project.

### Device preset discovery

`devicePresets.list` takes the same `devicePath` used by the other device
operations and returns `id`, display `name`, folder-derived `category`, and
`source` (`magda` or `plugin`). It combines MAGDA device-state presets with
scanned VST3/AU presets. IDs are opaque and stable; filesystem paths and preset
state never cross the facade.

`devices.applyPreset` consumes one of those IDs for the addressed device. It
keeps the device identity, slot, bypass state, and unrelated graph intact, and
returns the updated safe device graph plus the structured reference-impact
plan. Parameter references move only when a stable identity proves the remap;
references that cannot be preserved make the request fail before its one
undoable commit, with the rejected plan in `error.details.referenceImpact`.
Reapplying identical state is revision-neutral.

### Saved track-chain presets

Saved track-chain presets are exposed through the shared operation registry, so
the same contract is available over WebSocket and MCP:

- `trackPresets.list` is a read operation. It returns only the preset's opaque,
  stable `id`, display `name`, and optional folder-derived `category`; it never
  exposes a path or serialized plugin state.
- `tracks.createFromPreset` is an edit operation. Its sole input is `presetId`.
  It creates one media track, restores the preset's settings and complete device
  graph, and returns the new `trackId` plus the normal safe `deviceGraph`
  projection.
- `tracks.applyPreset` is an edit operation over an existing `trackId`. It
  consumes only an opaque ID returned by `trackPresets.list`, replaces the
  preset-owned main/post-FX chain plus track macros and modulators, and preserves
  the addressed track's identity, clips, hierarchy, mixer/input/routing state,
  sends, and rail-managed analysis devices. The result contains the same
  `trackId`, the updated safe `deviceGraph`, and a structured `referenceImpact`.

Track-preset application prepares and re-keys the complete replacement graph
before one undoable commit. Existing references owned by the removed chain are
reported as dropped. External references are remapped only when a unique plugin
identity and, for parameters, a stable parameter identity prove the target;
ambiguous or unproven targets reject the entire operation. A rejection is
revision-neutral and returns its plan at `error.details.referenceImpact`.

Creation and application are each one undoable mutation. Like every remote
write, both support the transport's `expectedRevision` and `requestId` metadata;
neither value is part of the operation payload. Preset IDs are addresses only:
clients cannot supply a filesystem path or native state blob.

### Reference-impact preflight

Operations that replace a device or a track's device chain must build a complete
reference-impact plan before they mutate the project. The shared inventory
covers automation lanes, macro links, modulator links, resolved controller or
alias bindings, device and rack sidechains, sends, track-to-track inputs, and
multi-output child links. A caller selects the references whose source or
target lies in the replaced graph and assigns each class one explicit policy:
`reject`, `preserve`, `remap`, or `drop`.

A remap is valid only when the old and new targets carry the same non-empty
stable identity. In particular, equal numeric parameter indices are not
evidence of compatibility. Missing mappings, missing identities, and identity
mismatches become rejected plan entries, and a plan with any rejection cannot
commit. Planning is pure over a snapshot; loading or validation failure cannot
change project state.

The result contract reports `preservedReferences`, `remappedReferences`,
`droppedReferences`, and `rejectedReferences`. Every entry carries a reference
kind, safe structured source and target addresses, and a reason code; remaps
also carry `newTarget`. Addresses can contain public track/node/lane/macro/mod
IDs, opaque binding IDs, routing roles, and public parameter stable IDs. They
cannot represent pointers, filesystem paths, raw plugin identifiers, or plugin
state. All four collections and every nested address are closed schemas.

### Track display and input state

`tracks.update` accepts `colourArgb`, `recordArmed`, and `inputMonitor` in
addition to its mixer and naming fields. Input monitoring uses the stable
`off`, `in`, and `auto` values and does not expose a physical device identifier.
The same value is projected by `tracks.get` and `tracks.list`.

A multi-field patch is one undo action. Restating the current values is a
revision-neutral no-op. Record-arm and input-monitor changes are rejected for
track types that do not accept external input, before any other field in the
patch is applied.

### Singleton chord track

The chord track is project-wide singleton state rather than a repeatable track
kind. `chordTrack.get` returns `{ track, chords }`: `track` is the normal safe
track projection or `null` when absent, and `chords` is chronological. Each
chord exposes its owning `clipId`, clip-relative `clipBeat`, absolute
`startBeat`, `lengthBeats`, and display `name`. Internal chord-group IDs that
link annotations to generated voicing notes never cross the API boundary.

`chordTrack.ensure` is an edit-scoped, undoable creation operation. It returns
the existing snapshot without changing the revision when the singleton already
exists. Generic `tracks.create` rejects the `chord` type with a conflict so a
caller cannot create a second chord track or bypass the singleton contract.

### Nested racks and chains

`devices.list` gives every rack and rack chain a canonical `nodePath`, using the
same safe path shape as devices. The full route is required because racks can
nest arbitrarily; an immediate `rackId`/`chainId` pair is not an address once a
rack contains another rack.

`racks.create`, `racks.remove`, and `racks.update` accept those paths at any
depth. Top-level `racks.create`/`remove` inputs remain supported for existing
clients. `chains.create`, `chains.remove`, and `chains.update` address the rack
or chain directly. Rack updates cover bypass and output volume; chain updates
cover name, output, mute, solo, bypass, volume, and pan.

Each successful mutation is one undo step. Creating a rack or chain preserves
its allocated identity through undo/redo, removing one restores its complete
subtree, and a property patch that restates current values is revision-neutral.

### Clip placement and duplication

`clips.move` and `clips.duplicate` require an explicit, view-specific
`destination`. Arrangement destinations contain `view: "arrangement"`,
`trackId`, and `startBeat`; session destinations contain `view: "session"`,
`trackId`, and `sceneIndex`. The destination view must match the source clip's
current view. Moving between arrangement and session is a separate conversion,
not an implicit side effect of placement.

`clips.resize` takes `lengthBeats` and an `edge` of `start` or `end`. All three
operations are edit-scoped and commit as one undo action. A move that restates
the current destination and a resize that restates the current length are
successful no-ops and do not advance the revision. A session destination must
be empty, except that moving a clip to its own current slot is a no-op.

### Automation lane writes

`automation.setPoints` replaces the complete point set on an absolute lane as
one undoable mutation. Input points contain beat position, normalized value,
and curve type; point IDs are allocated by MAGDA and returned only in the lane
projection. Repeating the same curve is a revision-neutral no-op. Clip-based
lanes reject this operation because their points belong to automation clips.

`automation.deleteLane` removes the lane and any automation clips it owns as
one undoable mutation. Both operations require the `edit` scope and participate
in the normal `expectedRevision` and `requestId` handling.

Clip-based lanes expose their contents through `automation.listClips` and
`automation.getClip`. The safe projection contains IDs, display metadata,
timeline bounds, looping state, and normalized points; it contains no engine or
plugin state.

`automation.createClip`, `automation.deleteClip`, `automation.moveClip`,
`automation.resizeClip`, and `automation.duplicateClip` each commit one undoable
timeline mutation. `automation.updateClip` atomically updates any combination
of name, colour, looping, loop length, and the complete local point set. Point
IDs are allocated by MAGDA. No-op moves, resizes, and updates are
revision-neutral, and points outside the clip's local beat range are rejected
before mutation.

### MIDI event CRUD

MIDI clips expose one stable per-clip ID space across notes, keyswitch notes,
control changes, pitch bend, channel pressure, and polyphonic aftertouch.
Keyswitches use the `note` event shape with `"keyswitch": true`; they remain
ordinary notes on the MIDI wire while retaining their authored role.

- `clips.listMidiEvents` reads the complete typed event list.
- `clips.addMidiEvents` atomically appends one or more events and assigns IDs.
- `clips.updateMidiEvents` atomically replaces the events named by `id`. An event
  may change type while retaining its ID.
- `clips.deleteMidiEvents` atomically removes the supplied `eventIds`.
- `clips.replaceMidiEvents` atomically replaces the complete list and assigns
  fresh IDs. Deleted IDs are never recycled.

Every mutator validates its whole request before changing the clip and commits
as one undo action. Passing a one-element `events` array is the singular create
or update form; the contract does not duplicate those operations with separate
singular names. Optimistic concurrency and idempotency use the same transport
`expectedRevision` and `requestId` metadata as every other write.

## Subscriptions

Ten topics partition what a client can watch: `project`, `tracks`, `clips`,
`devices`, `selection`, `transport`, `session`, `automation`, and the two
continuous ones, `meters` and `playhead`.

`subscriptions.subscribe`, `.unsubscribe`, `.list`, and `.resync` are declared in
the registry like any other operation and marked `transportScoped: true`. They
are executed by the transport adapter rather than by the dispatcher, because
what a connection watches is state only that connection has. Dispatching one
through a transport that cannot push fails with `invalid_request` rather than
`unknown_operation` — the operation is real, and only the route is wrong.

A pushed change is one envelope, independent of the transport that carries it:

```json
{ "topic": "clips", "type": "delta", "revision": 57, "payload": {} }
```

`type` is one of:

- `snapshot` — complete state for the topic, in the same shape as the topic's
  read operation (`tracks` is `tracks.list`, `project` is `project.get`, and so
  on). Delivered in the reply to `subscribe` and `resync`, so a client is never
  subscribed without knowing what it is watching, and pushed as an event when a
  client has fallen behind.
- `delta` — what changed since the previous event on that topic. For `tracks`,
  `clips`, `automation`, and `session` the payload is
  `{"added": [], "updated": [], "removed": []}`, where `removed` carries
  identities only — an id, or `{trackId, sceneIndex}` for a session slot. Apply
  `added` and `updated` as upserts keyed by id: a client may legitimately be sent
  a change it already has, and doing so must be harmless. For `project`,
  `transport`, `selection`, and `devices` the payload is the topic's full state,
  because there is nothing useful to diff.
- `sample` — a point reading of `meters` or `playhead`. Latest value wins,
  intermediate readings are discarded, and a dropped sample is never resent.

`revision` orders the stream and is the same counter `expected_revision` uses.
Continuous motion — a parameter following an LFO, a drag preview — publishes
events without advancing it, so equal revisions on consecutive events are
expected and mean "nothing was committed".

`subscribe` always delivers snapshots, including on a reconnect, and
`subscriptions.resync` forces the same thing at any time. There is no "resume
from revision N": `revision` counts committed mutations, while events are also
published for motion that commits nothing, so a client that disconnected and
missed one of those has a revision indistinguishable from a client that saw it.
A cursor that cannot tell those apart cannot be used to skip state.

A client that knows it already has state — because it is about to resync itself,
or only cares about what happens from now on — passes `"snapshot": false`. That
is the client asserting it, which is the difference: being wrong about it is then
its own choice rather than the server's guess.

Opening a different project invalidates every discrete topic at once, so a
client watching only `tracks` hears about the swap rather than continuing to
show the outgoing project's contents.

Delivery is bounded rather than buffered. A client that stops reading has its
events dropped and is marked for resync; the snapshot it is owed is retried on
its own until it is taken, so a client that fell behind is not left stale by the
project happening to go quiet. One that never resumes is disconnected.
Subscribing to `meters` or `playhead` costs nothing until asked for: nothing
samples them otherwise.

## Deliberately excluded data

Two separate mechanisms keep data out of the remote API, and they answer
different questions. Scopes decide *who may ask*; the exclusions below decide
*what exists to be asked for* — they hold for every client, at every scope, and
there is no permission that reveals them.

DTO fields are allow-listed. In particular, the remote API does not expose:

- project, audio-source, MIDI-source, plugin, render, or cache file paths;
- physical audio/MIDI device identifiers (logical `track:N`, `master`,
  `default`, and `all` routing tokens are retained);
- native plugin state, preset blobs, plugin filesystem identifiers, or raw
  plugin identity strings;
- pointers, engine objects, manager objects, or host/plugin instances;
- raw device parameter internals, wrapper parameters, kit internals, or
  transient loading objects. Device-owned mods and macros are projected through
  explicit read DTOs; plugin state and engine instances remain private;
- transient AI conversations, AI output, prompts, or model state;
- UI layout and expansion state, zoom/scroll state, active panels, parameter
  pages, editor grids, playhead caches, waveform/transient caches, and
  rail-managed mixer analysis devices;
- opaque project aliases/bindings or persistence timestamps.

Tracks, clips, and devices are projected into compact value DTOs. Nested device
racks are represented as flat `devices`, `racks`, and `chains` arrays joined by
stable IDs. This preserves structure without exposing recursive core objects.

Adding a field is an API change: update the DTO, encoder/decoder, output schema,
projection, exclusion review, and round-trip tests together.
