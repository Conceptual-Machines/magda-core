# Plugin Parameters

Third-party plugins rarely tell a host what their parameters actually mean — most expose every knob as a generic 0–100 % value. MAGDA takes a first pass at interpreting those values for you, and the **Parameter Configuration** dialog lets you refine or override the result for any plugin.

The configuration is saved per plugin (by plugin unique ID), so it's applied automatically every time the plugin is loaded.

## Automatic Inference

When a plugin is scanned, MAGDA looks at each parameter's name, value shape, and typical range and guesses a sensible unit and range. Common cases like cutoff frequencies (Hz), envelope times (ms), gain (dB), pitch (semitones), and mix amounts (%) are detected automatically. Parameters it can't classify fall back to a generic 0–100 % display.

You can override or extend the inferred configuration in the dialog described below.

## Configure Parameters Dialog

Open the dialog from the [Plugin Browser](panels/browsers.md#plugin-browser): right-click a plugin and choose **Configure Parameters…**.

The dialog shows one row per parameter with the following columns:

| Column | Description |
|--------|-------------|
| **Name** | Parameter name as reported by the plugin. |
| **Visible** | Toggle whether the parameter shows up in MAGDA's chain, inspector, and AI prompts. Hide parameters you never touch to reduce clutter. |
| **Unit** | Display unit — Hz, dB, ms, %, semitones, or any custom string. |
| **Range** | Minimum, centre, and maximum values in the parameter's own domain. Narrow the range so sliders and AI-generated values stay in the useful zone. |
| **Scale** | Linear, logarithmic, or exponential response curve. |

### Actions

- **Select All / Deselect All** — Toggle visibility across the whole list.
- **AI Detect** — Ask the AI agent to classify the entire parameter set (see below).
- **Apply** — Save changes without closing the dialog.
- **OK / Cancel** — Save and close, or discard.

## AI Detect

Typing units and ranges by hand for a plugin with dozens of parameters is tedious. The **AI Detect** button asks the configured AI agent to classify each parameter based on its name and populate units, ranges, centres, and scales in one pass.

The results populate the dialog so you can review them before applying — tweak anything you disagree with and hit **Apply**.

!!! note
    AI Detect uses whichever provider is configured in [Preferences > AI](interface/preferences.md). It works with both cloud providers and local inference.

## Learn Mode

Even with units sorted out, plugin parameter names rarely match the labels printed on the plugin's own UI — a knob labelled "Drive" in the GUI might be parameter #24 called `Saturation`. Learn mode closes that gap.

On any plugin device slot in the [FX chain](fx-chain.md):

1. Open the plugin's own window.
2. Click the **Learn** button in the device slot header — it highlights.
3. Touch any control in the plugin's window.
4. MAGDA navigates to the parameter page that contains the matching parameter and highlights its slot.

Click Learn again to exit. This is the fastest way to map an unfamiliar plugin, or to find the right parameter before setting up automation, modulation, or a macro link.

!!! note
    Learn is only enabled while the plugin's own window is open, and is hidden for MAGDA's built-in devices (their parameter names already match the UI).
