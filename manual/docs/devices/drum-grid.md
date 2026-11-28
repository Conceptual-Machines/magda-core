# Drum Grid

The Drum Grid is MAGDA's pad-based drum instrument. Each pad holds its own chain of devices, and each pad that holds a sound is its own mixer channel, giving you independent processing and mixing per drum voice.

<div class="magda-ui me-device" markdown="span">
<span>
<span class="me-dev">
<span class="me-dhead">[:material-gauge:](../modulation/macros.md){ title="Macros" } [:material-sine-wave:](../modulation/modulators.md){ title="Modulators" } <span class="me-presets">Presets</span> :general-save: <b>Drum Grid</b></span>
<span class="me-dbody">
<span class="me-dside">[:trackchain-icon-padpanel-boldm:](#overview){ .me-ib .me-on title="Show the pad panel" }[:trackchain-icon-padchain-boldm:](#overview){ .me-ib .me-on title="Show the pad's chain" }<span class="me-gap"></span>[&#9664;&ensp;&#9654;](#overview){ .me-pagers title="Pages" }</span>
[<span>C1</span><span>C#1</span><span>D1</span><span>D#1</span><span>G#0</span><span>A0</span><span>A#0</span><span>B0</span><span>E0</span><span>F0</span><span>F#0</span><span>G0</span><span class="me-pad me-sel" style="--c:#5b8fd0"><small>C0</small><b>Kick</b></span><span class="me-pad" style="--c:#57b06a"><small>C#0</small><b>Snare</b></span><span class="me-pad" style="--c:#c76b6b"><small>D0</small><b>Hat</b></span><span class="me-pad" style="--c:#9a6fd0"><small>D#0</small><b>Clap</b></span>](#pads){ .me-pgrid }
<span class="me-ppanel">[<b>Kick</b>](#pad-channels)
<code>C0</code>
Gain
<span class="me-bar me-gain">0.0 dB</span>
Pan
<span class="me-bar me-pan">C</span>
<span class="me-row"><span class="me-ib">:general-speaker:</span><span class="me-ib">:general-solo:</span><span class="me-ib me-power">:material-power:</span></span>
</span>
</span>
</span>
<span class="me-dev me-chain">
<span class="me-dhead">:material-gauge: :material-sine-wave: :material-creation: <span class="me-presets">Presets</span> :general-save: [<b>Kick</b>](#pad-chain)</span>
<span class="me-dbody">
<span class="me-dside"><span class="me-ib me-on">:trackchain-device-params:</span><span class="me-ib me-on">:general-bar_chart:</span></span>
<span class="me-col">
<span class="me-tabs"><span>TRANSIENT</span><span>BODY</span><span>CLICK</span></span>
<span class="me-params"><span>Level<b>50.0%</b></span><span>Pitch<b>220.0</b></span><span>Sweep<b>50.0%</b></span><span>Decay<b>8.0 ms</b></span><span>Curve<b>0.0</b></span></span>
</span>
<span class="me-scope"><svg viewBox="0 0 200 100" preserveAspectRatio="none"><g stroke="#ffffff10"><path d="M60 0V100M115 0V100M170 0V100"/></g><path d="M6.0 33.2 6.7 18.0 7.4 16.0 8.0 28.8 8.7 21.6 9.4 15.3 10.1 24.4 10.7 26.3 11.4 16.0 12.1 20.5 12.8 31.9 13.4 18.4 14.1 17.7 14.8 30.3 15.5 22.6 16.2 16.6 16.8 24.8 17.5 28.4 18.2 17.8 18.9 20.4 19.5 34.0 20.2 21.4 20.9 17.9 21.6 27.4 22.2 27.3 22.9 18.3 23.6 22.0 24.3 34.8 25.0 21.9 25.6 19.0 26.3 28.1 27.0 28.3 27.7 19.4 28.3 22.3 29.0 34.7 29.7 23.7 30.4 19.6 31.0 27.0 31.7 31.2 32.4 21.2 33.1 21.7 33.8 32.1 34.4 27.2 35.1 20.5 35.8 24.7 36.5 36.0 37.1 24.5 37.8 21.2 38.5 28.1 39.2 32.7 39.8 23.1 40.5 22.5 41.2 31.3 41.9 30.3 42.6 22.6 43.2 24.2 43.9 34.0 44.6 28.8 45.3 22.7 45.9 25.8 46.6 36.0 47.3 27.9 48.0 23.1 48.6 27.1 49.3 37.5 50.0 27.7 50.7 23.6 51.4 28.1 52.0 37.6 52.7 27.9 53.4 24.2 54.1 28.6 54.7 37.9 55.4 28.5 56.1 24.7 56.8 28.8 57.4 38.2 58.1 29.5 58.8 25.3 59.5 28.6 60.2 37.3 60.8 31.0 61.5 26.0 62.2 28.1 62.9 36.0 63.5 32.9 64.2 27.1 64.9 27.7 65.6 34.3 66.2 35.4 66.9 28.6 67.6 27.4 68.3 32.4 69.0 38.4 69.6 30.7 70.3 27.7 71.0 30.6 71.7 38.0 72.3 33.8 73.0 28.8 73.7 29.3 74.4 35.0 75.0 37.6 75.7 31.0 76.4 28.9 77.1 32.2 77.8 39.2 78.4 34.6 79.1 30.0 79.8 30.3 80.5 35.3 81.1 39.2 81.8 32.9 82.5 30.2 83.2 32.2 83.8 38.1 84.5 37.4 85.2 32.2 85.9 30.9 86.6 34.1 87.2 40.2 87.9 36.4 88.6 32.2 89.3 31.8 89.9 35.5 90.6 41.6 91.3 36.2 92.0 32.6 92.6 32.7 93.3 36.4 94.0 42.2 94.7 36.6 95.4 33.3 96.0 33.3 96.7 36.8 97.4 42.3 98.1 37.5 98.7 34.1 99.4 33.9 100.1 36.8 100.8 41.8 101.4 38.9 102.1 35.3 102.8 34.3 103.5 36.4 104.2 40.8 104.8 40.8 105.5 36.8 106.2 35.0 106.9 36.1 107.5 39.5 108.2 43.3 108.9 38.9 109.6 36.2 110.2 36.0 110.9 38.2 111.6 42.1 112.3 41.7 113.0 38.2 113.6 36.6 114.3 37.3 115.0 40.0 115.7 43.9 116.3 41.2 117.0 38.3 117.7 37.3 118.4 38.4 119.0 41.2 119.7 44.8 120.4 41.4 121.1 38.9 121.8 38.2 122.4 39.3 123.1 41.9 123.8 45.3 124.5 42.1 125.1 39.9 125.8 39.0 126.5 39.8 127.2 42.0 127.8 45.0 128.5 43.4 129.2 41.1 129.9 40.0 130.6 40.2 131.2 41.8 131.9 44.3 132.6 45.2 133.3 42.8 133.9 41.2 134.6 40.8 135.3 41.6 136.0 43.4 136.6 45.9 137.3 45.0 138.0 43.0 138.7 41.9 139.4 41.8 140.0 42.7 140.7 44.5 141.4 46.6 142.1 45.5 142.7 43.8 143.4 42.9 144.1 42.8 144.8 43.5 145.4 45.0 146.1 46.8 146.8 46.6 147.5 45.0 148.2 44.0 148.8 43.7 149.5 44.1 150.2 45.1 150.9 46.6 151.5 48.0 152.2 46.6 152.9 45.5 153.6 44.9 154.2 44.8 154.9 45.3 155.6 46.3 156.3 47.5 157.0 48.5 157.6 47.4 158.3 46.5 159.0 46.0 159.7 45.9 160.3 46.3 161.0 47.0 161.7 48.0 162.4 49.0 163.0 48.6 163.7 47.8 164.4 47.3 165.1 47.1 165.8 47.2 166.4 47.6 167.1 48.2 167.8 49.0 168.5 49.8 169.1 49.3 169.8 48.8 170.5 48.5 171.2 48.3 171.8 48.4 172.5 48.7 173.2 49.1 173.9 49.6 174.6 50.2 175.2 50.4 175.9 50.1 176.6 49.8 177.3 49.7 177.9 49.7 178.6 49.8 179.3 50.0 180.0 50.3 180.6 50.6 181.3 50.9 182.0 51.2 182.0 52.7 181.3 53.0 180.6 53.3 180.0 53.6 179.3 53.9 178.6 54.1 177.9 54.2 177.3 54.2 176.6 54.1 175.9 53.8 175.2 53.5 174.6 53.7 173.9 54.3 173.2 54.7 172.5 55.1 171.8 55.4 171.2 55.5 170.5 55.3 169.8 55.0 169.1 54.5 168.5 54.1 167.8 54.9 167.1 55.6 166.4 56.2 165.8 56.6 165.1 56.7 164.4 56.5 163.7 56.0 163.0 55.3 162.4 54.8 161.7 55.8 161.0 56.8 160.3 57.4 159.7 57.8 159.0 57.7 158.3 57.2 157.6 56.4 157.0 55.3 156.3 56.3 155.6 57.4 154.9 58.3 154.2 58.8 153.6 58.8 152.9 58.2 152.2 57.1 151.5 55.8 150.9 57.1 150.2 58.5 149.5 59.5 148.8 59.9 148.2 59.6 147.5 58.6 146.8 57.2 146.1 56.9 145.4 58.7 144.8 60.1 144.1 60.8 143.4 60.7 142.7 59.8 142.1 58.2 141.4 57.1 140.7 59.2 140.0 60.8 139.4 61.7 138.7 61.6 138.0 60.5 137.3 58.7 136.6 57.8 136.0 60.1 135.3 61.9 134.6 62.6 133.9 62.2 133.3 60.7 132.6 58.4 131.9 59.3 131.2 61.7 130.6 63.2 129.9 63.4 129.2 62.3 128.5 60.1 127.8 58.6 127.2 61.5 126.5 63.6 125.8 64.3 125.1 63.5 124.5 61.4 123.8 58.4 123.1 61.6 122.4 64.1 121.8 65.1 121.1 64.4 120.4 62.1 119.7 58.8 119.0 62.3 118.4 64.9 117.7 65.9 117.0 65.0 116.3 62.3 115.7 59.7 115.0 63.4 114.3 66.0 113.6 66.6 113.0 65.1 112.3 61.8 111.6 61.4 110.9 65.2 110.2 67.2 109.6 67.0 108.9 64.4 108.2 60.3 107.5 63.9 106.9 67.1 106.2 68.1 105.5 66.4 104.8 62.6 104.2 62.7 103.5 66.8 102.8 68.8 102.1 67.9 101.4 64.4 100.8 61.7 100.1 66.5 99.4 69.2 98.7 69.0 98.1 65.7 97.4 61.2 96.7 66.4 96.0 69.7 95.4 69.8 94.7 66.6 94.0 61.3 93.3 66.8 92.6 70.3 92.0 70.4 91.3 67.0 90.6 61.9 89.9 67.7 89.3 71.1 88.6 70.8 87.9 66.8 87.2 63.2 86.6 69.0 85.9 72.0 85.2 70.8 84.5 65.9 83.8 65.2 83.2 70.8 82.5 72.7 81.8 70.2 81.1 64.2 80.5 67.8 79.8 72.6 79.1 72.9 78.4 68.6 77.8 64.2 77.1 70.8 76.4 73.9 75.7 71.9 75.0 65.7 74.4 68.2 73.7 73.6 73.0 74.1 72.3 69.3 71.7 65.3 71.0 72.3 70.3 75.1 69.6 72.2 69.0 64.9 68.3 70.6 67.6 75.4 66.9 74.3 66.2 67.8 65.6 68.8 64.9 75.1 64.2 75.7 63.5 70.2 62.9 67.2 62.2 74.7 61.5 76.7 60.8 72.0 60.2 65.9 59.5 74.3 58.8 77.3 58.1 73.4 57.4 65.1 56.8 74.1 56.1 77.9 55.4 74.3 54.7 65.3 54.1 74.2 53.4 78.4 52.7 74.9 52.0 65.7 51.4 74.7 50.7 78.9 50.0 75.1 49.3 65.8 48.6 75.6 48.0 79.5 47.3 74.9 46.6 67.2 45.9 76.9 45.3 79.8 44.6 74.1 43.9 69.1 43.2 78.4 42.6 79.9 41.9 72.6 41.2 71.7 40.5 80.0 39.8 79.4 39.2 70.3 38.5 74.7 37.8 81.3 37.1 78.1 36.5 67.2 35.8 77.9 35.1 81.9 34.4 75.6 33.8 70.9 33.1 80.8 32.4 81.2 31.7 71.8 31.0 75.7 30.4 82.8 29.7 78.9 29.0 68.4 28.3 80.2 27.7 82.9 27.0 74.5 26.3 74.7 25.6 83.4 25.0 80.6 24.3 68.3 23.6 80.5 22.9 84.0 22.2 75.4 21.6 75.3 20.9 84.4 20.2 81.1 19.5 69.1 18.9 82.1 18.2 84.5 17.5 74.4 16.8 77.9 16.2 85.6 15.5 79.9 14.8 72.7 14.1 84.6 13.4 83.9 12.8 71.1 12.1 81.9 11.4 86.2 10.7 76.4 10.1 78.2 9.4 86.9 8.7 80.9 8.0 74.0 7.4 86.2 6.7 84.3 6.0 69.8Z" fill="#a3a3a8" opacity="0.85"/><path d="M6 12L170 52M6 52H190" stroke="#7d7ae6" stroke-width="1.6" fill="none"/></svg><span><i>0</i><i>288 ms</i><i>575 ms</i></span></span>
<span class="me-strip"><span class="me-ib me-power">:material-power:</span><span class="me-meter"></span></span>
[+](#pad-chain){ .me-drop title="Add to the pad's chain" }
</span>
</span>
</span>
[<span class="me-ib me-power">:material-power:</span><span class="me-meter"></span>](#overview){ .me-strip }
</div>

## Overview

The Drum Grid maps incoming MIDI notes to 64 pads, 16 to a page. The device shows three panels side by side: the pads, the selected pad's panel with its mix, and the selected pad's chain. The sidebar's **Show the pad panel** and **Show the pad's chain** toggles hide or show the last two, and its arrows turn the pages of pads. Like other devices, the Drum Grid has a side strip with a meter and gain fader.

## Pads

- Each pad is mapped to a specific MIDI note and shows that note and the name of the sound it holds
- Pads can be triggered from the Piano Roll, the Drum Grid Editor (bottom panel), or a MIDI controller
- Click a pad to select it and show its panel and chain; the play button on a loaded pad auditions it
- Drag a pad onto another to swap the two
- Right-click a loaded pad for **Analyze pad role**, which classifies its sample with the Sample Analyzer models to set the pad's role, and **Delete**, which clears the pad

Pad **labels**, instrument **roles**, and **drumkits** are managed by right-clicking rows in the [Drum Grid Editor](../panels/drum-grid-editor.md#row-labels-and-roles).

## Pad Chain

Each pad has one chain of devices, just like a track. Its instrument comes first, typically a [Sampler](sampler.md) playing the pad's sample; effects follow it, so you can put distortion on a snare without affecting the kick. To layer several sounds on one pad, put a [rack](../fx-chain.md#racks) in its chain.

- Drop an instrument, a MAGDA one or a plug-in, on a pad to make it the pad's instrument. This replaces the pad's whole chain, effects included.
- Drop effects or samples on the chain panel to add them to the selected pad's chain. An empty pad's chain panel also has a **+** button for building a chain from scratch.

Plug-ins inside a pad's chain show one row of parameters fewer than at the top of a track's chain, so each value still fits under its name.

## Pad Channels

Each pad that holds a sound is a mixer channel nested under the Drum Grid's track. It appears as a row under the track in the arrangement, with no clip lane, and as a strip under the grid's strip in the [Mixer View](../mixer-view.md). A pad channel has volume, pan, mute, solo, sends, automation and a meter, like any track, and its device chain is the pad's chain: select the channel and the bottom panel shows the pad's chain, and devices you add while it is selected land there.

Empty pads have no channel. Clearing a pad removes its channel, and undo brings it back with its settings.

The pad panel in the device mixes the selected pad's channel:

- **Gain** — level, -60 to +6 dB
- **Pan** — stereo position
- **Mute pad**, **Solo pad** and **Pad power**

A pad channel's output goes to the grid by default: the pads are summed where the Drum Grid sits, before any effects that follow it on the track. Route a channel's output elsewhere and it leaves the grid's mix. Soloing a pad keeps the grid's track audible.

!!! note "Projects with pad buses"
    Earlier versions routed pads to bus tracks. Those projects open with each bus converted: a bus that carried one pad becomes that pad's channel, keeping its effects, sends, automation and output, and a bus that carried several pads becomes a group that their channels route into.

## Adding Samples

- Drag audio files from the Media Explorer onto a pad to load a sample. The pad's chain is replaced by a Sampler playing it, rooted on the pad's note. To add a sample to a pad's chain instead, drop it on the chain panel.

### Multi-Sample Drop

Select several samples in the [Media Explorer](../panels/browsers.md) or [Media Library](../panels/media-library.md) and drag them onto any pad. Samples fill consecutive pads in selection order, starting from the drop target. While dragging, every pad that will receive a sample lights up so you can see the range before releasing.

## Pad Envelope and Macros

Each pad's Sampler has an amplitude envelope you can shape per pad: **Attack**, **Decay**, **Sustain** and **Release**.

These envelope controls are full modulation targets. You can link them to the Drum Grid's [macros](../modulation/macros.md) and modulate them with LFOs or envelopes the same way as any other parameter (see [Linking Parameters](../modulation/linking.md)). Slicing a sample across several pads gives each slice its own envelope, so a single macro can, for example, tighten every slice's release at once.

## MIDI Mapping

The 64 pads run chromatically from C0 on the first pad, so the General MIDI drum notes land where drum maps expect them: kick on C1, snare on D1, and so on.
