# Controllers

MAGDA can be driven by any class-compliant MIDI controller. Hardware knobs, sliders, pads, and pitch/mod wheels can all map to plugin parameters, macros, and LFO rates — with two complementary mapping systems:

- **Automap profiles** — a controller-specific profile maps each control to a focused-device macro automatically. Plug in a supported keyboard, focus a device, and the eight macro knobs already drive that device's macros. No setup.
- **MIDI Learn** — right-click any parameter, macro, or LFO Rate, choose **Learn MIDI**, move a controller, done. The mapping is yours and persists with the project.

Both can coexist. A Learn'd binding shadows automap on the same CC so your custom mapping wins.

## Controllers Dialog

Open from **Settings > Controllers**.

![Controllers Dialog](../assets/images/interface/controllers.png)

The dialog lists every controller MAGDA knows about. Each row shows the manufacturer and model, the live MIDI input port the controller is connected to, and a connection-status indicator (green dot when the named port is currently available).

Two buttons sit in the header:

- **+ Add profile** — register a new controller using one of the profiles already in your [controllers directory](#profiles-directory). Pops a menu of available profiles, then asks which MIDI input the controller is connected to.
- **Upload profile…** — pick a `.json` profile from disk and import it. Validates the JSON, copies it into the controllers directory, and refreshes the registry. After upload, the new profile is available to **+ Add profile**.

Right-click a row to **Show profile in Finder** (reveals the JSON file) or **Remove** (deletes the controller and its bindings).

Bindings created by MIDI Learn against a removed controller's port still work — they attach to the live MIDI port name, not to the controller registry entry.

### Profiles

A profile describes a controller's physical layout — which CC numbers correspond to the eight macro knobs, and so on. MAGDA ships with profiles for common controllers (Launchkey Mini MK3 / MK4, generic 8-knob, etc.) and you can add your own.

When a profile is assigned to a controller and a device is focused, the profile's macro knobs drive **that device's macros**. Focus a different device — the same hardware knobs follow.

If you only want MIDI Learn (no automap), don't add a controller — Learn'd bindings route via the live port name and don't need a registry entry.

#### Profiles directory

User profiles live alongside MAGDA's other configuration:

| Platform | Path |
|---|---|
| macOS | `~/Library/MAGDA/controllers/` |
| Windows | `%APPDATA%\MAGDA\controllers\` |
| Linux | `~/.config/MAGDA/controllers/` |

On first launch the bundled profiles are seeded into this folder. Edits and additions there are durable — drop a new `*.json` profile in and it's available next time you open the Controllers dialog.

### Profile JSON format

For the full JSON schema, the resolver list (the functions that route a CC to a target — `focused.macro`, `selected.volume`, `master.pan`, etc.), the `<vendor>.<model>.<intent>` id convention, and a worked example, see the [Controller Profile Format reference](../reference/controller-profile-format.md).

## MIDI Learn

To map a single hardware control to a single MAGDA target:

1. Right-click the parameter, macro knob, or LFO **Rate** slider.
2. Choose **Learn MIDI**. The control pulses orange.
3. Move a knob, slider, or press a button on your controller.
4. The binding is captured. The pulse stops; an orange dot appears.

Learn works on:

- **Plugin parameters** — any device parameter (pulses on the slider).
- **Macros** — track, rack, or device macros. Right-click the knob.
- **LFO / modifier Rate** — right-click the **Rate** slider in the modulator editor.

To cancel without capturing, choose **Cancel MIDI Learn** (same menu, same item).

## Clearing Mappings

Right-click → **Clear MIDI Mapping (N)** removes only your Learn'd bindings; automap profile coverage is left alone. After clearing a Learn'd macro, the macro falls back to its profile mapping (the dot returns to green).

## Indicator Legend

MAGDA shows controller coverage as small dots on macro knobs, parameter slots, and device headers.

| Dot | Meaning |
|-----|---------|
| Green | Profile-driven (automap) binding is active on this target. |
| Orange | A user MIDI Learn'd binding is active. |
| Grey | Profile binding exists but is shadowed by an explicit Learn override on the same CC. |

A device header can show two dots side-by-side (green for profile coverage, orange for any Learn'd binding inside the device).

![Macro panel with green automap dots](../assets/images/interface/controllers-automap.png)

*Above: a 4OSC Synth focused with an automap-mapped controller. Each macro knob carries a green dot — the automap profile is driving them, and the device-header dot mirrors the same green.*

![4OSC filter showing an orange MIDI-learn dot on FREQ](../assets/images/interface/controllers-midilearn.png)

*Above: the FREQ slider has an orange dot — a user MIDI Learn binding is in effect. The device-header dot turns orange to mirror it.*

## Generating a Profile from Description

If MAGDA doesn't ship a profile for your controller, the AI Assistant can write one for you. In the AI panel, type `/co` to autocomplete `/controller`, then describe the controller after the command:

![/controller autocomplete in the AI panel](../assets/images/interface/controllers-slash.png)

```
/controller Akai MPK Mini Mk3, 8 knobs on CC 70-77, channel 1
```

The AI generates a profile JSON, drops it in your [controllers directory](#profiles-directory), and prompts you to enable it. Describe the controller in plain language — manufacturer, model, which CCs the knobs send, MIDI channel — the more specific you are, the better the profile.

To tweak the generated JSON by hand — fix a CC number, switch a knob to drive `selected.pan` instead of a focused-device macro — the [Controller Profile Format reference](../reference/controller-profile-format.md) explains every field.

See [AI Assistant — Slash Commands](../panels/ai-assistant.md#slash-commands).

## Tips

!!! tip
    A Learn'd binding routes through the **port name**, so it survives renaming the controller in the dialog or even removing it from the registry. The binding still fires whenever the named port is connected.
