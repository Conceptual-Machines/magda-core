# Hosted plugin parameter control

Status: proposed implementation specification

Date: 2026-09-13

Scope: the native engine's hosted plugins, with a shared application-facing API and a Tracktion adapter. This replaces the parameter-ownership assumptions behind #2629, rather than replacing the useful sparse-table and parameter-catalog work.

This document is a design, not a claim that the described APIs exist. Proposed names may change; the ownership, ordering, failure and test contracts must not.

## 1. Decision

The live plugin owns its ordinary current state. Its chunk is a durable snapshot of that state, not a live command interface. MAGDA owns automation, modulation, controller mappings and the base values those controls require.

Three operations must have separate paths:

1. **Observe**: obtain metadata and subscribe to a parameter's current value.
2. **Edit**: deliver a one-off value or an explicit edit gesture to the plugin.
3. **Drive**: continuously calculate a value through MAGDA's automation/modulation graph.

Observing or editing a parameter must not require a persistent `DeviceInfo::parameters` entry, a parameter-table entry, a render-plan rebuild, or a serialized history of touched slots.

### Ownership

| Data | Authority | Persistence |
| --- | --- | --- |
| Ordinary plugin patch | Live plugin | Captured chunk |
| Parameter names, IDs, ranges and capabilities | Instance catalog plus display configuration | Optional descriptive cache; configuration persists |
| Automation lanes, modulation links, bindings | MAGDA document | Yes |
| Base value needed by a host control | MAGDA control record | Yes |
| Current effective/observed value | Runtime observation cache | No |
| Accepted but undelivered edit | Runtime command state | No; save must settle it |
| UI subscriptions and active gestures | Runtime service | No |
| Wrapper wet/dry and other MAGDA-owned controls | MAGDA document | Yes; never entrusted to a plugin chunk |

Internal MAGDA devices retain their existing model-owned parameter semantics. Do not route them through chunk-owned semantics merely to make an API uniform.

## 2. Why this change is necessary

The current code has separated enumeration from storage, but not writes from storage:

- `DeviceParameterList.cpp` can describe parameters absent from the model.
- `TrackManager::setDeviceParameterValue` historically writes only an existing model entry.
- `EngineHost::syncMirroredParameters` trims the model according to addressed slots.
- `EngineExternalDevice::writeParameters` delivers values through the table.
- Plugin notifications flow back through `setDeviceParameterValueFromPlugin`, which uses a parameter-change notification also consumed by the host publisher.

Consequently a visible knob can have no write destination. The uncommitted `touchedParameters` patch fixes that symptom by turning an edit into permanent mirroring. It also fabricates parameter records when metadata is missing. This is not the intended final design.

The recent `hostWrote` two-block mask is also not an ownership contract. A callback's arrival time cannot prove whether it is an echo, quantized readback, internal modulation or a genuine user edit. Resetting suppression flags from a publishing thread creates additional transition windows.

The fix is an explicit command path and a non-command observation path.

## 3. Invariants and non-goals

### Required invariants

- A valid ordinary parameter can be edited without adding it to the document or compiled table.
- Merely opening a UI, listing parameters over MCP or enabling value observation changes no document state.
- Plugin observations never automatically become outbound parameter commands.
- Plugin observations never overwrite a host-owned modulation base or authored lane.
- No parameter operation can reach a replacement plugin through a reused device ID.
- Accepted operations have a documented terminal outcome; no silent queue overflow.
- Render callbacks do not allocate, block, capture chunks, access the filesystem, or post message-loop work.
- Bypass does not disable editing, observation, undo or state capture.
- Successful save includes all applicable edits accepted before its save barrier.
- Configured display units never change the normalized value delivered to a hosted parameter.
- Permission checking, metadata validation and actual delivery resolve the same parameter identity.

### Non-goals

- Replacing the automation evaluator or inventing new automation modes.
- Sample-accurate UI dragging in the first implementation. Existing automation accuracy must be preserved.
- Requiring every plugin format to expose identical callback provenance.
- Eliminating all runtime per-parameter memory. Compact runtime caches are acceptable; full-document duplication and unnecessary per-block work are the costs being removed.
- Implementing out-of-process hosting now. The endpoint must nevertheless avoid exposing instance pointers to consumers.

## 4. Parameter identity and metadata

Use two representations:

**Document address**: device path/section plus stable plugin parameter ID where available. Legacy slot is a fallback, not proof of identity after a catalog changes.

