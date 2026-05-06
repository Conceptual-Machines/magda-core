# Faust 64-slot pool refactor — plan

Working notes for the in-flight refactor on `feat/faust-poc`. Goal: give the
Faust device a fixed pool of 64 stable parameter slots so macro/mod links
and automation lanes survive a DSP recompile, plus typed control kinds
(continuous / discrete / boolean) driven by Faust UI metadata. Mirrors
the way generic devices already render via `ParamGridComponent`.

## Why

Today's `FaustPlugin` rebuilds its `AutomatableParameter` list on every
DSP swap. That's clean to read but it makes every binding-style state
fragile — a recompile renumbers params, links break, automation lanes
detach. Fixed pool fixes that and gives us:

- Stable indices for macro / mod / MIDI Learn / automation across
  `loadDspSource`.
- Typed widgets (text slider, dropdown, toggle) for free, since
  `ParamSlotComponent` already dispatches on `ParameterInfo::scale`.
- Faust UI metadata becomes the single source of truth for control
  shape — the model emits it, the parser reads it, the host renders it.

## Components

### Faust side

- **`FaustParamSlot`** *(value type)* — one slot in the pool. Fields:
  `int index; bool active; juce::String label; Kind kind { Continuous,
  Discrete, Boolean }; float min/max/step/defaultReal;
  std::vector<std::pair<float, juce::String>> choices; FAUSTFLOAT* zone`.
  Decouples Faust's runtime view from MAGDA's parameter view.
- **`FaustMetadataParser`** *(stateless free functions)* — extracts
  `[idx:N]`, `[style:menu{'A':0;'B':1}]`, `[style:radio{…}]`,
  `[unit:Hz]`, `[scale:log|exp]` from a Faust label. Returns
  `{cleanLabel, ControlMetadata}` — the UI never sees the bracketed
  annotations.
- **`FaustParamPool`** *(64-element owner)* — owns the lifetime-stable
  `AutomatableParameter`s (`param_01`…`param_64`) plus their
  `FaustParamSlot` siblings. Single load-bearing method:
  `rebindFromHarvest(harvested)` — fills slots by `[idx:N]` first, then
  encounter order into the next free slot, marking the rest inactive.
  Survives DSP swap because the pool's `AutomatableParameter`s never
  get torn down.
- **`HarvestedControl` + `UIHarvester`** *(internal to
  `FaustPlugin.cpp`)* — small Faust `UI` subclass that captures every
  `addHorizontalSlider` / `addVerticalSlider` / `addNumEntry` /
  `addCheckButton` / `addButton` along with the preceding `declare()`
  metadata. Tracks group-level vs control-level metadata via a
  push/pop stack on `openBox`/`closeBox` (control-level keys win on
  conflict). Pure data extraction — no MAGDA types.

### MAGDA bridge

- **`paramInfoFromSlot(FaustParamSlot) → ParameterInfo`** *(free
  function)* — picks `ParameterScale` from `Kind` + metadata
  (`Continuous` + `[scale:log]` → `Logarithmic`; `Continuous` plain →
  `Linear`; `Discrete` → `Discrete` with populated `choices`;
  `Boolean` → `Boolean`), populates `unit`, `scaleAnchor`. This is
  what `DeviceInfo.parameters` ends up filled from for Faust devices.

### Plugin / UI

- **`FaustPlugin`** *(refactored)* — ctor builds the 64-slot
  `FaustParamPool` once. `loadDspSource` does compile → harvest →
  `pool.rebindFromHarvest`. `applyToBuffer` walks the *active
  binding list on `FaustState`* (see real-time boundary below) and
  writes the AutomatableParameter's denormalized value into the zone.
- **`FaustUI`** *(refactored)* — keeps the bespoke header (logo / Load
  / Edit / framed name box) but the body becomes a thin wrapper over
  `ParamGridComponent`, like generic devices. The bespoke `ParamSlot`
  vector goes away.

## Real-time boundary

Audio thread MUST NOT walk the pool's slot table directly — that's
mutated on the message thread by `rebindFromHarvest`. The atomic swap
already used today is the right shape; we just put the binding list
*on the state* instead of dynamically computing it from the pool:

```cpp
struct FaustState {
    std::unique_ptr<dsp> dsp;
    interpreter_dsp_factory* factory;
    int dspIn = 0, dspOut = 0;

    struct ActiveBinding {
        int slotIndex;            // → pool.param(slotIndex)
        FAUSTFLOAT* zone;         // owned by `dsp`, valid for state's lifetime
        FaustParamSlot::Kind kind;
        float min, max, step;     // frozen at compile time
        bool logScale;
    };
    std::vector<ActiveBinding> activeBindings;
};
```

