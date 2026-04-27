# Controller Profile Format

A controller profile is a small JSON file that tells MAGDA how a piece of hardware is laid out and what each control should do by default. Profiles live in MAGDA's controllers directory (`~/Library/MAGDA/controllers/` on macOS — see [Controllers → Profiles directory](../interface/controllers.md#profiles-directory)) and load on app launch.

This page is a reference for anyone writing or editing a profile by hand — community contributors, advanced users, plugin authors. Most users never need to look at the JSON.

## File shape

```jsonc
{
  "id": "novation.launchkey_mini_mk4.macros",
  "vendor": "Novation",
  "name": "Launchkey Mini MK4 — Macros",

  "controls": [
    { "controlId": "knob_1", "kind": "knob", "cc": 21, "channel": 1 },
    { "controlId": "knob_2", "kind": "knob", "cc": 22, "channel": 1 }
    // …
  ],

  "defaultBindings": [
    { "controlId": "knob_1", "resolverKind": "focused.macro", "args": { "macroIndex": "0" } },
    { "controlId": "knob_2", "resolverKind": "focused.macro", "args": { "macroIndex": "1" } }
    // …
  ]
}
```

## Top-level fields

| Field | Required | Description |
|---|---|---|
| `id` | ✓ | Stable, dot-separated identifier. Convention: `<vendor>.<model>` (lowercase, underscores). The id is what MAGDA uses internally — multiple files with the same id collide last-wins. |
| `vendor` |   | Display name of the manufacturer (shown in the dialog). |
| `name` | ✓ | Display name of the model. |
| `controls` | ✓ | Array of physical controls the hardware exposes. Must contain at least one. |
| `defaultBindings` |   | Array of "what each control does out of the box". Optional — a profile that only declares controls is still valid; bindings just won't fire automatically. |

## Controls

Each entry describes a physical control. MAGDA listens for the matching MIDI message and routes it to whatever the control is bound to.

| Field | Required | Description |
|---|---|---|
| `controlId` | ✓ | Stable name used to reference this control from `defaultBindings`. Free-form; convention is `knob_1`, `pad_3`, `slider_a`, etc. |
| `kind` | ✓ | What the control physically is. Free-form string but conventional values are `knob`, `slider`, `pad`, `button`. Used for UI hints. |
| `cc` | ✓ | MIDI CC number, `0–127`. |
| `channel` | ✓ | MIDI channel, `1–16`, or `-1` for "any channel". |
| `feedbackCc` |   | If the controller accepts colour / state feedback on a different CC than its input, set it here. Most don't. |

## Default bindings

Each entry says: *when this control fires, route the value to this resolver*.

| Field | Required | Description |
|---|---|---|
| `controlId` | ✓ | Must match a `controlId` from `controls`. |
| `resolverKind` | ✓ | Which resolver runs when this control fires. See the table below. |
| `args` |   | Per-resolver arguments. JSON object of strings (numeric values are still strings). |

A binding referencing a non-existent `controlId` is dropped at load with a warning — the upload flow in the Controllers dialog also catches it during validation.

## Resolvers

A resolver is a tiny function that runs every time a bound control fires. It returns a concrete target (a parameter, a macro, a track property) based on **the current state of MAGDA** — focused device, selected track, etc. — so the same hardware control can drive different things depending on context.