**Runtime handle**: device key, instance generation, catalog generation and resolved slot. The handle contains no borrowed pointer. Resolve string IDs off the audio thread.

A parameter catalog is an immutable snapshot containing:

- Stable ID, resolved slot, name and unit.
- Normalized default, discrete/boolean information and allowed edit capabilities.
- Display configuration and text-provider access through the control endpoint.
- Explicit value convention. Hosted commands are always finite normalized positions in `[0, 1]`; reject out-of-range input instead of silently clamping application commands.

Metadata and values are separate structures. Do not use a `ParameterInfo` with guessed ranges as a substitute for a missing catalog entry.

Catalog snapshots are cached per instance/catalog generation. Opening a control or dragging a knob must not repeatedly enumerate the whole instance or read a configuration file.

On catalog change, invalidate handles, rebuild mappings, and re-resolve stable IDs. If a parameter cannot be uniquely resolved, report an unavailable target; never silently redirect it to the parameter now occupying an old slot. Plugins without stable IDs may use slots only within a compatible catalog, with explicit diagnostics when compatibility cannot be established.

## 5. Application-facing service

Introduce a `HostedParameterService` behind `AudioEngine` or the existing device-control boundary. `TrackManager` remains the document owner; it is not the transport for ordinary hosted edits.

Conceptual API:

```cpp
CatalogSnapshot describe(DevicePath);
ParameterRead read(ParameterHandle);
Subscription observe(DeviceHandle, ParameterSet, ObservationSink);

EditReceipt beginEdit(ParameterHandle, EditOrigin, EditPolicy);
EditReceipt setValue(GestureToken, NormalizedValue);
EditReceipt endEdit(GestureToken);
EditReceipt setOnce(ParameterHandle, NormalizedValue, EditOrigin, EditPolicy);

// Async completion / query by receipt, not a plugin echo interpreted as an ACK.
CommandOutcome outcome(CommandId);
```

The public control side runs on the existing control executor. Remote/API callers marshal there. Realtime controller input uses a prebound, fixed-size ingress and does not perform catalog lookup, allocate a gesture object or call the message-thread API.

### Results

An immediate receipt distinguishes `Accepted` from rejection. Rejections include invalid value, unknown parameter, unavailable/loading instance, stale handle, permission denial, active-driver conflict, queue full and closing session.

Accepted commands eventually become one of:

- `Delivered`: the adapter accepted the setter operation at its execution boundary.
- `Superseded`: a newer value in the same coalescible gesture replaced this update.
- `Cancelled`: the instance/session/gesture was invalidated.
- `Failed`: adapter execution failed, with a reason.

`Delivered` does not claim that the plugin's DSP has consumed a queued format-level change or that its observed value equals the requested value. Capture completion has a stronger, separate contract.

If the user drags to `0.7`, a receipt reports the accepted requested value. It must not return an unrelated old instance readback as the result of that write. Expose requested, pending and observed fields distinctly. Preserve existing API response compatibility through an explicitly documented adapter, or version the wire response; do not silently change field meanings.

Programmatic opt-in remains independent of table membership. An allowed MCP/controller edit must not need a pre-existing mirrored value. UI-origin edits must not bypass validation or accidentally acquire programmatic privileges.

## 6. Delivery and realtime execution

### Command transport

The control executor resolves metadata and produces bounded POD commands containing runtime handle, command sequence, gesture token, operation and normalized value. The audio side reads only prepared numeric handles and flags.

Use a tested bounded queue or equivalent bounded mailbox design. Specify capacity, maximum commands consumed per callback and reserved capacity for gesture termination/cancellation/barriers. The implementation must not use an unbounded `while(pop())` loop that concurrent producers can keep alive indefinitely.

Only value updates within the same gesture, handle and ordering epoch may coalesce. Never coalesce across begin/end, preset restore, undo, control-ownership transitions or capture barriers. Superseded receipts must be completed explicitly; a highest-sequence number alone is not proof that every earlier command was delivered.

An accepted end-of-gesture must not be lost when the queue is saturated. Reserve space or implement an equivalent proven termination protocol. Rejection must not leave a pending UI value permanently displayed.

### Execution boundary

The engine drains parameter commands before evaluating/delivering the block's effective parameter controls. It uses a pinned endpoint registry, with generation validation and lifetime leases managed off the audio thread.

This pump is independent of whether a device has a render op. A bypassed or otherwise omitted plugin still receives edits through its retained endpoint. No fake render-plan entry is required to turn a bypassed knob.

