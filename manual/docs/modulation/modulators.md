# Modulators

Modulators are signal generators that modulate device parameters over time.

## LFO

A low-frequency oscillator that cycles through a waveform shape.

### Waveforms

- Sine
- Triangle
- Sawtooth
- Square
- Sample & Hold
- Custom curve (draw your own shape)

### Parameters

- **Rate** — Speed of the LFO cycle (Hz or synced to tempo)
- **Depth** — Modulation amount
- **Phase offset** — Starting point in the waveform cycle (0°–360°)
- **Tempo sync** — Lock the rate to musical divisions (1/4, 1/8, 1/16, etc.)
- **One-shot** — Play the waveform once instead of looping
- **Trigger mode** — Free-running, note-retrigger, or transport-retrigger

## Envelope

A triggered shape that modulates a parameter over time, typically following ADSR curves.

### Parameters

- **Attack** — Rise time
- **Decay** — Fall time to sustain level
- **Sustain** — Held level
- **Release** — Fade-out time after trigger ends
- **Trigger mode** — MIDI note-on, transport start, or manual

## Random

Generates randomized modulation values.

### Parameters

- **Rate** — How often a new random value is generated
- **Smoothing** — Interpolation between random values (0 = stepped, 100 = smooth)
- **Range** — Min/max bounds for the random output
- **Tempo sync** — Lock the rate to musical divisions

## Follower

Tracks the amplitude of an audio signal and outputs a modulation value that mirrors the signal's loudness.

### Parameters

- **Input source** — Which audio signal to follow (track input, sidechain, etc.)
- **Attack** — How quickly the follower responds to rising levels
- **Release** — How quickly the follower responds to falling levels
- **Gain** — Sensitivity adjustment