`loadDspSource` builds a fresh `FaustState` (including its bindings
vector), then `std::atomic_store(&active_, newState)`. The audio
thread does:

```cpp
auto state = std::atomic_load(&active_);
for (auto& b : state->activeBindings) {
    *b.zone = denormalize(pool.param(b.slotIndex).getCurrentValue(),
                          b.min, b.max, b.logScale);
}
state->dsp->compute(numSamples, in, out);
```

The pool's `AutomatableParameter::getCurrentValue` is wait-free in TE.
The state's binding vector is immutable for the state's lifetime.
Mutation surface (the slot table) is only ever read on the message
thread.

## Edge-case lockdowns

- **Duplicate `[idx:N]`** — take the first encountered, warn, route
  the loser to the next free slot via encounter order.
- **Out-of-range `[idx:N]`** (≥64 or <0) — same: warn, encounter-order
  fallback.
- **Value persistence across rebind** — keep the
  `AutomatableParameter`'s normalized value (so links / automation
  lanes survive). If the new range invalidates the stored value
  (range shrunk past it), clamp and log a one-line "renormalized"
  diagnostic.
- **Label cleaning** — parser returns `{cleanLabel, ControlMetadata}`.
  `cleanLabel` has no `[…]` annotations. UI labels and AliasGenerator
  both consume `cleanLabel`.
- **>64 active controls** — first 64 fill deterministically (idx-tagged
  first, then encounter order), extras dropped with a visible
  compile-time diagnostic surfaced in the FaustUI error label
  ("3 controls dropped: pool overflow"). Not silent.
- **Group-level vs next-control metadata** — group declares appear
  between `openBox`/`closeBox`; control declares appear between
  consecutive `addXxx`. Harvester maintains a metadata stack (push on
  open, pop on close); each control's effective metadata is the
  union of the stack frames + its own, with control-level keys
  winning on conflict. Tested explicitly — this is the subtle bit.

## Phase ordering

1. **Phase 1** — `FaustParamSlot.hpp`, `FaustMetadataParser.{hpp,cpp}`,
   plus unit tests. Pure logic, no audio path touched. Lands on its
   own.
2. **Phase 2** — `FaustParamPool.{hpp,cpp}`, `HarvestedControl`,
   `UIHarvester`. Still no plugin behaviour change; pool is built but
   not yet wired into `FaustPlugin`. Pool tests use a stubbed
   harvested control list.
3. **Phase 4** — `paramInfoFromSlot()`. Wire the path that fills
   `DeviceInfo.parameters` for Faust devices through it. Done before
   Phase 5 because the UI refactor proves nothing without correct
   `ParameterInfo`.
4. **Phase 3** — *single focused cutover commit.* `FaustPlugin` ctor
   builds the pool, `loadDspSource` produces immutable `FaustState`
   with `activeBindings`, `applyToBuffer` reads only state. Old
   `bindings_`, `ParamHarvestingUI`, `rebuildParameters` retired in
   the same commit. Nothing else changes in this commit — keeps the
   diff reviewable for the audio path.
5. **Phase 5** — `FaustUI` body becomes `ParamGridComponent`. Header
   stays.
6. **Phase 6** — system prompt update: allow `checkbox()` and
   `numEntry()`, instruct the model to emit `[idx:N]` so generated
   DSPs have stable slot indices across regenerations.

Phase 1 is independent. Phases 2–3 must land in this order. Phase 4
before 5. Phase 6 is independent and trivial.

## Risk areas

- **Audio thread correctness** — the cutover commit (Phase 3) is the
  only place the audio path changes shape. Read-only access to
  immutable state plus the existing atomic swap is the contract.
  Anything else racing the audio thread is a bug.
- **Faust metadata edge cases** — `[style:menu{…}]` payload contains
  semicolons, colons, single quotes; group-level scope nesting; mixed
  `:` and `;` separators in the wild. Cover with parser unit tests
  before integrating.
- **Slot identity stability** — `[idx:N]` is the user contract for
  "this parameter lives at slot N forever". Encounter-order fallback
  is convenient but means renaming a slider can shift the slot of
  every later control. Diagnostics must make this visible to the user
  (compile-time warning surfaced in the FaustUI error label).

## Status

- Initial proposal: 2026-05-04
- Approved scope: 64-slot pool, kinds {Continuous, Discrete, Boolean},
  drop bargraphs/soundfiles for now (existing system-prompt rule).
- Branch: `feat/faust-poc`. Lands on top of merge `2f821f6d` (AI panel
  + CoderAgent split).