When audio callbacks stop, a control-side pump may execute only after obtaining explicit exclusive access/quiescence from the audio owner. Stopped transport alone does not mean callbacks stopped. Never race a control-side setter against the audio-side command pump.

Plugin formats must implement their documented thread requirements behind the adapter. If a setter is not safe at the realtime boundary, use the serialized control operation path and the existing processing-exclusion mechanism. A UI must not directly call `AudioPluginInstance` as an alternative route.

### Runtime ordering

At each execution boundary:

1. Acquire compatible endpoint/control snapshots.
2. Consume the bounded command batch in order, checking generations.
3. Apply accepted authority transitions and ordinary edits.
4. Resolve host-driven values under the resulting authority state.
5. Deliver changed effective values and process active plugins.
6. Publish command completions and observations through preallocated storage.

Ownership transitions and commands carry revisions. A newly published table must not immediately overwrite a gesture accepted against the previous table. Ignore/defer incompatible control revisions until the boundary can apply the transition coherently; test this explicitly.

## 7. Host control and gestures

Replace the single broad addressed classification with at least:

- `ObservedParameters`: transient subscriptions, independent of the document.
- `ControlledParameters`: parameters for which the host graph requires a base or an effective value.
- `PendingEdits`: transient commands and gestures.

Automation lanes, macros and modifiers create control records where needed. A UI selection does not. An AI permission list does not. MIDI learn observes candidates; a completed binding either issues commands or becomes an explicitly continuous control mapping.

Persist a normalized `baseValue` only when host control needs one. Preserve it across temporary driver inactivity. On acquiring control for the first time, seed it from the current observed plugin state after preceding accepted edits have settled. Never guess it from a display range or overwrite it with modulated output.

### Authority rules

Reuse `AutomationAuthorityState` and its existing Reading/Disabled/Touching/Writing transitions. Do not serialize active gestures.

| Situation | Required behavior |
| --- | --- |
| No host control | Ordinary edit goes directly to the plugin |
| Macro/modifier controls parameter | A base-edit command changes its host base; effective output remains graph-derived |
| Automation Reading, no gesture | Lane owns the effective value |
| UI BeginTouch | Suppress lane playback according to the existing state machine; edit the manual/base value |
| UI BeginWrite | Record gesture values through the existing automation recording path |
| EndGesture | Return to the authority prescribed by the existing state machine |
| Automation Disabled | Preserve authored lane, allow manual/base edits |
| Noninteractive one-shot during Reading | Reject by default as `Controlled`; an explicit policy may disable playback or record an edit |
| Bypass or removed render op | No active effective driver; ordinary edits still work; document controls survive |

Macro/modifier mappings and manual-base edits must retain their current mathematical semantics. A direct effective override, if offered, is a distinct explicit policy and cannot silently replace a base edit.

A gesture owns one parameter temporarily. Competing gestures are rejected or explicitly preempted; they are not silently merged. Every preemption completes the displaced gesture and clears its UI pending state.

The UI labels base versus effective values where both exist. When control resumes after bypass, the host-owned base/automation wins by design; editing an effective value while bypassed must not secretly rewrite an automation lane.

## 8. Plugin feedback and observation

