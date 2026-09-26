# MAGDA Remote API contract

The remote API is a transport-neutral contract above `MagdaApi`. WebSocket,
MCP, and future adapters consume `magda::remote::OperationRegistry`; they do
not define their own operation names or schemas and do not serialize MAGDA
core model objects.

The decision against a generic `batch.execute` surface is recorded in
[remote-api-batch-decision.md](remote-api-batch-decision.md).

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

### Engine diagnostics

`engine.health` and `meters.read` are read-scoped, revision-neutral snapshots.
They require no subscription and never expose logs, device paths, or plugin
state. `observedAtMs` is a Unix millisecond timestamp for the read.

`engine.health` reports the running engine, whether a project is bound, whether
an audio interface is open, callback load, and xruns since the current project
was loaded. `callbackLoad` is JUCE's approximate 0–1 share of the callback
deadline. `sinceMs` marks the project boundary. The bounded `problems` list
records observed xrun increments, counter resets, and unavailable audio devices;
`discardedProblemCount` reports older entries removed after 32. A separate
dropout count is currently unavailable from either audio I/O backend and is
reported as `null`. `problemCoverage` says `audioIoObservations` because other
engine problems have no shared event source yet. Other unavailable metrics are
also `null`, never zero.

`meters.read` reports the latest published left and right peaks for up to 128
addressable tracks, plus the master. Entries with no published sample set
`available` to false and their levels to `null`; `truncatedTrackCount` reports
additional tracks omitted from the bounded payload. Remote subscriptions and
one-shot reads share the same latest-value snapshot, so neither consumes data
needed by the other. A sample older than one second is unavailable rather than
presented as a current level.

### Asynchronous jobs

Long-running project, render, capture, and plugin-heavy work shares one job
contract. A producing operation accepts work with an opaque `job_…` id and a
captured project revision, then moves it through `accepted`, `running`, and
exactly one of `completed`, `cancelled`, `failed`, or `unsupported`. Progress is
monotonic from 0 to 1. A cancellation is terminal immediately: a worker that
finishes late cannot overwrite it with success or publish a complete artifact.

`jobs.list` and `jobs.get` are read-scoped. `jobs.cancel` is revision-neutral
control, not an undoable project edit; ownership is checked first and the caller
must still hold the scope captured by the producing operation. Job ids are
connection-owned, so another connection receives `not_found` rather than being
allowed to inspect or cancel the work. Active work is cancelled and all of its
records are forgotten when that owner disconnects. Completed records are kept
for at most ten minutes and the newest 128 terminal jobs.

Project-bound jobs default to `stable_until_completion`: the completion revision
must equal the acceptance revision before a project transition or final artifact
is published. A producer that prepares a complete immutable snapshot may
explicitly declare `check_at_start_only`. Project replacement cancels all
project-bound work. Job results are operation-defined safe objects capped at 64
KB; errors must be transport-safe and artifacts carry only opaque `artifact_…`
ids, kind, and media type—never a filesystem path or native engine object.

The `jobs` subscription topic is owner-specific and revision-neutral. Its
snapshot is `jobs.list`; updates send the owner's complete bounded list so a
shared subscription baseline can never leak another connection's work. MCP also
projects the same list as `magda://jobs`.

### Current-project save

`project.get` reports `open`, `dirty`, and `hasSaveTarget` without exposing the target's
path. `project.save` is edit-scoped and writes only to that existing target. It
never opens a chooser; an untitled project fails with `conflict`, leaving Save
As an explicit in-app action. Saving changes persistence state rather than
project content, so a successful save does not advance the Remote API revision
or create an undo command.

### Project lifecycle

`project.new` creates an untitled project; `project.close` closes the current
project. Both require the `edit` scope. A dirty project is refused with
`conflict` unless the request explicitly sets `discardUnsavedChanges: true`.
These operations never open a dialog or file picker. Closing an already closed
project succeeds without changing the revision. Creating a project while none
is open succeeds. Both return the same safe status as `project.get`; when closed,
`open` is false and no project file path is exposed.

A successful transition clears project undo history and old idempotency entries,
cancels queued requests and project-bound jobs, advances the revision once,
and sends fresh snapshots for all subscribed discrete topics. Lifecycle
transitions are not undoable edits. A retry with the transition's request ID
replays its result until the next project boundary.

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

`devices.replace` takes an existing `devicePath`, a `catalogId` from
`devices.catalog`, and an optional opaque `presetId`. It stages the replacement
and preset while the incumbent is still live, then exchanges them in the same
chain slot as one undoable edit. The response contains the replacement's new
path, the safe device graph, and a complete reference-impact plan. Parameter
targets are remapped only when the device identity is compatible and matching
non-empty parameter stable IDs prove the target; owned links and sidechains are
reported as dropped, while unproven automation, macro, modulator, binding, and
routing targets reject the operation before commit. Filesystem paths, raw
loader identifiers, and plugin state never cross the API.

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

### Track routing

Track audio and MIDI I/O use three shared-registry operations:

- `routing.endpoints.list` returns the currently selectable endpoints and any
  selected-but-unavailable endpoint. Each entry has an opaque or logical `id`,
  display `name`, `media`, `direction`, `kind`, availability, channel count,
  and an optional public track ID. Physical backend identifiers never cross the
  facade. The four contextual None choices are explicit IDs such as
  `none:audio:input`; `track:N`, `master`, `default`, and `all` remain logical
  IDs.
- `routing.get` projects the four endpoint IDs for one track together with the
  existing `recordArmed` and `inputMonitor` state. Those two fields are
  informative: their write path remains `tracks.update`, so routing does not
  create parallel arm or monitor controls.
- `routing.set` is an `edit`-scoped patch over any combination of the four
  route fields. It accepts only available IDs from discovery in the matching
  media and direction.

The complete requested graph is preflighted before mutation. Unsupported track
or engine combinations, unavailable endpoints, mutually exclusive audio/MIDI
inputs, and mixed audio/MIDI feedback cycles fail without changing the model or
revision. A route that must replace another connection reports that connection
in `droppedConnections`. A successful compound edit, including cascades to
another track, commits as one undo action and advances the revision once; a
no-op advances neither. Both transports use their normal `expectedRevision`
and `requestId` metadata for the operation.

### Track sends

Track sends use four shared-registry operations:

- `sends.list` returns a track's sends. Each send has a stable opaque `id`, the
  public `sourceTrackId`, a logical `destinationEndpointId`, normalized `level`,
  `enabled`, and a `position` of `pre_fader` or `post_fader`.
- `sends.create`, `sends.update`, and `sends.remove` are `edit`-scoped
  mutations. Destinations are accepted only as currently available audio-input
  track endpoint IDs such as `track:N`; backend identifiers and raw bus indices
  never cross the facade.

Every mutation preflights track and engine compatibility, destination
availability, duplicate sends, the per-track send limit, and feedback cycles
before changing the model. A successful mutation is one undo action and one
revision. Restating the current send is a revision-neutral no-op, and rejected
requests leave both model and revision unchanged. Stable send identity survives
undo, redo, and project round trips.

Replacing a destination and removing a send report affected connections in the
closed `invalidatedConnections` collection. Durable references to a send level
are remapped when its destination changes; removal is rejected while such a
reference remains. Both transports use their normal `expectedRevision` and
`requestId` metadata for all three mutations.
### Device and rack sidechains

`sidechains.list` enumerates devices and racks (optionally for one track), while
`sidechains.get` inspects one owner by canonical `ownerPath`. Each result carries
current state beside capabilities derived from that live owner: supported
audio/MIDI types, declared audio width, tap points, trim/listen support, and
channel mappings. Sources use logical `track:N` IDs; plugin bus identifiers and
backend routing IDs never cross the facade.

`sidechains.set` is an edit-scoped patch. A string `sourceEndpointId` sets a
logical source and `null` clears the complete configuration. Type, pre-FX or
post-fader tap, -60..+24 dB trim, enabled/listen state, and a supported channel
mapping may be updated together. Devices advertise their declared audio port
and MIDI capability. Racks accept audio or MIDI for their own triggers but do
not advertise device-key tap, trim, listen, or channel-map controls. Both
engines currently expose only `automatic` channel mapping; a future mapping
must be advertised before it can be requested.

The complete patch is validated before mutation: owner and source must resolve,
media must be compatible, and the proposed edge must not close a cycle through
track inputs, outputs, sends, hierarchy, multi-output links, or another active
sidechain. A disabled sidechain retains its configured source. Removing that
source track clears it; replacing or removing its owning device/rack removes it
with the owner. Source changes and clears return the structured reference-impact
plan from the shared reference inventory.

One successful patch is one undo action and advances the revision once. A
failure changes nothing, and an identical patch is revision-neutral. WebSocket
and MCP share the handler, closed schemas, `expectedRevision`, and `requestId`
behaviour.

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

`chordTrack.detect` reads an explicit clip-relative beat range using a bounded
scan window. It is revision-neutral and returns structured root/quality data,
confidence, and closed warning codes rather than internal chord-group IDs.
`chordTrack.extract` applies that detection to the singleton track at an
explicit arrangement beat. Its `populatedPolicy` is `fail`, `replace`, or
non-overlapping `merge`; a successful extraction is one undo action and returns
both the new clip ID and the resulting chord-track snapshot.

`chordTrack.sendToTrack` addresses one chord-track clip and bakes its source
voicing into a plain MIDI clip on a regular track. The caller must select an
occupied-range policy (`fail` or `replace`) and an instrument policy
(`preserve_target` or `require_existing`). Chord annotations and internal group
links do not follow the baked notes. Detection, extraction, and send use the
same registry schemas and handlers over WebSocket and MCP.

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

