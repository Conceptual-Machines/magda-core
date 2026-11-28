# Mixer View

The Mixer View provides a channel-strip interface for balancing levels, panning, routing, and applying effects. Switch to it by clicking **Mix** in the footer bar.

## Layout

<div class="magda-ui mv mv-mix" markdown="span">
<span class="mv-rail">[:trackchain-icon-sends-boldm::trackchain-oscilloscope3::trackchain-icon-spectrum-boldm:<span class="mv-on">:trackchain-icon-inserts-boldm:</span>](#toggle-rail){ title="Toggle rail" }<span class="mv-railgap"></span>[:trackchain-icon-checkmix-boldm:](#mix-analysis){ title="Analyze" }<span class="mv-railgap"></span>[:toolbar-track_width_narrow::toolbar-track_width_medium::toolbar-track_height_large::general-record-monitor:<span class="mv-on">:general-input-output:</span>](#toggle-rail){ title="Toggle rail" }</span>
<span class="mv-strip mv-drums mv-sel">[:general-chevron_right:Drums](#track-name-and-color){ .mv-head }[<span>Drum Grid</span><span>Compressor</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:64%"></span><span class="mv-thumb" style="--y:30%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-2.1](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[C](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-bass">[Bass](#track-name-and-color){ .mv-head }[<span>FM0</span><span>Saturator</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:44%"></span><span class="mv-thumb" style="--y:34%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-7.2](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[C](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-keys">[Keys](#track-name-and-color){ .mv-head }[<span>Poly Synth</span><span>Chorus</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:40%;--r:27%"></span><span class="mv-thumb" style="--y:40.7%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-11.2](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[20L](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-pluck">[Pluck](#track-name-and-color){ .mv-head }[<span>Marimba</span><span>Delay</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:6%;--r:12%"></span><span class="mv-thumb" style="--y:43%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-21.2](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[35R](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-pad">[Pad](#track-name-and-color){ .mv-head }[<span>Halo</span><span>Filter</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:14%;--r:16%"></span><span class="mv-thumb" style="--y:45.6%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-13.8](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[9L](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-arp">[Arp](#track-name-and-color){ .mv-head }[<span>Arpeggiator</span><span>Poly Synth</span><span>Phaser</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:21%;--r:30%"></span><span class="mv-thumb" style="--y:45.6%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-12.0](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[25R](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-lead">[Lead](#track-name-and-color){ .mv-head }[<span>Poly Synth</span><span>Grain Delay</span><span>EQ</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:37%;--r:40%"></span><span class="mv-thumb" style="--y:36%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-11.2](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[5R](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-mixgap"></span>
<span class="mv-strip mv-aux">[Reverb](#track-name-and-color){ .mv-head }[<span>Reverb</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:11%;--r:12%"></span><span class="mv-thumb" style="--y:36%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-23.9](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[C](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-aux">[Delay](#track-name-and-color){ .mv-head }[<span>Delay</span>](#mini-fx-chain){ .mv-fx }[<span class="mv-meters" style="--m:13%;--r:14%"></span><span class="mv-thumb" style="--y:36%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#volume-fader){ .mv-fader }[-22.7](#level-meters){ .mv-peak }[<span>Al..</span><span>N..</span><span>N..</span><span>M..</span>](#io-routing){ .mv-io }[C](#pan){ .mv-pan }[:general-master_on::general-solo:](#mute-solo-record){ .mv-ms }</span>
<span class="mv-strip mv-master">[Master](#master-channel-strip){ .mv-head }[<span class="mv-meters" style="--m:72%;--r:70%"></span><span class="mv-thumb" style="--y:24.7%"></span><span class="mv-scale"><i style="--y:0%">6</i><i style="--y:12.8%">3</i><i class="mv-zero" style="--y:24.7%">0</i><i style="--y:35.5%">3</i><i style="--y:45.4%">6</i><i style="--y:61.8%">12</i><i style="--y:74.4%">18</i><i style="--y:84.3%">24</i><i style="--y:95.7%">36</i><i style="--y:100%">48</i></span>](#master-channel-strip){ .mv-fader }[0.4](#level-meters){ .mv-peak }[<span>Out 1 + Out 2</span>](#io-routing){ .mv-out }[:general-master_on:](#master-channel-strip){ .mv-ms }</span>
</div>

- **Channel strips** — One per track, scrollable horizontally
- **Chord channel strip** — The [chord track](chord-track.md), always the first strip on the left
- **Aux channel strips** — Auxiliary/bus tracks, in a fixed section next to the master with a small gap before it
- **Master channel strip** — Final output, fixed on the right
- **Resizable width** — Drag the edge of a channel strip to resize

Click a channel strip to select it. Drag plugins onto a channel strip to add them to the track's FX chain. Drag a strip by its name to move it: a line shows where it will land between strips, including inside or out of a group, and an outline shows a group it will join.

## Toggle Rail

A vertical rail of toggles runs down the left edge of the mixer. Each toggle shows or hides a row across every channel strip, so you can pare the mixer back to just the controls you need. The toggles at the top of the rail are:

- **Sends** — the send section
- **Oscilloscope** — a mini oscilloscope per strip
- **Spectrum** — a mini spectrum analyzer per strip
- **FX Chain** — the strip's mini FX chain

The **Analyze** button is in the middle of the rail (see [Mix Analysis](#mix-analysis)). The bottom of the rail has, from the top:

- **Narrow**, **Medium** and **Wide track width** — set every channel strip to that width. Right-click one of them to edit the three widths
- **Monitor** — the record-arm and input-monitoring row
- **I/O** — input and output routing selectors

The rail's on/off state is remembered between sessions.

## Channel Strip Controls

Each channel strip provides:

### Volume Fader

Drag the fader to adjust the track's output level. The fader sits over the track's peak meter, with the current level shown as a dB readout below the thumb.

### Level Meters

The peak meter sits directly behind the fader and shows the real-time signal level. A peak value displays the highest level reached — watch for clipping. Click the peak readout to reset it.

### Pan

The pan control positions the track in the stereo field, from hard left to hard right.

### Mute, Solo, Record

- **Mute** (speaker) — Silence the track output
- **Solo** (ring) — Solo this track, muting all others
- **Record** (dot) — Arm the track for recording
- **Monitor** — Enable input monitoring

### Track Name and Color

The track name is in the head at the top of each strip. The head shows the track color as a line across its top, or fills with the color when **Track colour** is set to **Full bar** in **Settings > Appearance**. A color bar also runs along the bottom of the strip.

### Mini FX Chain

With the **FX Chain** toggle on, each strip shows a compact version of the track's FX chain, one row per device (a rack shows as one row with its name). Click a row's dot to bypass the device. Click its chevron to unfold up to three of its parameters as faders, so you can tweak an effect without leaving the mixer. Choose exactly which parameters appear per device from the **Mini FX** column in the [Configure Parameters dialog](plugin-parameters.md) — if none are chosen, the row falls back to the device's first visible parameters. Third-party plugins also show a button to open their own window; native MAGDA devices do not. The same rows appear in the FX section of the track and master [inspectors](panels/inspector.md).

### Mini Analyzers

With the **Oscilloscope** or **Spectrum** toggle on, a mini [analyzer](devices/analysis.md) appears on each strip that carries the matching device. Click the expand chevron to unfold its controls beneath the trace, or the pop-out button to open the full analyzer in its own floating window.

## Master Channel Strip

The master strip controls the final output level of the mix. It has its own volume fader and meters, and a speaker button that mutes the master. It has no pan control and no solo or record buttons. With the **I/O** toggle on, an output selector above the speaker button sets the hardware outputs the master plays through (for example *Out 1 + Out 2*).

## Mix Analysis

The **Analyze** button in the middle of the toggle rail runs a measured analysis of the mix and shows the findings in a small panel anchored to the button.

- **Scope** — with no channels selected, the whole mix is analysed; select channel strips first to scope the measurement to just those tracks. The panel title shows the scope and the range (the loop region if one is active, otherwise the whole song).
- **Findings** — a per-track table of measured values (integrated loudness, loudness range, peak, peak-to-loudness ratio, stereo correlation and width, with a row for the master) followed by any detected **collisions**: frequency ranges where two tracks mask each other, with a severity rating.
- **Re-run** the analysis after changes, or **Stop** a run in progress.

The same measurements feed the [mixing agent](panels/ai-assistant.md#mixing-agent): once an analysis exists, the AI console in the mixer view picks it up as context, so you can ask for mix feedback grounded in real numbers rather than guesswork.

## I/O Routing

Each channel strip has routing selectors for:

- **Audio input** — Select which physical input or bus feeds the track
- **Audio output** — Choose where the track's audio is sent (master, aux bus, etc.)
- **MIDI input** — Select the MIDI input device and channel
- **MIDI output** — Route MIDI to a specific device or virtual instrument

## Sends

Each channel strip has a send section for routing audio to auxiliary buses. Show or hide it with the **Sends** toggle in the rail.

- **Add a send** — Create a new send slot to route signal to an aux track
- **Send level** — Adjust how much signal is sent to the aux bus
- **Remove a send** — Click the remove button on a send slot to delete it

The send section sizes itself to the number of sends across your tracks, so every strip stays aligned.

## Multi-Output Plugins

When an instrument has multiple output pairs activated, each output appears as its own channel strip in the mixer — just like any other track. You can route these strips to groups or aux sends independently. See [Multi-Output Plugins](tracks.md#multi-output-plugins) for setup details.

The device's multi-out menu lets you toggle several pairs in one go: the menu stays open as you tick pairs, and **Activate All** / **Deactivate All** bring every output in or out with one click.

## Drum Grid Pad Channels

Every [Drum Grid](devices/drum-grid.md) pad that holds a sound has its own mixer channel, nested under the strip of the grid's track. Click the chevron on the track's strip to fold the pad channels away or show them again. Empty pads have no channel.

A pad channel works like any other channel: volume, pan, mute, solo, sends, automation, effects, and a meter. Its output goes to the Drum Grid by default, so the pads are summed into the grid before any effects that come after the grid on its track. Route a pad's output somewhere else and it leaves the grid's mix.

- **Solo** — soloing a pad channel keeps the grid's track audible, so you hear the pad through the rest of the track
- **Chain** — select a pad channel to show that pad's chain in the bottom panel. Devices you add to it land in the pad's chain
- **Pad mix** — the pad's level, pan, mute, and solo on the Drum Grid device and its pad channel are the same controls

Projects saved with pad bus outputs are converted when you open them: a bus that carried one pad becomes that pad's channel, keeping its effects, sends, automation, and output, and a bus that carried several pads becomes a group that their channels route into.