Replace per-device `MessageManager::callAsync` from parameter callbacks with a host-owned drain (#2632). A callback does bounded work into preallocated observation storage and marks a wake/dirty indicator. The control executor drains at a bounded UI cadence.

Observation events must be tagged with instance/catalog generation and, when known, source classification:

- Native-editor gesture begin/update/end.
- Unclassified plugin parameter readback.
- State/program/catalog change.
- Host command completion (generated by MAGDA, not inferred from readback).

For arbitrary-thread plugin callbacks, use a proven multi-producer publication mechanism; three unrelated atomics do not form a coherent event. Coalescing value observations is allowed. Gesture boundaries and invalidations require reliable delivery or an explicit reset/resync indication.

Always maintain enough invalidation/state-change information to capture unobserved edits correctly. Filtering UI value traffic must not make a plugin's changed patch invisible to save or dirty-state tracking. Unknown change provenance may conservatively mark plugin state dirty, but must not create automation or another outbound command.

### Echo handling

- Remove the two-block echo deadline as a correctness mechanism.
- Host commands update pending/delivery state. Plugin notifications update observation state.
- An unclassified notification is never automatically treated as a user edit, automation write or host-base update.
- While a MAGDA gesture is active, its UI displays the latest accepted requested value; observed/quantized readback is retained separately.
- After the latest command is delivered and the gesture ends, release the pending overlay and display the adapter's refreshed observation. Do not synthesize a second setter to force the display to agree.
- Known native-editor gestures may participate in Touch/Write and undo. Use JUCE/format gesture hooks where available. Where provenance is unavailable, preserve ordinary plugin editing through the chunk/observation path; do not claim accurate automation recording from every generic callback.

Command IDs order MAGDA commands only. They are not IDs attached to arbitrary plugin echoes. Format-specific observation refresh and capture fences must be tested without assuming a callback is a correlated acknowledgement.

Effective driver state is published by the execution owner, including explicit inactive transitions. Message-thread enumeration of observed/controlled slots must not clear flags owned by an in-flight audio block.

## 9. UI, API and controller integration

Route all hosted writes through the service:

- `DeviceSlotParameterPaging`, custom UI forwarding and mini-chain controls.
- `DeviceApiLive::setDeviceParameter`, remote handlers, agents and scripting.
- MIDI learn/controller actions, preserving their existing permission and normalization policy.
- Undo/redo commands.

`DeviceParameterList` becomes a projection over cached metadata plus runtime read state. It no longer gives every existing document parameter unconditional authority over the displayed plugin value. It may expose a pending request or a host base explicitly, according to read policy.

`describe` and subscription acquisition do not mutate `DeviceInfo`. Subscription tokens are RAII/lifetime-scoped, and cancellation prevents delivery to destroyed controls. Closed UIs stop receiving frequent value updates; catalog and capture remain available.

Separate notifications in code:

```text
user/API intent -> command service -> adapter
plugin observation -> runtime cache -> observers
authored control edit -> document -> control-table publication
```

Remove hosted feedback's call through `notifyDeviceParameterChanged` when that signal also commands the engine. Different directions require different event types, not a comment or temporary suppression flag.

The Tracktion adapter must implement the same application contract while retaining its format wrapper's required execution rules. Do not run both the old `AudioBridge` setter and the new service for one command. Internal-device forwarding is a separate adapter to the existing model path.

## 10. Save, restore, presets and missing plugins

### Persistence shape

Keep chunk, plugin identity, wrapper state, UI selections, configuration and authored automation/modulation/bindings. Add or formalize a sparse host-control record keyed by stable parameter identity containing only required host base values and control configuration.

Do not serialize observation caches, pending overlays, gesture tokens, runtime generations, per-block driver flags or `touchedParameters`.

A cached catalog may be stored for missing-plugin UI, but must be explicitly non-authoritative. No ordinary cache value is replayed over a successfully restored chunk.

### Save barrier

A save captures a document revision and command watermark. It must:

1. Order a per-instance capture barrier after all edits accepted into that save revision.
2. Settle every earlier command as delivered, superseded, cancelled or failed; surface failures rather than pretending the intended state was saved.
3. Ensure delivered parameter changes have reached the state represented by the plugin's capture API.
4. Exclude concurrent processing/state mutation through the existing control-plane capture lease and quiescence protocol.
5. Capture on the appropriate non-realtime executor.
6. Verify instance generation and install the snapshot into the matching save revision.
7. Release deferred post-barrier edits in order and commit the file atomically.

Do not block the message thread waiting for an acknowledgement that must execute on that same thread. Extend the save operation into an asynchronous coordinator if needed. The current synchronous `control_->drain()` alone is not proof that queued realtime parameter edits have reached plugin state.

Some adapters queue setter changes until processing. Each supported format must provide a tested parameter-to-capture fence. If a legal processing boundary is necessary, coordinate it without audible output or accidental transport advancement; do not assume invoking a setter or sleeping two blocks is sufficient. An adapter lacking a safe fence must fail/defer capture explicitly, not report success with stale state.

Host automation values and its base remain separate. Capture may contain the effective automated value at the barrier; the saved host-control record retains the base and the document determines the effective value on restoration.

### Restore and preset replacement

- Serialize apply-state against commands and capture through the control plane.
- Invalidate the old state/command epoch. No pre-restore pending edit may replay into the restored patch.
- Apply the chunk, refresh catalog and observations, then rebind host controls by identity.
- Retain or replace host-control configuration according to the existing operation's scope: plugin-native preset changes preserve document automation; a MAGDA device preset replaces the fields defined by that preset operation.
- Reset last-delivered-value caches so a newly restored instance cannot skip a required host-controlled write.
- Unavailable IDs are unresolved targets with diagnostics, not guessed numeric mappings.

### Missing/loading instances

Reject live parameter edits as `Unavailable`/`Loading` until a parameter can be validated and delivered. Do not fabricate metadata or silently persist a value that cannot be applied. Keep the previous chunk and control records intact, and allow editing document automation against cached metadata where its identity is known.

If a future product requirement permits editing a missing plugin's patch, implement an explicit persisted deferred-command feature with validation and restore ordering. It is not an implicit property of this design.

## 11. Undo, dirty state and recovery

A MAGDA gesture is one undo operation. Capture its observed/base value at the ordered gesture boundary and its final accepted value at completion. Do not use an arbitrarily stale UI cache as the undo baseline. Pending commands are included in ordering when determining that baseline.

Undo/redo uses the same command/control-edit APIs and permission-independent internal undo authority. It validates target identity and generation, and reports an unavailable target rather than writing to a replacement slot. A one-parameter command undo does not promise to restore every secondary change a complex plugin made; full patch/preset undo uses coordinated chunk snapshots.

Native-editor gestures can enter the journal when reliable gesture provenance exists. Preserve the plugin's own undo behavior otherwise; do not invent gesture groups from a callback timeout.

Dirty-state tracking is independent of parameter mirroring. Mark accepted patch edits and meaningful plugin state notifications dirty; do not serialize a new chunk per knob movement. Autosave uses the same save barrier. An undrained runtime queue is not crash durability.

## 12. Migration and implementation sequence

### Phase A — Explicit contracts and a correctness baseline

- Add identity, catalog, receipt/outcome and value-read types.
- Split command and observation notifications.
- Add an end-to-end fake hosted plugin fixture with controllable processing and feedback delays.
- Keep existing broad runtime caches temporarily if useful. Do not expand serialized mirrors.
- Do not land the uncommitted touched-slot design as the final schema. Existing user edits in a running development session must be captured before switching behavior.

Exit: tests can distinguish accepted, delivered, observed and captured values.

### Phase B — One-off commands

- Implement the bounded command pump, generation validation and endpoint leases.
- Implement bypass/idle delivery and reliable gesture termination.
- Move hosted UI, API, undo and controller setters to the service.
- Remove the requirement to create a model record before an ordinary edit.

Exit: the first unconfigured knob works, with no table/plan rebuild and no persistent parameter growth.

### Phase C — Observation and gestures

- Implement host-owned draining and subscriptions.
- Integrate pending UI overlays, native gesture provenance and Touch/Write.
- Remove feedback-to-command publication and the block-count echo heuristic.

Exit: delayed echoes, quantization, native editor movement and competing gestures cannot fight a drag or mutate a host base.

### Phase D — Sparse host control

- Retain `DeviceParams` slot/value iteration and sparse table support.
- Replace UI/AI-list-driven mirroring with explicit control-record membership.
- Seed bases on ordered acquisition; publish coherent authority transitions.
- Table shape changes are driven by control-graph changes, never ordinary edits/subscriptions.
- Reuse `EngineSession::publishValues`' existing structural escalation when the parameter layout changes; do not add a second guessed topology mechanism.

Exit: render work scales with active host control and changed commands, not parameter count or historical touches.

### Phase E — Persistence and removal

- Implement save barriers and format-specific capture fences.
- Migrate old full arrays: a successfully restored chunk is patch authority; preserve only bases required by actual host controls. Use legacy parameter replay only through an explicit existing no-chunk/recovery policy.
- If experimental touched-slot files exist, remove ownership derived solely from touch history. Capture a live authoritative patch before migration where possible. A stale chunk plus touched overrides requires explicit reconciliation; never silently discard known newer edits.
- Remove obsolete mirrored-value writers, fabricated metadata fallbacks and `touchedParameterSlots` writing.
- Preserve original chunks when a plugin is missing or capture fails.

Exit: old/new projects, presets and autosaves round-trip without reintroducing a competing full parameter state.

Each phase may merge behind a feature flag, but only one hosted write path may be enabled for a device. Native-engine release remains gated on the full behavior matrix below.

## 13. Code responsibilities

| Existing area | Required change |
| --- | --- |
| `audio/DeviceParameterList.*` | Catalog/read projection and subscriptions; remove unconditional document-value overlay for ordinary plugin state |
| `core/TrackManagerDevices.cpp` | Keep internal/model-control mutations; delegate hosted ordinary edits; separate inbound observations |
| `core/AddressedParameters.*` | Replace broad membership with control dependency and observation queries; no touched-history input |
| `api/device_api_live.cpp`, remote handlers | Validate through catalog; permission by identity; return receipts and explicit read states |
| `engine/AudioEngine.hpp`, `MagdaAudioEngine.*` | Application-facing parameter endpoint and adapter dispatch |
| `engine/host/EngineHost.cpp` | Command pump, control snapshots, subscription drain, save coordinator |
| `audio/plugins/engine/DeviceControl.*` | Extend existing serialized control/lifetime boundary for catalog/state barriers and non-RT operations |
| `EngineExternalDevice.*` | Adapter-local delivery and feedback capture; no document writes; no timing-based echo classification |
| `engine/param/*`, `PlanExecutor.*` | Sparse host-driven values and coherent authority revisions; retain numeric slot mapping |
| `TrackSerializer`, preset/capture paths | Chunk plus authored host-control persistence; legacy reconciliation |
| Tracktion bridge/processors | Implement equivalent service contract without duplicate outbound setters |

Prefer a dedicated `HostedParameterService` and transport types over adding all storage and protocol logic to `EngineHost::Impl`. Keep the pure control classification and identity resolution testable without a running audio device.

## 14. Acceptance tests

Use deterministic fixtures; do not use sleeps or subjective listening as the only evidence. Fixtures must independently control callback cadence, setter/readback behavior, parameter quantization, native gestures, delayed notifications and chunk contents.

### Ordinary edits

- A device with zero host-control records: edit slot 2, a high slot and a sparse slot from the grid, mini row and permitted MCP endpoint. Assert actual plugin value changes, no document entry is added and no plan/table rebuild occurs.
- Same edits while transport is stopped, device bypassed, rack bypassed, track inactive and hardware callbacks suspended.
- Invalid values/IDs, no permission, loading instance and unavailable instance reject without mutation.
- Queue saturation returns honest outcomes; final gesture value and end/cancel are not lost.

### Feedback and authority

- Immediate, one-block, ten-block and out-of-order readback do not create outbound commands.
- Quantized/smoothed feedback is shown as observation without fighting an active drag.
- Native-editor changes reach subscribers and captured state with no host control record.
- Macro/modifier output never becomes its own base.
- Reading -> Touching/Writing -> Reading follows the existing state machine, including table publication arriving mid-gesture.
- Automated plugin -> bypass -> disable lane -> edit -> unbypass does not retain stale driven suppression.
- Ending learn removes its subscription; unrelated UI observation continues.

### Lifecycle and persistence

- Remove/re-add same device ID, replace plugin and change catalog while commands/observations are pending: no event crosses generations.
- One instance's capture lease cannot keep a stale command logically valid for a replacement instance.
- Edit then immediately save: reopened chunk contains the edit, including a setter that queues until processing.
- Edits accepted after the save barrier are excluded from that revision and remain pending for the next save.
- Capture timeout/failure leaves the previous save recoverable and reports failure.
- Preset apply invalidates earlier commands and refreshes the catalog/cache before new edits.
- Missing-plugin save preserves its original chunk and host-control records.
- Undo/redo works for ordinary gestures without persistent touched slots; chunk undo covers preset replacement.

### Threading and performance

- Allocation/lock instrumentation around MAGDA's realtime path; plugin-internal costs measured separately.
- Concurrent producer, observation and snapshot tests under ThreadSanitizer where supported.
- Measure 0, 10 and 100 controlled parameters on fixtures exposing 100, 1,000 and 10,000 parameters. With no commands/controls, MAGDA's per-block parameter-delivery work must not scan the catalog.
- UI opening may perform bounded non-RT setup. Subsequent drags must not re-enumerate all parameters, reread config files or capture chunks.
- Assert rebuild counters: ordinary edits and subscriptions zero; real control-layout changes allowed.
- Run the existing external-device, control-plane, parameter-table, automation, API and serialization suites on both adapter paths, plus installed-plugin smoke tests for supported formats.

## 15. Definition of done

The feature is complete only when MAGDA's own controls can edit any valid permitted hosted parameter without first creating document ownership; observations cannot feed commands back; host controls preserve their authored bases; and a successful save reliably captures accepted edits.

The implementation must include the format-specific delivery/capture capability matrix, queue bounds and overflow behavior, migration policy, diagnostic counters and the deterministic tests above. Sparse mirroring or reduced callback counts alone do not satisfy this specification.
