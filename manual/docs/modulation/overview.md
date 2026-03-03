# Modulation Overview

MAGDA provides two complementary modulation systems: **Modulators** and **Macros**. Both allow you to dynamically control device parameters without manual automation.

![Modulation — Curve Editor](../assets/images/modulation/curve-editor.png)

## Modulators

Modulators are signal generators that continuously vary a target parameter over time. MAGDA includes:

- **LFO** — Low-frequency oscillator with multiple waveforms (sine, triangle, sawtooth, square, random)
- **Bezier Curve Shape** — Freely editable modulation shape drawn with bezier curves for complex custom patterns

See [Modulators](modulators.md) for details.

## Macros

Macros are user-defined knobs that can control multiple parameters at once. Each track has 16 macro knobs (across 2 pages) that provide quick access to the most important parameters.

See [Macros](macros.md) for details.

## Hierarchical Scope

Modulators and macros are scoped to their parent track. A modulator on Track 1 can target any parameter within Track 1's device chain, but not parameters on other tracks.

## Multi-Target and Multi-Source

- A single modulator can drive **multiple target parameters** simultaneously
- A single parameter can be driven by **multiple modulation sources**, with their effects combined

## Linking

Parameters are connected to modulation sources using MAGDA's link mode. See [Linking Parameters](linking.md) for the workflow.
