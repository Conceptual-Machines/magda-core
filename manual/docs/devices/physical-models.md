# Physical Models

Three struck physical models ship with MAGDA: **Marimba**, **Djembe**, and **Bell**. Each is a modal physical model driven by a strike exciter, not a sample. The ring and decay come out of the model itself, so there are no loops or release samples.

All three are 8-voice instruments. Marimba and Djembe follow the played note for pitch. Bell is fixed-pitch: the note only triggers the strike.

## Faceplate

The three models share one faceplate: an **EXCITER** column, a **RESONATOR** column, and a drawing of the body on the right, a tone bar, a drum membrane, or a bell in cross-section. The dot on the body marks the **STRIKE POSITION**; click or drag on the body to move it, which sets the same control as **Position** in the EXCITER column. The body flashes on each note.

The sidebar's **Show the parameters** toggle hides the two columns, and **Show the body** hides the drawing. One of the two always shows.

There is no gain control on the faceplate: set the model's level with the fader on the device's side strip.

## Marimba

A struck tone-bar-and-tube model.

| Column | Control | Range | Default | Description |
|--------|---------|-------|---------|-------------|
| EXCITER | **Position** | 0 to 1 | 0.3 | Strike position on the bar (0 centre, 1 edge) |
| EXCITER | **Tone** | 500 to 12000 Hz (log) | 7000 | Exciter brightness |
| EXCITER | **Sharpness** | 0 to 1 | 0.25 | Exciter transient hardness |
| RESONATOR | **Decay** | 50 to 2000 ms | 100 | Bar ring time (T60) |

## Djembe

A struck hand-drum membrane model.

| Column | Control | Range | Default | Description |
|--------|---------|-------|---------|-------------|
| EXCITER | **Position** | 0 to 1 | 0.4 | Strike location (0 centre, 1 edge) |
| EXCITER | **Sharpness** | 0 to 1 | 0.5 | Exciter transient hardness |
| RESONATOR | **Decay** | 50 to 3000 ms | 600 | Lowest-mode ring time (T60) |
| RESONATOR | **Spacing** | 20 to 600 Hz | 200 | Modal frequency spacing (higher is more metallic) |
| RESONATOR | **Inharmonicity** | 0 to 1 | 0 | Upper-mode spreading (0 even, 1 widest) |

At low Spacing the Djembe reads as a pitched tom; raise it for a more metallic, gong-like character.

## Bell

A struck, fixed-pitch modal church-bell model.

| Column | Control | Range | Default | Description |
|--------|---------|-------|---------|-------------|
| EXCITER | **Position** | 0 to 1 | 0.3 | Strike position on the bell (0 centre, 1 edge) |
| EXCITER | **Tone** | 500 to 12000 Hz (log) | 7000 | Exciter brightness |
| EXCITER | **Sharpness** | 0 to 1 | 0.25 | Exciter transient hardness |
| RESONATOR | **Decay** | 300 to 40000 ms | 8000 | Ring time (T60); cathedral-long at the top, tine-short at the bottom |

## Macros, modulation, and presets

Each model is a standard MAGDA device, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).

## AI sound design

Click the **AI** icon in the device header and describe the sound you want. The agent reads this device's own parameters and their real-world ranges, then writes values into them. See [Per-Device AI Panel](../panels/ai-assistant.md#per-device-ai-panel).
