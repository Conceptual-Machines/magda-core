# Linking Parameters

Linking connects modulation sources (modulators or macros) to target parameters on devices.

<div class="magda-ui mc" markdown="span">
<span class="mc-row mc-tchdr">:knob-knob: :trackchain-icon-mods-boldm:<span class="mc-sep"></span>:trackchain-icon-racks-boldm: :trackchain-multiband-rack: :trackchain-gain-staging:<span class="mc-sep"></span><span class="mc-preset">Presets :general-chevron_down:</span> :general-save:<span class="mc-sep"></span><span class="mc-name">7&ensp;Keys</span>[LINK MODE](#link-mode){ .mc-banner }:analysis-postfx: :trackchain-oscilloscope3: :trackchain-icon-spectrum-boldm: :trackchain-icon-levels-boldm:<span class="mc-sep"></span><span class="mc-power">:general-power:</span></span>
<span class="mc-panels">
<span class="mc-row mc-dhdr">:knob-knob: <span class="mc-on-mod">:trackchain-icon-mods-boldm:</span> :ai-ai-sound-designer:<span class="mc-sep"></span><span class="mc-preset">Presets :general-chevron_down:</span> :general-save:<span class="mc-sep"></span><span class="mc-name">Poly Synth</span><span class="mc-led mc-push"></span></span>
<span class="mc-panel"><span class="mc-ptitle">[<span class="mc-caps mc-mod">MODS</span>](modulators.md)<span class="mc-push mc-dim">&lsaquo;&ensp;1/1&ensp;&rsaquo;</span><span class="mc-dim">+</span></span><span class="mc-grid4"><span class="mc-card mc-modcard mc-sel mc-linking"><span class="mc-tag">SINE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 12C12 0 22 0 34 12S56 24 68 12" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 76%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">1/4</span><span class="mc-dim mc-push">1</span><span class="mc-on-att">:general-link_flat:</span></span></span><span class="mc-card mc-modcard"><span class="mc-tag">CURVE</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 18C20 18 30 6 50 5L68 5" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 74%; top: 21%"></i></span><span class="mc-row"><span class="mc-time">1/8</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">ADSR</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 3L8 16L44 16L68 21" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 88%"></i></span><span class="mc-row"><span class="mc-time">435 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">S&amp;H</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 16H20V12H38V14H54V10H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 42%"></i></span><span class="mc-row"><span class="mc-time">2.00 Hz</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-modcard"><span class="mc-tag">FOL</span><span class="mc-thumb"><svg viewBox="0 0 68 24" preserveAspectRatio="none"><rect y="8" width="68" height="10" fill="#0f1317"/><path d="M0 14H68" stroke="#ff8822" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg><i class="mc-mdot" style="left: 100%; top: 58%"></i></span><span class="mc-row"><span class="mc-time">50 ms</span><span class="mc-dim mc-push">1</span>:general-link_flat:</span></span><span class="mc-card mc-empty">+ Add mod</span><span class="mc-card mc-empty"></span><span class="mc-card mc-empty"></span></span></span>
<span class="mc-panel mc-grow-panel"><span class="mc-dbody" style="padding: 0"><span class="mc-col"><span class="mc-tabs"><span class="mc-sel mc-caps">FILTER 1</span><span class="mc-caps">FILTER 2</span></span><span class="mc-cells" style="--mc-cols: 2">[<span class="mc-cell mc-slider">Cutoff<span class="mc-row"><span class="mc-strack" style="--mc-v: 30%"></span><span class="mc-sval">30%</span></span></span>](#link-settings)[<span class="mc-cell">Reso<b>30.0%</b><i class="mc-amt" style="left: 30%; width: 45%"></i></span>](#visual-indicators)<span class="mc-cell">Env Amt<b>0.0 oct</b></span>[<span class="mc-cell">Drive<b>0.0%</b><i class="mc-amt" style="left: 0%; width: 20%"></i></span>](#visual-indicators)</span></span><span class="mc-graph" style="flex-basis: 8em"><svg viewBox="0 0 100 80" preserveAspectRatio="none"><path d="M0 20H100M0 40H100M0 60H100M30 0V80M60 0V80" stroke="#232a33" fill="none" vector-effect="non-scaling-stroke"/><path d="M64 0V80" stroke="#5b8dd6" fill="none" vector-effect="non-scaling-stroke"/><path d="M0 34C30 34 52 33 60 24Q64 18 68 28L100 66" stroke="#7aa7e8" stroke-width="1.5" fill="none" vector-effect="non-scaling-stroke"/></svg></span></span></span>
</span>
</div>

## Link Mode

Link mode is the workflow for creating modulation connections:

1. **Enter link mode** — Click the link button in the corner of a modulator's or macro's card. **LINK MODE** shows in the track chain header while it is on
2. **Click a target** — Click any device parameter in the source's scope. MAGDA links it at 30% and shows a slider over the parameter
3. **Adjust the link** — Drag the slider to set the amount (0–100%). Clicking a parameter that is already linked shows the slider at its current amount
4. **Exit link mode** — Press ++escape++ or click the link button again

## Drag and Drop

You can also create links by dragging a modulator's or macro's card directly onto a target parameter. This is a shortcut that skips the link-mode workflow.

## Link Settings

Each link has the following properties:

- **Amount** — How much the source affects the target. A modulator's link can be negative, from −100% to +100%
- **Bipolar** — When enabled, modulation swings both above and below the parameter's current value. When disabled, modulation only adds to the current value
- **On** / **Off** — A modulator's link can be switched off without removing it

Edit a source's links on the **Links** tab of its [modulator editor](modulators.md#the-modulator-editor), or in the macro editor. To see everything driving one parameter, hover over it and hold ++alt++: a popover lists each link with its source, an amount you can drag, a **BI** / **UNI** switch, and a button that removes the link.

## Visual Indicators

When modulation is active:

- Outside link mode, a bar under each linked parameter shows the modulation moving in real time
- In link mode, the bar shows the link amount for the source you are linking instead
- The source's link button stays lit while its link mode is on

## Linking Modulator-to-Modulator

Modulators can target each other's **Rate** to create more interesting motion than a single LFO can produce on its own. A slow LFO modulating a faster LFO's rate gives you accelerating-then-decelerating wobbles; a square-wave LFO driving another LFO's rate gives you discrete tempo shifts.

To create the link:

1. Right-click the source modulator's card and pick the destination from **Link to Parameter... → Modulators → Rate**, or
2. Use link mode with the source modulator and click the destination's Rate field in its editor.
3. Adjust the modulation amount as usual.

A modulator's Rate can be driven by **modulators and macros from the same scope** (track, rack, or device). Cross-scope links — say, a track-level macro driving a Rate inside a nested rack — are not supported.

## Same-Scope Rule

Macro→param, macro→mod, mod→param, and mod→mod links must all stay within the same scope. The scope is the parent that owns the source: track-level macros target track-level mods or any device on the track; rack-level macros target rack-level mods or devices in that rack; device-level macros and mods target only that device.