This is the difference between a profile and a [MIDI Learn binding](../interface/controllers.md#midi-learn): Learn pins a control to one specific parameter, the profile/resolver path makes it follow context.

MAGDA ships with five built-in resolvers:

### `focused.macro`

Drives one macro of whichever device currently has focus.

| Arg | Type | Description |
|---|---|---|
| `macroIndex` | string of int (`"0"`–`"15"`) | Which macro to drive. Devices have 16 macros total (8 per page × 2 pages). |

Example:

```json
{ "controlId": "knob_1", "resolverKind": "focused.macro", "args": { "macroIndex": "0" } }
```

The eight knobs of a typical 8-knob controller usually map to `macroIndex` `"0"` through `"7"` — they always land on the focused device's first eight macros, regardless of which track or device that is.

### `selected.volume`

Drives the volume fader of the currently-selected track. No args.

```json
{ "controlId": "fader_1", "resolverKind": "selected.volume", "args": {} }
```

Useful for a generic-purpose volume fader on a controller — point it at the selected track and re-select tracks to "move" the fader.

### `selected.pan`

Drives the pan of the currently-selected track. No args.

```json
{ "controlId": "encoder_pan", "resolverKind": "selected.pan", "args": {} }
```

### `master.volume`

Drives the master bus volume. No args.

```json
{ "controlId": "fader_master", "resolverKind": "master.volume", "args": {} }
```

### `master.pan`

Drives the master bus pan. No args.

## Multiple profiles per controller

A single piece of hardware doesn't have to be one profile. A Launchkey Mini might be useful in two different ways: knobs driving the focused device's macros, or knobs driving the selected track's volume / pan / sends. Each of those is a different **intent**, and each is its own profile JSON.

The architecture supports this — what matters is that each profile has a distinct `id`. The convention is `<vendor>.<model>.<intent>`, dot-separated at every segment:

```
novation.launchkey_mini_mk4.macros     ← knobs follow focused device
novation.launchkey_mini_mk4.mix        ← knobs control selected-track mix
```

The `name` field uses the model name with the intent appended for clarity:

```jsonc
{
  "id": "novation.launchkey_mini_mk4.mix",
  "vendor": "Novation",
  "name": "Launchkey Mini MK4 — Mix"
}
```

Each variant shows up as its own row in the Controllers dialog. A user picks the variant they want for the way they're working today; switching variants is one dialog visit, not a rebuild of the JSON.

## A complete example

A full profile for a hypothetical 8-knob, 1-fader, 1-pan-encoder controller:

```jsonc
{
  "id": "acme.studio_8",
  "vendor": "Acme",
  "name": "Studio 8",

  "controls": [
    { "controlId": "knob_1", "kind": "knob", "cc": 21, "channel": 1 },
    { "controlId": "knob_2", "kind": "knob", "cc": 22, "channel": 1 },
    { "controlId": "knob_3", "kind": "knob", "cc": 23, "channel": 1 },
    { "controlId": "knob_4", "kind": "knob", "cc": 24, "channel": 1 },
    { "controlId": "knob_5", "kind": "knob", "cc": 25, "channel": 1 },
    { "controlId": "knob_6", "kind": "knob", "cc": 26, "channel": 1 },
    { "controlId": "knob_7", "kind": "knob", "cc": 27, "channel": 1 },
    { "controlId": "knob_8", "kind": "knob", "cc": 28, "channel": 1 },
    { "controlId": "fader_master", "kind": "slider", "cc": 7,  "channel": 1 },
    { "controlId": "pan_encoder",  "kind": "knob",   "cc": 10, "channel": 1 }
  ],

  "defaultBindings": [
    { "controlId": "knob_1", "resolverKind": "focused.macro", "args": { "macroIndex": "0" } },
    { "controlId": "knob_2", "resolverKind": "focused.macro", "args": { "macroIndex": "1" } },
    { "controlId": "knob_3", "resolverKind": "focused.macro", "args": { "macroIndex": "2" } },
    { "controlId": "knob_4", "resolverKind": "focused.macro", "args": { "macroIndex": "3" } },
    { "controlId": "knob_5", "resolverKind": "focused.macro", "args": { "macroIndex": "4" } },
    { "controlId": "knob_6", "resolverKind": "focused.macro", "args": { "macroIndex": "5" } },
    { "controlId": "knob_7", "resolverKind": "focused.macro", "args": { "macroIndex": "6" } },
    { "controlId": "knob_8", "resolverKind": "focused.macro", "args": { "macroIndex": "7" } },
    { "controlId": "fader_master", "resolverKind": "master.volume", "args": {} },
    { "controlId": "pan_encoder",  "resolverKind": "selected.pan",  "args": {} }
  ]
}
```

The eight knobs follow whichever device has focus; the master fader always drives the master bus; the pan encoder always drives whichever track is selected.

## Validation

When MAGDA loads a profile (on launch, or after import via the dialog) it runs structural and semantic checks:

- **Top-level**: `id` and `name` must be non-empty; `controls` must contain at least one valid entry.
- **Per control**: required fields present; `cc` in `0–127`; `channel` is `-1` or `1–16`. Out-of-range entries are dropped with a warning.
- **Per binding**: required fields present; the referenced `controlId` must exist in `controls`. The resolver kind must be registered.
- **Cross-field**: duplicate `controlId`s in the same profile are flagged; bindings referencing unknown `controlId`s are flagged.

Profiles that fail any of these checks load partially or are rejected entirely, depending on the failure. Look at the app logs (`~/Library/MAGDA/Logs/` on macOS) for diagnostic messages.

## Authoring tips

- **Generate, don't write from scratch.** The AI panel's `/controller <description>` command produces a JSON skeleton that's almost always closer to correct than starting from a blank file.
- **Name `controlId`s consistently.** `knob_1`, `knob_2`, … makes the binding list scan easily; `cc21`, `cc22`, … doesn't.
- **Use `channel: -1` if you don't know.** "Any channel" is friendlier than locking to 1 — most users never change their controller's channel and a literal `1` is fine in practice, but `-1` saves anyone who has.
- **Test against the hardware before submitting.** A profile that compiles cleanly can still be subtly wrong (CC numbers off by one, channel mismatch). Walk every knob in the Controllers dialog and confirm each one moves the expected macro.

## Sharing

Tested profiles can be shared via the [community page](https://magda.land/community/controllers). Submissions ship in the next MAGDA release for everyone using that hardware.

## Extending the resolver list (developer note)

Resolvers are registered via `magda::ResolverRegistry::registerResolver(...)` at startup. Adding a new resolver kind requires a C++ subclass of `AliasResolver` — see `magda/daw/core/aliases/ResolverRegistry.hpp` in the source. Profiles can't define their own resolvers; they consume the registered set.