`clips.createMidi` requires a discriminated `placement`; `clips.move` and
`clips.duplicate` use the same shape as `destination`. Arrangement placement
contains `view: "arrangement"`, `trackId`, and `startBeat`. Session placement
contains `view: "session"`, `trackId`, durable `sceneId`, and an explicit
`occupiedPolicy` of `fail`, `swap`, or `replace`. The old top-level `view` and
unplaced session clips are rejected. The destination view must match an
existing source clip's current view; moving between arrangement and session is
a separate conversion, not an implicit side effect of placement.

`clips.resize` takes `lengthBeats` and an `edge` of `start` or `end`. All three
operations are edit-scoped and commit as one undo action. A move that restates
the current destination and a resize that restates the current length are
successful no-ops and do not advance the revision. For an occupied session
destination, `fail` leaves both slots unchanged, `replace` deletes the occupant,
and `swap` exchanges the two placed clips. `swap` is only meaningful for move;
create and duplicate reject it. Clearing a slot is therefore the same model
operation as deleting its clip or relocating it. Undo/redo restores the full
source/destination plan and preserves allocated clip IDs.

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

### Session snapshot and scene indexing

Session scenes are durable project records. Each carries a stable integer `id`,
name, colour, and an ordered `sceneIndex`. `sceneIndex` is zero-based everywhere
the model, engine, WebSocket API, and MCP API address a slot. `displayIndex` in
the scene projection is the corresponding one-based label for user-facing
clients; it is never accepted as an address.

`session.get` is a complete deterministic snapshot with three arrays:

- `scenes`, in project order, including empty scenes;
- `tracks`, in project track order, with the active Session clip (or null) and
  the track's `arrangement`/`session` playback mode;
- `slots`, ordered by scene and then track, including empty slots. Every slot
  carries its stable `sceneId`, zero-based `sceneIndex`, nullable `clipId`,
  launch state, and record-arm/recording state. Occupied slots also expose
  `launchSettings`: `launchMode`, `launchQuantize`, `followAction`,
  `followActionDelayBeats`, and `followActionLoopCount`; empty slots report it
  as null.

Native projects preserve the scene records and their next-ID watermark.
DAWproject import/export maps them to ordered `<Scene>` elements and retains
name and colour. A legacy Session clip with no valid row is assigned to the
first empty slot on its track during load; a duplicate legacy slot is resolved
the same way, so the loaded grid is never ambiguous.

Scene structure is edited only by the `edit`-scoped lifecycle operations:

- `session.createScene` inserts a fresh stable ID at an optional zero-based
  `index` and accepts initial name and colour metadata;
- `session.updateScene` changes name and/or colour as one metadata edit;
- `session.moveScene` reorders one stable `sceneId` to `toIndex`;
- `session.duplicateScene` inserts a metadata copy after its source and requires
  `copyClips` to state whether occupied source slots are copied;
- `session.deleteScene` requires `populatedPolicy`: `fail`, `deleteClips`, or
  `moveClips`. Moving also requires a distinct `destinationSceneId` whose
  corresponding track slots are all empty.

Every operation returns the complete updated `session.get` projection. The
complete slot plan is checked before mutation, scene order and all affected
clip indices publish as one structural change, and each successful request is
one undo step and one revision. Stable scene IDs survive moves and undo/redo;
the final scene cannot be deleted. Metadata restatements and moves to the
current index are revision-neutral no-ops.

`session.updateClipSettings` applies any non-empty subset of an occupied
Session clip's launch settings as one validated edit, one undo step, and one
revision. Launch mode is `trigger` or `toggle`; quantize accepts `none`,
`8_bars`, `4_bars`, `2_bars`, `1_bar`, `1/2`, `1/4`, `1/8`, or `1/16`; and
follow action accepts `none`, `next`, `previous`, `random`, `stop`, or `again`.
Unknown enum values, negative delays, and loop counts below one are rejected
before the model is touched. A restatement is a revision-neutral no-op.

`session.returnToArrangement` hands either one `trackId`, or every track when
`trackId` is absent, from Session playback back to the arrangement. It is live
engine control under the `session` scope: it creates no undo history and does
not advance the project revision. Playback-mode handoffs and launch-setting
edits both publish a fresh `session` snapshot; settings edits also publish the
updated clip on the `clips` topic.

## Subscriptions

Eleven topics partition what a client can watch: `project`, `tracks`, `clips`,
`devices`, `selection`, `transport`, `session`, `automation`, owner-specific
`jobs`, and the two continuous ones, `meters` and `playhead`.

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
  `transport`, `selection`, `devices`, and `jobs` the payload is the topic's full
  state, because there is nothing useful to diff (and a job baseline belongs to
  one connection rather than the shared project).
- A change to the `session` scene or track envelope (scene metadata/order,
  active clip, or playback mode) is sent as a fresh `session.get` snapshot.
  Slot-only occupancy, launch, and recording changes retain the keyed delta
  form.
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
- physical audio/MIDI device identifiers (routing discovery exposes opaque
  hashes plus logical `track:N`, `master`, `default`, `all`, and contextual
  `none:*` tokens instead);
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
