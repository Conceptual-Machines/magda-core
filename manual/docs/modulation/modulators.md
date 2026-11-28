# Modulators

Modulators are signal generators that modulate device parameters over time. A track's modulators can target any parameter on any device in the track's chain; racks and devices have modulators of their own too (see [Hierarchical Scope](overview.md#hierarchical-scope)).

Open a track's modulators with the **Track modulators** button in the track chain header, or a rack's or device's with the **Mods** button in its header. They also appear in the [Inspector](../panels/inspector.md#mods-and-macros). Click **+ Add mod** in the next free slot and pick **LFO**, **Curve**, **Envelope**, **Random**, or **Follower**. Modulators fill the slots in order; click **+** in the panel's header to add another page of slots, and right-click the header to **Remove last page**.

<div class="magda-ui mc" markdown="span">
<span class="mc-panels">
<span class="mc-row mc-dhdr">[<span class="mc-on-mac">:knob-knob:</span> <span class="mc-on-mod">:trackchain-icon-mods-boldm:</span>](#modulators) :ai-ai-sound-designer:<span class="mc-sep"></span><span class="mc-preset">Presets :general-chevron_down:</span> :general-save:<span class="mc-sep"></span><span class="mc-name">Poly Synth</span><span class="mc-led mc-push"></span></span>
<span class="mc-panel"><span class="mc-ptitle">[<span class="mc-caps mc-macro">MACROS</span>](macros.md)<span class="mc-push mc-dim">&lsaquo;&ensp;1/2&ensp;&rsaquo;</span><span class="mc-dim">+</span></span><span class="mc-grid4"><span class="mc-card mc-mcard mc-sel"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M8.2 23.8A11 11 0 0 1 21.9 6.7" stroke="#7777dd" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L19.8 10.1" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-val">62%</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M8.2 23.8A11 11 0 0 1 8.9 7.6" stroke="#7777dd" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L11.5 10.7" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-val">35%</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span><span class="mc-card mc-mcard"><svg class="mc-knob" viewBox="0 0 32 32"><path d="M8.2 23.8A11 11 0 1 1 23.8 23.8" stroke="#2f3640" stroke-width="3" fill="none" stroke-linecap="round"/><path d="M16 16L16.0 9.0" stroke="#e8edf1" stroke-width="2" stroke-linecap="round"/></svg><span class="mc-row"><span class="mc-dim">-</span>:general-link_flat:</span></span></span></span>
<span class="mc-panel mc-editor mc-medit"><span class="mc-ptitle">[<span class="mc-caps mc-macro">MACRO EDIT</span>](macros.md#layout)</span><span class="mc-name" style="text-align: center">Macro 1</span><span class="mc-dim">Value</span><span class="mc-box">0.62</span><span class="mc-dim">Links</span><span class="mc-lrow">Cutoff<span class="mc-push">50%</span><span class="mc-dim">Un</span><span class="mc-dim">x</span></span><span class="mc-lrow">Resonance<span class="mc-push">30%</span><span class="mc-dim">Un</span><span class="mc-dim">x</span></span></span>
<span class="mc-panel"><span class="mc-ptitle">[<span class="mc-caps mc-mod">MODS</span>](#modulators)<span class="mc-push mc-dim">&lsaquo;&ensp;1/1&ensp;&rsaquo;</span><span class="mc-dim">+</span></span><span class="mc-grid4"><span class="mc-card mc-modcard"><span class="mc-tag">SINE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 12C12 0 22 0 34 12S56 24 68 12" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 76%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">1/4</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">CURVE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 18C20 18 30 6 50 5L68 5" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 74%; top: 21%"></i></span><span class="mc-row"><span class="mc-time">1/8</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard mc-sel"><span class="mc-tag">ADSR</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 3L8 16L44 16L68 21" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">435 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">S&amp;H</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 16H20V12H38V14H54V10H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 42%"></i></span><span class="mc-row"><span class="mc-time">2.00 Hz</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">FOL</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 14H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 58%"></i></span><span class="mc-row"><span class="mc-time">50 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-empty">[+ Add mod](#modulators)</span><span class="mc-card mc-empty"></span><span class="mc-card mc-empty"></span></span></span>
<span class="mc-panel mc-editor"><span class="mc-tabs">[<span class="mc-sel mc-caps">ENVELOPE</span>](#envelope-adsr)[<span class="mc-caps">LINKS 1</span>](#the-modulator-editor)</span><span class="mc-display"><svg viewBox="0 0 100 50" preserveAspectRatio="none"><path d="M50 0V50M75 0V50" stroke="#3a424c" stroke-dasharray="3 3" fill="none" vector-effect="non-scaling-stroke"/><path d="M2 46L6 4L32 30L76 30L98 44" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><span class="mc-tag">Idle</span></span><span class="mc-pairs"><span>A 15 ms</span><span>D 420 ms</span><span>S 0.30</span><span>R 650 ms</span></span><span class="mc-row mc-small">A crv 0.00<span>D crv 0.00</span><span>R crv 0.00</span></span><span class="mc-row">[<span class="mc-caps mc-dim">TRIG</span>](#the-modulator-editor)<span class="mc-field mc-row" style="flex: 1">MIDI<span class="mc-push">:general-chevron_down:</span></span>:general-settings_nobg:</span></span>
</span>
</div>

Each card shows the modulator's shape, a short type tag (such as SINE, ADSR, S&H, or FOL), its rate or time, and how many links it has. The name appears on the card only while you rename it.

Right-click a modulator's card for **Rename**, **Show Automation Lane**, **Disable** / **Enable**, the **Link to Parameter...** submenus, **Unlink** for each of its links, **Clear All Links**, **Learn MIDI** (which maps a controller to the modulator's rate), and **Remove**. Custom names such as "Filter Wobble" or "Swell" make a track with several LFOs and envelopes far easier to read.

## The Modulator Editor

Click a modulator's card to open its editor beside the panel; click the card again to close it. The editor has two tabs at the top:

- **The modulator's name** — the first tab, over the modulator's controls. Double-click the name to rename it.
- **Links** — how many parameters the modulator drives, over a list of its links. Each row shows the target parameter, the link amount (drag across the row to change it, from −100% to +100%), **BI** / **UN** for bipolar or unipolar, **ON** / **OFF**, and **x** to remove the link.

Every modulator except the follower ends on its **TRIG** row: **Free** (runs continuously), **Transport** (restarts when playback starts), **MIDI** (restarts on incoming notes), or **Audio** (restarts on incoming audio). For **MIDI** and **Audio** on a device or rack modulator, the settings button beside it picks what triggers it: the track itself or another track.

The LFO, Curve, and Random modulators share a rate row: the **Free** / **Sync** button switches between a rate in Hz and a tempo-synced division (such as 1 Bar, 1/4, or 1/8), and the field beside it sets the value.

## LFO

A low-frequency oscillator that cycles through a waveform shape. Pick the shape from the row of icons at the top:

- Sine
- Triangle
- Square
- Saw Up
- Saw Down

Below the shape display sit the rate row and the trigger. With the **Audio** trigger, **Atk** and **Rel** set how the trigger follows the incoming level.

## Curve

A freely editable modulation shape. Use it to create complex, custom modulation patterns that go beyond standard waveforms.

<div class="magda-ui mc mc-fit" markdown="span">
<span class="mc-panels">
<span class="mc-row mc-dhdr">:knob-knob: <span class="mc-on-mod">:trackchain-icon-mods-boldm:</span> :ai-ai-sound-designer:<span class="mc-sep"></span><span class="mc-preset">Presets :general-chevron_down:</span> :general-save:<span class="mc-sep"></span><span class="mc-name">Poly Synth</span><span class="mc-led mc-push"></span></span>
<span class="mc-panel"><span class="mc-ptitle">[<span class="mc-caps mc-mod">MODS</span>](#modulators)<span class="mc-push mc-dim">&lsaquo;&ensp;1/1&ensp;&rsaquo;</span><span class="mc-dim">+</span></span><span class="mc-grid4"><span class="mc-card mc-modcard"><span class="mc-tag">SINE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 12C12 0 22 0 34 12S56 24 68 12" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 76%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">1/4</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard mc-sel"><span class="mc-tag">CURVE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 18C20 18 30 6 50 5L68 5" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 74%; top: 21%"></i></span><span class="mc-row"><span class="mc-time">1/8</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">ADSR</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 3L8 16L44 16L68 21" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">435 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">S&amp;H</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 16H20V12H38V14H54V10H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 42%"></i></span><span class="mc-row"><span class="mc-time">2.00 Hz</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">FOL</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 14H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 58%"></i></span><span class="mc-row"><span class="mc-time">50 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-empty">+ Add mod</span><span class="mc-card mc-empty"></span><span class="mc-card mc-empty"></span></span></span>
<span class="mc-panel mc-editor"><span class="mc-tabs">[<span class="mc-sel mc-caps">CURVE</span>](#curve)[<span class="mc-caps">LINKS 1</span>](#the-modulator-editor)</span><span class="mc-row">[<span class="mc-field mc-row">Preset<span class="mc-push">:general-chevron_down:</span></span>](#curve):general-save: [<span class="mc-on-mod">:waveform-curve:</span>](#editing)</span>[<span class="mc-display mc-curve"><svg viewBox="0 0 100 60" preserveAspectRatio="none"><path d="M0 15H100M0 30H100M0 45H100M25 0V60M50 0V60M75 0V60" stroke="#232a33" fill="none" vector-effect="non-scaling-stroke"/><path d="M4 54C14 54 22 52 28 48C36 42 42 34 52 24C60 16 66 12 74 10C82 8 88 8 96 7V60H4Z" fill="#ff8822" fill-opacity="0.16"/><path d="M4 54C14 54 22 52 28 48C36 42 42 34 52 24C60 16 66 12 74 10C82 8 88 8 96 7" stroke="#ff8822" stroke-width="2" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-pt" style="left: 4%; top: 90%"></i><i class="mc-pt mc-handle" style="left: 28%; top: 80%"></i><i class="mc-pt" style="left: 52%; top: 40%"></i><i class="mc-pt" style="left: 64%; top: 23%"></i><i class="mc-pt mc-handle" style="left: 74%; top: 17%"></i><i class="mc-pt" style="left: 96%; top: 12%"></i></span>](#editing)<span class="mc-row">[<span class="mc-chip mc-caps">SYNC</span>](#the-modulator-editor)<span class="mc-box" style="flex: 1">1/8</span></span><span class="mc-row">[<span class="mc-caps mc-dim">TRIG</span>](#the-modulator-editor)<span class="mc-field mc-row" style="flex: 1">Free<span class="mc-push">:general-chevron_down:</span></span>:general-settings_nobg:</span></span>
</span>
</div>

The row at the top holds a **Preset** menu of starting shapes (Triangle, Sine, Ramp Up, Ramp Down, S-Curve, Exp, Log) and a button that opens the curve in its own, larger editor window. Below the curve sit the rate row and the trigger.

### Editing

- **Add a point** — Double-click an empty spot on the curve
- **Move a point** — Drag it to a new position
- **Adjust curvature** — Drag the handle between two points to bend that segment; double-click the handle to straighten it
- **Delete a point** — Double-click an existing point

The editor window adds a **Loop** / **1-Shot** switch, so the curve can play once instead of looping, along with grid and snap settings and buttons to save the curve as a preset or reset it.

## Random

A randomised modulation source with its own shaping and smoothing controls. It runs as stepped random values or as continuous noise.

### Type

The **Random** / **Noise** switch at the top selects the source:

- **Random** — Generates a new random value each cycle (sample & hold)
- **Noise** — Continuous random signal

### Parameters

- **Shp** (Shape) — Biases the distribution of the random values
- **Smo** (Smooth) — Smooths the transitions between values (0 = hard steps, 1 = gliding)
- **Stp** (Step Depth) — Amount of stepped variation between values
- **Rate** — The rate row sets the speed, free-running or tempo-synced
- **TRIG** — How the modulator is (re)started

## Envelope (ADSR)

An ADSR envelope used as a modulation source. Triggered by playback or incoming MIDI notes, it shapes a parameter through attack, decay, sustain, and release stages. The display at the top shows the envelope shape and its current state.

### Parameters

- **A** (Attack) — Rise time to peak (ms)
- **D** (Decay) — Fall time to the sustain level (ms)
- **S** (Sustain) — Held level while gated (0–1)
- **R** (Release) — Fall time after the gate releases (ms)
- **A crv / D crv / R crv** — Bend each stage from logarithmic to exponential
- **TRIG** — What starts the envelope: **Free** (cycles continuously), **Transport** (starts on play), **MIDI** (gated by note on/off), or **Audio**

## Envelope Follower

Follows the amplitude of an audio signal and uses it as a modulation source. Use it to make one parameter react to the dynamics of a track, for example ducking a pad from a kick drum. The display at the top shows the live follower output.

### Parameters

- **Gain** — Input gain into the detector (dB)
- **A** (Attack) — How fast the follower rises
- **H** (Hold) — Time held at the peak before releasing
- **R** (Release) — How fast the follower falls

Times from one second up read in seconds.

### Detection filters

Band-limit what the follower reacts to so it tracks only part of the spectrum:

- **HP** — Toggle on and set the cutoff to ignore low frequencies
- **LP** — Toggle on and set the cutoff to ignore high frequencies

Cutoffs from 1 kHz up read in kHz.

### Audio source

Click the **Audio Source** button at the bottom to choose what the follower listens to — the track's own audio, or another track's audio routed in as a sidechain.
