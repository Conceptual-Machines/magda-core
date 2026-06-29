# Physical Models

Three struck physical models ship with MAGDA: **Marimba**, **Djembe**, and **Bell**. Each is a modal physical model driven by a strike exciter, not a sample. The ring and decay come out of the model itself, so there are no loops or release samples.

All three are 16-voice instruments. Marimba and Djembe follow the played note for pitch. Bell is fixed-pitch: the note only triggers the strike.

## Marimba

A struck tone-bar-and-tube model.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Strike Position** | 0 to 1 | 0.3 | Strike position on the bar (0 centre, 1 edge) |
| **Strike Tone** | 500 to 12000 Hz (log) | 7000 | Exciter brightness |
| **Strike Sharpness** | 0 to 1 | 0.25 | Exciter transient hardness |
| **Decay** | 50 to 2000 ms | 100 | Bar ring time (T60) |

## Djembe

A struck hand-drum membrane model.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Strike Position** | 0 to 1 | 0.4 | Strike location (0 centre, 1 edge) |
| **Strike Sharpness** | 0 to 1 | 0.5 | Exciter transient hardness |
| **Decay** | 50 to 3000 ms | 600 | Lowest-mode ring time (T60) |
| **Spacing** | 20 to 600 Hz | 200 | Modal frequency spacing (higher is more metallic) |
| **Inharmonicity** | 0 to 1 | 0 | Upper-mode spreading (0 even, 1 widest) |

At low Spacing the Djembe reads as a pitched tom; raise it for a more metallic, gong-like character.

## Bell

A struck, fixed-pitch modal church-bell model.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Strike Position** | 0 to 1 | 0.3 | Strike position on the bell (0 centre, 1 edge) |
| **Strike Tone** | 500 to 12000 Hz (log) | 7000 | Exciter brightness |
| **Strike Sharpness** | 0 to 1 | 0.25 | Exciter transient hardness |
| **Decay** | 300 to 40000 ms | 8000 | Ring time (T60); cathedral-long at the top, tine-short at the bottom |

## Macros, modulation, and presets

Each model is a standard MAGDA device, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).
