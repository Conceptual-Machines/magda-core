# Poly Sequencer

The Poly Sequencer is a polyphonic step-sequencer MIDI device. Where the [Step Sequencer](step-sequencer.md) plays one note per step, the Poly Sequencer can place a full chord — up to eight notes — on each step, and it switches between a melodic keyboard grid and a drum-lane grid.

<div class="magda-ui mq mq-device mq-seq" markdown="span">
<span class="mq-main">
<span class="mq-head"><span class="mq-hicons">:knob-knob::trackchain-icon-mods-boldm:[:ai-ai-sound-designer:](#ai-pattern-generation)[:general-random:](#header-buttons)[<span class="mq-rec">:transport-record_circle:</span>](#step-recording)</span><span class="mq-vsep"></span>[<span class="mq-thru">:general-midi_thru:</span>](#header-buttons)<span class="mq-vsep"></span><span class="mq-presets">Presets :general-chevron_down:</span>:general-save:<span class="mq-vsep"></span><span class="mq-name">Poly Sequencer</span><span class="mq-led"></span>[:general-copy:](#header-buttons)</span>
<span class="mq-seqbody">
<span class="mq-sbar">[<span class="mq-railbtn on">:trackchain-device-time-bend:</span>](#time-bend)</span>
<span class="mq-steps">
<span class="mq-settings">[Rate](#settings)<span class="mq-box">1/16</span>Steps<span class="mq-box">32</span>Gate<span class="mq-box">95%</span>Dir<span class="mq-seg mq-caps"><span class="on">FWD</span><span>REV</span><span>P-P</span><span>RND</span></span>View<span class="mq-seg mq-caps"><span class="on">KEYS</span><span>DRUMS</span></span></span>
<span class="mq-sgrid" style="--n:32"><span class="mq-sruler">[<span class="mq-rl">:panels-enter_fs:</span>](#pattern-views)<span>1</span><span></span><span></span><span></span><span>5</span><span></span><span></span><span></span><span>9</span><span></span><span></span><span></span><span>13</span><span></span><span></span><span></span><span>17</span><span></span><span></span><span></span><span>21</span><span></span><span class="play">23</span><span></span><span>25</span><span></span><span></span><span></span><span>29</span><span></span><span></span><span></span></span>[Notes](#pattern-views){ .mq-slab }<span class="mq-cell mq-nbox hi">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox hi">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox hi">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox play">4</span><span class="mq-cell mq-nbox">4</span><span class="mq-cell mq-nbox hi">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span><span class="mq-cell mq-nbox">G</span>[Gate](#per-step-lanes){ .mq-slab }<span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span><span class="mq-cell gt"></span>[Tie](#per-step-lanes){ .mq-slab }<span class="mq-cell"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span><span class="mq-cell ti"></span>[Velocity](#per-step-lanes){ .mq-slab }<span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span><span class="mq-cell ve"></span>[Prob](#per-step-lanes){ .mq-slab }<span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span><span class="mq-cell pr"></span></span>
</span>
<span class="mq-bend"><span class="mq-brow"><span class="mq-bpow">:general-power:</span>[<span class="mq-bbtn">SHAPE</span>](#time-bend)<span class="mq-blab">SPAN</span><span class="mq-bbtn on">1</span><span class="mq-bbtn">2</span><span class="mq-bbtn">4</span><span class="mq-bbtn">1/16</span><span class="mq-bbtn on">SNAP</span></span><span class="mq-brow">[<span class="mq-bbtn on">TIME</span>](#time-bend)<span class="mq-bbtn">DYN</span>[<span class="mq-bbtn">GROOVE</span>](#time-bend)<span class="mq-blab">AMT</span><span class="mq-box">100%</span><span class="mq-blab">CYC</span><span class="mq-box">1x</span>[:panels-enter_fs:](#time-bend)</span><svg class="mq-curve" viewBox="0 0 200 110" aria-hidden="true"><path class="mq-cgrid" d="M0 10H200M0 30H200M0 50H200M0 70H200M0 90H200M25 0V110M50 0V110M75 0V110M100 0V110M125 0V110M150 0V110M175 0V110"/><path class="mq-cplay" d="M78 0V110"/><path class="mq-cfill" d="M6 104L194 6V110H6Z"/><path class="mq-cline" d="M6 104L194 6"/><circle class="mq-cmid" cx="100" cy="55" r="3.5"/><circle class="mq-cend" cx="6" cy="104" r="3.5"/><circle class="mq-cend" cx="194" cy="6" r="3.5"/></svg></span>
</span>
</span>
<span class="mq-side"><span class="mq-power">:general-power:</span><span class="mq-noteview"></span><span class="mq-delta">&Delta;</span></span>
</div>

The faceplate has the steps on the left and the [time bend](#time-bend) on the right. The sidebar's **Show the time bend** toggle hides or shows the time bend; the choice is saved with the device.

## Settings

A strip across the top of the steps holds the pattern's settings:

| Setting | Range | Description |
|--------|-------|-------------|
| **Rate** | 1/4. to 1/32, with dotted (.) and triplet (T) values | How fast the sequencer advances |
| **Steps** | 1–32 | Number of steps in the pattern |
| **Gate** | 5–100% | Default note length as a percentage of the step |
| **Dir** | FWD, REV, P-P, RND | Playback direction: forward, reverse, ping-pong or random |
| **View** | KEYS, DRUMS | Switches the grid between the keyboard and drum lanes |

Right-click the strip or the step ruler for **Quantize** and **Quantize Sub**; see [Time Bend](#time-bend) below.

The device header holds **AI**, **Randomize pattern**, **Step record**, **MIDI thru** (passes incoming notes through to the downstream instrument), and **Copy** (*Click to copy pattern, drag to timeline*), the same buttons as on the [Step Sequencer](step-sequencer.md#header-buttons).

## Pattern Views

Under the settings strip, a ruler numbers the steps and lights the one playing. Below it sits the grid, in one of two views. When the device is too short for the grid, as at the device chain's standard height, a row of step boxes stands in for it. Each box names the step's note, its chord (such as `Am7`) or its note count; click a box to toggle notes on that step from a pop-up keyboard, or from a list of drums in the Drums view.

The expand button at the start of the ruler (*Expand into the arrangement*) lends the grid to the arrangement, where it has room to work, while the step boxes stay in the device. Only one faceplate can be out at a time. Its collapse button, or ++esc++, puts it back.

### Keys view

A piano-roll grid: rows are pitches (with a keyboard on the left), columns are steps. Click a cell to add or remove a note at that pitch and step — stack several in one column to build a chord. Notes draw as bars that run on through the steps tied to them. The up/down buttons beside the keyboard shift the rows by an octave, the +/− buttons zoom between one and four octaves, and the mouse wheel scrolls or zooms.

### Drums view

An x0x-style lane grid, one lane per drum sound. If a [Drum Grid](drum-grid.md) device sits downstream on the same track, the Poly Sequencer reads its pads and names the lanes to match; otherwise it falls back to a General MIDI drum map (Kick, Snare, CHat, OHat, LTom, MTom, Crash, Ride). Notes that don't match a lane appear as extra lanes you can still edit.

Right-click any step in either view for **Mute Step** (or **Unmute Step**), **Clear Step**, and a **Pattern** submenu (shift by a semitone or octave, clear the pattern).

## Per-Step Lanes

Below the grid, four lanes set values for every step:

- **Gate** — toggle the step's gate on or off.
- **Tie** — tie the step to the previous one so the note sustains without retriggering.
- **Velocity** — drag to set step velocity.
- **Prob** — drag to set the probability that the step fires, for humanised or evolving patterns.

## Time Bend

The [Time Bend](../time-bend.md) column reshapes step timing across the pattern, the same idea as in the [Step Sequencer](step-sequencer.md#time-bend) and [Arpeggiator](arpeggiator.md). Drag the curve's points and bend its segments, right-click a point for a hard corner, or start from a **SHAPE**. **SPAN** (**1**, **2** or **4** bars) sets how long one pass of the curve lasts. The grid box (for example **1/16**) sets the grid division, and **SNAP** snaps points to the grid. **GROOVE** loads a groove's curve; **AMOUNT** sets how much of the bend is heard and **CYCLES** repeats it within the pattern. The **TIME | DYNAMICS** switch draws a level per step instead, which scales each step's velocity as it plays; the velocities in the pattern are left as they are. On the faceplate, DYNAMICS, AMOUNT and CYCLES show as **DYN**, **AMT** and **CYC**. See [Time Bend](../time-bend.md) for the editor itself.

**Quantize** (0–100%, in the right-click menu) pulls the bent steps back toward a grid of **Quantize Sub** divisions (16–512) of the bend's cycle. The time bend has its own expand button, which moves it into the arrangement.

## AI Pattern Generation

Describe the pattern you want — "4-on-the-floor kick", "lo-fi seventh chords", "hi-hat rolls" — in the device's [AI panel](../panels/ai-assistant.md#per-device-ai-panel) and let its agent fill the grid. The agent reads the current view and, in the Drums view, the downstream drum lanes, so generated drum hits land on the right pads. Fields you leave alone in the request are preserved, so you can regenerate just the notes without losing your rate or step count.
