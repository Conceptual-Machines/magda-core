# Track Chain

<div class="magda-ui mc mc-chain" markdown="span">
<span class="mc-row mc-tchdr">[:knob-knob: :trackchain-icon-mods-boldm:](#header-controls)<span class="mc-sep"></span>:trackchain-icon-racks-boldm: :trackchain-multiband-rack: :trackchain-gain-staging:<span class="mc-sep"></span>[<span class="mc-preset">Presets :general-chevron_down:</span>](../fx-chain.md#track-chain-presets) :general-save:<span class="mc-sep"></span>[<span class="mc-name">7&ensp;Keys</span>](#header-controls)<span class="mc-push"></span>[:analysis-postfx:](../fx-chain.md#post-fx-section) :trackchain-oscilloscope3: :trackchain-icon-spectrum-boldm: :trackchain-icon-levels-boldm:<span class="mc-sep"></span><span class="mc-led"></span> :general-master_on::general-solo:<span class="mc-sep"></span><span class="mc-mono">-4.4 dB</span> <span class="mc-mono">L20</span><span class="mc-sep"></span><span class="mc-power">:general-power:</span> [<span class="mc-fold">:general-chevron_down:</span>](#folding-the-header)</span>
<span class="mc-devices">
<span class="mc-dev mc-grow">
<span class="mc-row mc-dhdr">:knob-knob: :trackchain-icon-mods-boldm:<span class="mc-sep"></span>:general-sidechain:<span class="mc-sep"></span>[<span class="mc-preset">Presets :general-chevron_down:</span>](../fx-chain.md#presets) :general-save:<span class="mc-sep"></span>[<span class="mc-name">Compressor</span>](../fx-chain.md#device-layout)<span class="mc-led mc-push"></span></span>
<span class="mc-dside"><span class="mc-on">:trackchain-device-params:</span><span class="mc-on">:trackchain-device-faceplate:</span></span>
<span class="mc-dbody"><span class="mc-col"><span class="mc-tabs"><span class="mc-sel">Curve</span><span>Timing</span><span>Level</span></span><span class="mc-cells"><span class="mc-cell">Threshold<b>-18.0 dB</b></span><span class="mc-cell">Ratio<b>4.0</b></span><span class="mc-cell">Knee<b>+6.0 dB</b></span></span></span><span class="mc-graph"><svg viewBox="0 0 100 80" preserveAspectRatio="none"><path d="M0 20H100M0 40H100M0 60H100M25 0V80M50 0V80M75 0V80" stroke="#232a33" stroke-width="1" fill="none" vector-effect="non-scaling-stroke"/><path d="M62 0V80" stroke="#c98a4a" stroke-width="1" vector-effect="non-scaling-stroke"/><path d="M0 76L60 40Q66 37 74 36L100 32" stroke="#5fc27a" stroke-width="2" fill="none" vector-effect="non-scaling-stroke"/></svg></span></span>
<span class="mc-strip">[<span class="mc-power">:general-power:</span>](../fx-chain.md#device-power)<span class="mc-mix"><span class="mc-caps">MIX</span>100</span><span class="mc-meter"><i></i><i></i></span>[<span class="mc-delta">Δ</span>](../fx-chain.md#delta-solo)</span>
</span>
<span class="mc-dev">
<span class="mc-row mc-dhdr">:knob-knob: :trackchain-icon-mods-boldm:<span class="mc-sep"></span><span class="mc-preset">Presets :general-chevron_down:</span> :general-save:<span class="mc-sep"></span><span class="mc-name">Reverb</span><span class="mc-led mc-push"></span></span>
<span class="mc-dside"><span class="mc-on">:trackchain-device-params:</span><span>:trackchain-device-faceplate:</span></span>
<span class="mc-dbody"><span class="mc-cells" style="--mc-cols: 1"><span class="mc-cell">Predelay<b>20.0 ms</b></span><span class="mc-cell">Decay<b>50.0</b></span></span><span class="mc-graph" style="flex-basis: 5em"><svg viewBox="0 0 100 80" preserveAspectRatio="none"><path d="M70 0V80" stroke="#7c858d" stroke-width="1" stroke-dasharray="3 3" fill="none" vector-effect="non-scaling-stroke"/><path d="M4 76V8C10 40 24 66 50 73S90 76 100 76Z" fill="#5b8dd6" fill-opacity="0.85"/></svg></span></span>
<span class="mc-strip"><span class="mc-power">:general-power:</span><span class="mc-mix"><span class="mc-caps">MIX</span>30</span><span class="mc-meter"><i></i><i></i></span><span class="mc-delta">Δ</span></span>
</span>
[+](../fx-chain.md#working-with-devices){ .mc-add }
</span>
</div>

Displayed in the bottom panel when a track or device is selected. Shows the track's FX chain as a horizontal strip of devices:

- **Drag** devices to reorder them in the chain
- **Click** a device to select it and show it in the [Inspector](inspector.md#device-inspector)
- **Drag** plugins from the Plugin Browser onto the chain to add them, or click the **+** at the end of the chain
- **Right-click** a device for **Add to New Rack** and **Delete**; ++backspace++ deletes the selected devices
- **Racks** are shown as containers with nested parallel chains

See [FX Chain & Racks](../fx-chain.md) for working with devices, presets, and racks.

When you select a Drum Grid pad's channel, the panel shows that pad's chain, and devices you add land in the pad's chain.

## Header Controls

The header runs across the top of the panel. From the left:

- **Track macros** / **Track modulators** — show the track's own [macro](../modulation/macros.md) and [modulator](../modulation/modulators.md) panels at the start of the chain
- ![rack](../assets/images/icons/rack.svg){ width="16" } **Add rack** / **Add multiband rack** — create a new rack on the track
- **Gain staging** — start a [gain-staging](../fx-chain.md#gain-staging) pass
- **Presets** — the [track preset](../fx-chain.md#track-chain-presets) box and the floppy disk that saves one
- **Track name**
- **Post-FX panel**, **Oscilloscope**, **Spectrum Analyzer**, **Levels** — show the [post-FX panel](../fx-chain.md#post-fx-section), and add or remove each analyzer there
- **MIDI light**, **Mute** (speaker), **Solo** (ring)
- **Volume** and **pan**
- ![power](../assets/images/icons/power_on.svg){ width="16" } **Power** — switch the whole track chain on or off
- **Fold** — the chevron at the far right

The master shows no MIDI light, solo, pan, or power. On a pad channel the presets, gain staging, multiband rack, and power buttons are hidden. The chord track shows only its name, volume, and its audition control.

## Folding the Header

Click the chevron at the right end of the header to fold it to a thin, darker strip; the controls fade out as it folds. The MIDI light stays visible on the strip. Click anywhere on the strip to bring the header back.

Right-click the strip (or the chevron) for a menu that shows or hides the **Post-FX Panel** and adds or removes the **Oscilloscope**, **Spectrum Analyzer**, and **Levels** analyzers.

MAGDA remembers whether the header is folded between sessions. Folding applies to the track chain only; the other bottom-panel editors keep their toolbars.
