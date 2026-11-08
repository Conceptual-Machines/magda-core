# Strum

Strum is a MIDI device that turns a held chord into a strum, a roll or a quick arpeggio. Instead of every note starting at once, the notes start one after another, spaced along a curve you choose.

## Overview

Strum sits on a track's device chain before an instrument, like the [Arpeggiator](arpeggiator.md). It works with any instrument, MAGDA's own or a plugin. Play a chord and Strum sends its notes on in the chosen order, spread over the **Strum Length**. The notes keep the velocities you played.

The panel beside the parameters shows where each strummed note lands in time for the current **Shape** and **Cycles**. It previews the timing, not a particular chord.

## Triggering

**Trigger** sets when a strum happens:

- **Chord** strums once each time you play a chord. Strum waits a moment after the first note so the whole chord is caught, and strums again if you add a note. The notes ring until you release the chord.
- **Loop** strums the held chord over and over until you release it. Each new strum ends the notes of the one before.

In Loop mode, **Loop Sync** sets how the gap between strums is measured: **Time** uses **Sync Interval** in milliseconds, and **Beat** uses **Loop Rate**, a note value that follows the project tempo.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Trigger** | Chord, Loop | Chord | Strum once per chord, or repeatedly while it's held |
| **Order** | Up, Down, Up/Down, As Played | Up | The order the notes start in. Up goes from the lowest note, Down from the highest, Up/Down goes up then back down, and As Played follows the order you pressed the keys. |
| **Shape** | Linear, Ease In, Ease Out, Snap, Spike, S-Curve, Overshoot, Bounce | Ease In | The curve that spaces the notes across the strum |
| **Cycles** | 1–8 | 1 | Splits the strum into that many short strums, each following the shape |
| **Strum Length** | 1–400 ms | 90 ms | Time from the first note to the last |
| **Loop Sync** | Time, Beat | Time | In Loop mode, measure the gap between strums in milliseconds or note values |
| **Loop Rate** | 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T | 1/4 | Gap between strums with Loop Sync set to Beat |
| **Sync Interval** | 60–2000 ms | 500 ms | Gap between strums with Loop Sync set to Time |

## Macros, modulation, and presets

Strum sits in the standard device slot, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and its settings save and recall through the [preset system](../fx-chain.md#presets).
