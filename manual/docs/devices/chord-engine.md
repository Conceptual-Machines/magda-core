# Chord Engine

The Chord Engine is MAGDA's built-in chord analysis, suggestion, and generation device. It detects chords from incoming MIDI, identifies the musical key, and suggests what chord to play next — all in real time.

## Overview

The Chord Engine is a MIDI device that lives on an instrument track's FX chain. Its suggestions tie straight into note editing: you can drag chords onto the Piano Roll, where they land as notes with a chord annotation, and the same panel docks beside the Piano Roll and the chord track's editor (docked, it has no keyboard). This tight integration between harmonic analysis and note editing is central to the workflow.

It analyses incoming MIDI notes and provides:

- **Real-time chord detection** — identifies the chord you're playing
- **Key and scale detection** — determines the tonal centre from your playing
- **Context-aware chord suggestions** — recommends chords based on music theory and voice leading
- **AI-generated progressions** — uses a language model to compose chord progressions in a given style
- **Drag-and-drop to Piano Roll** — drop suggestions directly onto the timeline as MIDI notes with chord annotations

The suggestions column has two tabs, chosen with the icons at its top: **K&S** (Krumhansl-Schmuckler analysis and suggestions) and **AI** (LLM-generated progressions).

## K&S Tab

<div class="magda-ui mq mq-device" markdown="span">
<span class="mq-main">
<span class="mq-head"><span class="mq-name">Chord Engine</span><span class="mq-led"></span></span>
<span class="mq-ce">
<span class="mq-col">
<span class="mq-sec">[<span class="mq-cap">Chord</span>](#chord-detection)<span class="mq-btn">C</span></span>
<span class="mq-chordbox"><span class="mq-chordtitle"><span class="mq-chordname">C4 maj</span><span class="mq-badge">I</span><span class="mq-voicing">C E G</span></span><span class="mq-keys"><span class="on"></span><span></span><span class="on"></span><span></span><span class="on"></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span><i style="left:4.9%"></i><i style="left:12.0%"></i><i style="left:26.3%"></i><i style="left:33.4%"></i><i style="left:40.6%"></i><i style="left:54.9%"></i><i style="left:62.0%"></i><i style="left:76.3%"></i><i style="left:83.4%"></i><i style="left:90.6%"></i></span></span>
<span class="mq-status">MIDI in &middot; play a chord or pick one</span>
<span class="mq-sec">[<span class="mq-cap">History</span>](#chord-detection):general-delete:</span>
<span class="mq-hist"><span class="mq-chip">G3 maj<small>V</small></span>&rarr;<span class="mq-chip">C4 maj<small>I</small></span></span>
</span>
<span class="mq-col">
<span class="mq-tabs">[<span class="mq-tab on">:ai-psychology:</span>](#ks-tab)[<span class="mq-tab">:ai-ai-sound-designer:</span>](#ai-tab)<span class="mq-sub">after C4 maj</span><span class="mq-legend mq-sub"><span class="mq-fn mq-t">tonic</span><span class="mq-fn mq-p">pre-dominant</span><span class="mq-fn mq-d">dominant</span></span></span>
<span class="mq-group">[<span class="mq-cap">Diatonic</span>](#suggestion-generation) <span class="mq-sub">in key</span></span>
<span class="mq-tiles"><span class="mq-tile mq-p">F maj7<small>IV7</small></span><span class="mq-tile mq-d">G maj7<small>V7</small></span><span class="mq-tile mq-t">A min7<small>vi7</small></span><span class="mq-tile mq-p">D min<small>ii</small></span><span class="mq-tile mq-t">E min<small>iii</small></span><span class="mq-tile mq-p">F maj<small>IV</small></span><span class="mq-tile mq-d">G maj<small>V</small></span><span class="mq-tile mq-d">B dim<small>vii</small></span></span>
<span class="mq-group">[<span class="mq-cap">Beyond the key</span>](#suggestion-generation) <span class="mq-sub">secondary, borrowed &amp; substitutes</span></span>
<span class="mq-tiles"><span class="mq-tile mq-d">A 7<small>V/x</small></span><span class="mq-tile mq-d">B 7<small>V/x</small></span><span class="mq-tile mq-d">C 7<small>V/x</small></span><span class="mq-tile mq-d">D 7<small>V/x</small></span></span>
</span>
<span class="mq-col">
<span class="mq-sec">[<span class="mq-cap">Key</span>](#key-detection)</span>
<span class="mq-keyrow"><span class="mq-keybox">C major</span><span class="mq-iconbtn">:browser-browser:</span></span>
<span class="mq-sec" style="margin-top:0.8em">[<span class="mq-cap">Scales</span>](#scale-detection)</span>
<span class="mq-scale">C Major (Ionian)</span>
<span class="mq-scale">C Mixolydian</span>
<span class="mq-scale">C Lydian</span>
<span class="mq-scale">G Major (Ionian)</span>
</span>
</span>
<span class="mq-foot">[<span class="mq-cap">Novelty</span>](#footer-controls)<span class="mq-sub">safe</span><span class="mq-slider"></span><span class="mq-sub">wild</span><span class="mq-mono">30%</span><span class="mq-vsep"></span>[<span class="mq-cap">Extensions</span>](#footer-controls)<span class="mq-seg"><span class="on">7th</span><span>9th</span><span>11th</span><span>13th</span><span>Alt</span></span>[<span class="mq-filter">:general-funnel:</span>](#footer-controls){ .mq-push }</span>
</span>
<span class="mq-side"><span class="mq-power">:general-power:</span><span class="mq-noteview"></span></span>
</div>

The K&S tab uses the Krumhansl-Schmuckler algorithm to detect the key from your playing and generate chord suggestions from music theory rules. The interface has three columns:

| Column | Content |
|--------|---------|
| **Chord** (left) | The chord you are playing, with its degree in the detected key and a keyboard lighting its voicing, then the **History** of recent chords |
| **Suggestions** (centre) | Suggestion tiles in two groups, **Diatonic** (in key) and **Beyond the key** (secondary, borrowed and substitute chords). Each tile shows its Roman numeral degree and a dot for its harmonic function: tonic, pre-dominant or dominant. A secondary dominant tile shows **V/x** in place of the degree. |
| **Key / Scales** (right) | Detected key, a button to browse scales, and the matching scales |

Press any chord tile, in the suggestions or the history, to hear it through the track's instrument; drag it onto the Piano Roll to place it (see [Placing Chords](#placing-chords)). The **C** button in the Chord header switches note names between letters (C), solfège (Do) and both (C/Do).

### Chord Detection

Play notes on the track and the panel displays the detected chord name in real time. Detection uses pitch-class set matching against a library of known chord shapes (triads, sevenths, ninths, elevenths, thirteenths, augmented, diminished, suspended, and slash voicings).

Recent chords appear in the **History** section as a chain of chips, ending on the chord just played. The bin button beside it clears the history and resets detection.

### Key Detection

The detected key (e.g. "D# minor") is shown at the top of the right column. It uses the **Krumhansl-Schmuckler key-finding algorithm**:

1. A **pitch-class histogram** accumulates every note you play, weighted by an exponential time decay (recent notes matter more; decay constant is 5 seconds).
2. The histogram is correlated against **Krumhansl major and minor profiles** — empirically derived distributions of pitch-class frequency in tonal music.
3. All 24 possible keys (12 roots x major/minor) are scored. A small bias toward the third of each candidate helps disambiguate major vs minor.
4. The highest-scoring key wins.

### Scale Detection

Below the key, the panel lists the top matching scales (Ionian, Aeolian, Dorian, Mixolydian, Pentatonic, etc.), ranked by how well your pitch content matches each scale's note set.

- **Click** a scale to see all diatonic chords built from it
- **Shift-click** a scale to select it for filtering (it highlights). Multiple scales can be selected — their pitch classes are merged to filter suggestions

The browse button beside the key swaps the suggestions for a list of scales, with root-note buttons to narrow it to one key; the back arrow returns to the suggestions.

### Suggestion Generation

Suggestions are generated from:

- **Diatonic candidates** — chords built from scale degrees I–VII, with optional extensions (7ths, 9ths, 11ths, 13ths)
- **Non-diatonic candidates** — modal borrowings, secondary dominants, and altered chords
- **Voice leading** — suggestions are voiced to minimise movement from the last chord played

Recently played chords are filtered out so suggestions always point forward.

### Footer Controls

| Control | Description |
|---------|-------------|
| **Novelty** | Slider from *safe* to *wild*: the balance between diatonic (0%) and non-diatonic (100%) suggestions |
| **Extensions** | Toggles for the extensions the suggestions may use: **7th**, **9th**, **11th** and **13th**. **Alt** adds borrowed chords and tritone substitutes. |
| **Scale filter** | The funnel button: filter suggestions by the detected scales |

## AI Tab

In AI mode, a language model generates full chord progressions based on a text description. Type a prompt describing the style, mood, or harmonic flavour you want, and the AI returns a sequence of chords.

### Using AI Suggestions

1. Type a description in the prompt at the foot of the panel (*Describe a chord progression...*). This can be a qualitative mood description (e.g. "soulful and jazzy", "dark ambient") or a functional one using chord grades (e.g. "I-iv-V7 progression", "ii-V-I turnaround")
2. Press **Enter** or click the send button
3. The AI returns one or more labelled progressions as rows of chord buttons
4. **Press** a chord to audition it
5. **Drag** a chord onto the Piano Roll to place it as MIDI notes

The model in use is named beside the tabs. Each progression row also has a copy button (*Click to copy chords, drag to timeline*) for taking the whole progression at once.

AI progressions are labelled with a short description (e.g. "Dim pull with warm iv and V7", "Backdoor cadence into Dim").

### AI with Key Context

The AI receives the currently detected key as context, so its suggestions are harmonically relevant to what you've been playing.

## Suggestion Pipeline

The K&S tab's suggestion engine uses the following pipeline:

```mermaid
flowchart TD
    A["MIDI Note Input"] --> B["Pitch-Class Histogram<br>(time-decayed, τ = 5s)"]

    B --> C["Krumhansl-Schmuckler<br>Key-Profile Correlation<br>12 rotations × major/minor/modal"]

    C --> D["Detected Key + Mode"]

    D --> E["Diatonic Candidates<br>Scale degrees I–VII<br>+ 7ths / 9ths / 11ths / 13ths"]
    D --> F["Non-Diatonic Candidates<br>Modal borrowings<br>Secondary dominants<br>Alterations / Slash chords"]

    E --> G["Mix by Novelty<br>0.0 = diatonic, 1.0 = chromatic"]
    F --> G

    G --> H["Filter Recently Played Chords"]

    H --> I["Voice-Leading Optimizer<br>Minimize centroid distance<br>from last played chord"]

    I --> J["Top-K Suggestions"]
```

## Chord Timeline

<div class="magda-ui mq mq-roll" markdown="span">
<span class="mq-rail">[<span class="mq-railbtn on">:midi_editor-v1-me-chord-track:</span>](#chord-timeline)<span class="mq-railbtn">:midi_editor-v1-me-monitor:</span><span class="mq-railbtn">:midi_editor-icon-fold-boldm:</span></span>
<span class="mq-rollbody">
<span class="mq-rh">[<span class="mq-cap">Chords</span>](#chord-timeline)[:general-refresh:](#detecting-chords)</span>
<span class="mq-lane">[<span class="mq-cb" style="left:0.00%;width:calc(25.00% - 3px)">C4 maj<small>I &bull;&bull;&bull;</small></span>](#note-tracking)[<span class="mq-cb" style="left:25.00%;width:calc(25.00% - 3px)">A3 min<small>vi &bull;&bull;&bull;</small></span>](#note-tracking)[<span class="mq-cb" style="left:50.00%;width:calc(25.00% - 3px)">F3 maj<small>IV &bull;&bull;&bull;</small></span>](#note-tracking)[<span class="mq-cb" style="left:75.00%;width:calc(25.00% - 3px)">G3 maj<small>V &bull;&bull;&bull;</small></span>](#note-tracking)</span>
<span class="mq-rh"></span>
<span class="mq-ruler"><span class="bar" style="left:0.00%">1</span><span style="left:12.50%">1.3</span><span class="bar" style="left:25.00%">2</span><span style="left:37.50%">2.3</span><span class="bar" style="left:50.00%">3</span><span style="left:62.50%">3.3</span><span class="bar" style="left:75.00%">4</span><span style="left:87.50%">4.3</span></span>
<span class="mq-pkeys" style="--rows:18"><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w">C4</span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="w"></span></span>
<span class="mq-grid" style="--rows:18"><span class="mq-rows"><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="b"></span><span class="w"></span><span class="w"></span></span>[<span class="mq-pn" style="left:0.00%;width:calc(25.00% - 2px);top:calc(9 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:0.00%;width:calc(25.00% - 2px);top:calc(5 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:0.00%;width:calc(25.00% - 2px);top:calc(2 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:25.00%;width:calc(25.00% - 2px);top:calc(12 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:25.00%;width:calc(25.00% - 2px);top:calc(9 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:25.00%;width:calc(25.00% - 2px);top:calc(5 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:50.00%;width:calc(25.00% - 2px);top:calc(16 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:50.00%;width:calc(25.00% - 2px);top:calc(12 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:50.00%;width:calc(25.00% - 2px);top:calc(9 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:75.00%;width:calc(25.00% - 2px);top:calc(14 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:75.00%;width:calc(25.00% - 2px);top:calc(10 * var(--row))"></span>](#placing-chords)[<span class="mq-pn" style="left:75.00%;width:calc(25.00% - 2px);top:calc(7 * var(--row))"></span>](#placing-chords)</span>
</span>
</div>

When a Chord Engine is present on a track, the Piano Roll shows a **chord row** above the note grid. This row displays chord annotations that are linked to their MIDI notes. The **Chord track** button in the Piano Roll's rail shows or hides it, and the clip remembers your choice.

### Placing Chords

Drag a chord from the suggestions grid and drop it onto the Piano Roll:

1. **Drop** sets the start position — a blinking indicator appears
2. **Move the mouse** to preview the chord length
3. **Click** to confirm the length, or press **Enter** to use the default length
4. Press **Escape** to cancel

The dropped chord creates both MIDI notes and a chord annotation in the timeline. These are linked — moving, resizing, or deleting the notes automatically updates the annotation.

### Detecting Chords

Click the refresh button in the chord row (*Recalculate chords from notes*) to auto-detect chords from existing notes. The detection scans notes at each bar boundary, identifies chords with 2+ simultaneous notes, and creates linked annotations.

### Note Tracking

Chord annotations track their associated MIDI notes in real time:

- **Move notes** — the annotation follows
- **Resize notes** — the annotation length updates
- **Delete notes** — the annotation shrinks or disappears
- **Transpose notes** — the chord name re-detects automatically
- **Undo/redo** — annotations revert along with their notes
