# Waveform Editor

<div class="magda-ui me-editor me-wave" markdown="span">
<span class="me-toolbar">
[Drum Loop](#toolbar){ .me-chip .me-name }
<span class="me-gap"></span>
<span class="me-tools">[:midi_editor-v1-me-pointer:](#editing-the-clip){ .me-btn .me-on title="Pointer" }[:midi_editor-v1-me-label:](#labels){ .me-btn title="Label" }[:midi_editor-v1-me-marker:](#warp-markers){ .me-btn title="Marker" }</span>
<span class="me-gap"></span>
<span>[:waveform-envelope:](#gain-envelope){ .me-btn title="Gain envelope" } [:midi_editor-v1-me-loop:](#toolbar){ .me-btn .me-green title="Loop" } [1/4](#toolbar){ .me-chip }<span class="me-chip me-purple">AUTO</span><span class="me-chip">SNAP</span> [:material-power:](#toolbar){ .me-btn .me-power title="Power" } [:midi_editor-v1-me-maximise:](#toolbar){ .me-btn title="Maximise" }</span>
</span>
<span class="me-rail">[:waveform-spectrogram:](#spectrogram){ .me-btn title="Spectrogram" }[:waveform-transients:](#transients){ .me-btn title="Transients" }<span class="me-gap"></span>[:panels-icon-panel-right-close-boldm:](#properties-panel){ .me-btn title="Properties panel" }</span>
<span class="me-ruler"><span><b>1</b></span><span>1.2</span><span>1.3</span><span>1.4</span><span><b>2</b></span><span>2.2</span><span>2.3</span><span>2.4</span></span>
<span class="me-audio">[<svg viewBox="0 0 400 200" preserveAspectRatio="none"><g stroke="#ffffff14" stroke-width="1" vector-effect="non-scaling-stroke"><path d="M50 0V200"/><path d="M100 0V200"/><path d="M150 0V200"/><path d="M200 0V200"/><path d="M250 0V200"/><path d="M300 0V200"/><path d="M350 0V200"/><path d="M0 100H400"/></g><path id="me-wave-ch" d="M0.0 48.9 1.2 13.8 2.5 13.2 3.8 22.9 5.0 22.7 6.2 27.2 7.5 31.9 8.8 31.7 10.0 36.1 11.2 36.2 12.5 39.1 13.8 40.3 15.0 40.5 16.2 40.6 17.5 43.2 18.8 43.7 20.0 43.7 21.2 43.9 22.5 45.0 23.8 45.7 25.0 45.5 26.2 32.0 27.5 32.6 28.8 38.6 30.0 41.5 31.2 43.3 32.5 44.3 33.8 44.7 35.0 46.3 36.2 46.5 37.5 47.0 38.8 47.5 40.0 47.7 41.2 48.2 42.5 48.3 43.8 48.4 45.0 48.2 46.2 48.4 47.5 48.6 48.8 48.5 50.0 48.6 51.2 15.1 52.5 13.2 53.8 17.1 55.0 23.5 56.2 23.2 57.5 25.8 58.8 25.5 60.0 28.4 61.2 32.5 62.5 30.5 63.8 35.9 65.0 35.8 66.2 35.6 67.5 38.9 68.8 38.6 70.0 40.8 71.2 39.8 72.5 40.3 73.8 41.5 75.0 41.4 76.2 25.1 77.5 27.2 78.8 31.7 80.0 34.8 81.2 37.7 82.5 38.5 83.8 39.9 85.0 42.3 86.2 43.0 87.5 44.8 88.8 44.5 90.0 45.1 91.2 45.2 92.5 45.8 93.8 46.7 95.0 46.9 96.2 46.9 97.5 47.6 98.8 47.4 100.0 47.8 101.2 14.9 102.5 20.4 103.8 18.5 105.0 27.4 106.2 29.8 107.5 31.7 108.8 31.9 110.0 37.5 111.2 37.8 112.5 39.1 113.8 39.6 115.0 41.0 116.2 42.0 117.5 44.0 118.8 44.5 120.0 45.2 121.2 45.0 122.5 45.4 123.8 46.7 125.0 47.0 126.2 33.3 127.5 37.2 128.8 39.4 130.0 41.5 131.2 43.9 132.5 45.5 133.8 45.8 135.0 46.6 136.2 46.9 137.5 47.1 138.8 47.6 140.0 47.9 141.2 48.1 142.5 48.2 143.8 48.6 145.0 48.3 146.2 48.4 147.5 48.4 148.8 48.5 150.0 48.7 151.2 16.0 152.5 21.4 153.8 19.1 155.0 25.9 156.2 27.8 157.5 28.5 158.8 30.4 160.0 30.9 161.2 33.8 162.5 35.3 163.8 35.7 165.0 37.0 166.2 36.9 167.5 39.1 168.8 37.0 170.0 38.8 171.2 40.9 172.5 41.3 173.8 41.7 175.0 42.2 176.2 24.3 177.5 23.3 178.8 26.5 180.0 32.6 181.2 35.1 182.5 38.6 183.8 40.1 185.0 40.7 186.2 42.1 187.5 41.8 188.8 44.0 190.0 44.9 191.2 44.0 192.5 45.2 193.8 46.1 195.0 46.0 196.2 46.9 197.5 46.6 198.8 46.4 200.0 46.8 201.2 7.5 202.5 15.9 203.8 19.1 205.0 24.5 206.2 23.4 207.5 28.1 208.8 29.4 210.0 34.1 211.2 36.4 212.5 35.8 213.8 36.9 215.0 38.8 216.2 40.1 217.5 41.2 218.8 42.3 220.0 44.1 221.2 44.2 222.5 45.0 223.8 45.9 225.0 46.3 226.2 28.7 227.5 33.8 228.8 35.9 230.0 38.2 231.2 41.9 232.5 42.6 233.8 44.0 235.0 45.1 236.2 46.5 237.5 47.1 238.8 47.5 240.0 47.8 241.2 48.0 242.5 47.9 243.8 47.9 245.0 48.1 246.2 48.4 247.5 48.3 248.8 48.3 250.0 48.7 251.2 9.2 252.5 10.1 253.8 14.6 255.0 17.8 256.2 22.6 257.5 27.0 258.8 24.8 260.0 29.6 261.2 28.6 262.5 29.5 263.8 33.8 265.0 35.1 266.2 34.1 267.5 36.2 268.8 39.1 270.0 40.1 271.2 40.8 272.5 39.4 273.8 40.5 275.0 42.7 276.2 22.9 277.5 26.8 278.8 32.3 280.0 36.4 281.2 37.9 282.5 40.9 283.8 42.5 285.0 41.4 286.2 43.1 287.5 44.1 288.8 44.1 290.0 45.4 291.2 45.3 292.5 45.8 293.8 46.8 295.0 47.0 296.2 47.1 297.5 47.4 298.8 47.3 300.0 47.7 301.2 13.4 302.5 21.1 303.8 18.9 305.0 27.0 306.2 29.5 307.5 31.6 308.8 32.6 310.0 36.8 311.2 36.8 312.5 39.7 313.8 40.9 315.0 42.0 316.2 44.0 317.5 44.0 318.8 45.0 320.0 45.8 321.2 45.3 322.5 46.5 323.8 46.5 325.0 46.6 326.2 23.7 327.5 30.4 328.8 33.5 330.0 36.6 331.2 38.5 332.5 42.4 333.8 42.8 335.0 44.6 336.2 45.5 337.5 45.6 338.8 46.5 340.0 46.9 341.2 47.2 342.5 47.4 343.8 47.9 345.0 48.0 346.2 48.2 347.5 48.3 348.8 48.3 350.0 48.5 351.2 14.5 352.5 17.6 353.8 16.0 355.0 20.4 356.2 21.2 357.5 22.9 358.8 29.2 360.0 28.9 361.2 28.3 362.5 30.3 363.8 35.1 365.0 36.2 366.2 35.8 367.5 38.2 368.8 38.4 370.0 39.8 371.2 38.6 372.5 39.0 373.8 39.5 375.0 42.0 376.2 20.7 377.5 25.5 378.8 32.1 380.0 31.0 381.2 33.3 382.5 38.2 383.8 37.4 385.0 40.5 386.2 41.4 387.5 41.3 388.8 42.5 390.0 44.4 391.2 44.5 392.5 44.9 393.8 45.6 395.0 46.1 396.2 46.3 397.5 46.1 398.8 47.1 400.0 46.8 400.0 53.2 398.8 52.9 397.5 53.9 396.2 53.7 395.0 53.9 393.8 54.4 392.5 55.1 391.2 55.5 390.0 55.6 388.8 57.5 387.5 58.7 386.2 58.6 385.0 59.5 383.8 62.6 382.5 61.8 381.2 66.7 380.0 69.0 378.8 67.9 377.5 74.5 376.2 79.3 375.0 58.0 373.8 60.5 372.5 61.0 371.2 61.4 370.0 60.2 368.8 61.6 367.5 61.8 366.2 64.2 365.0 63.8 363.8 64.9 362.5 69.7 361.2 71.7 360.0 71.1 358.8 70.8 357.5 77.1 356.2 78.8 355.0 79.6 353.8 84.0 352.5 82.4 351.2 85.5 350.0 51.5 348.8 51.7 347.5 51.7 346.2 51.8 345.0 52.0 343.8 52.1 342.5 52.6 341.2 52.8 340.0 53.1 338.8 53.5 337.5 54.4 336.2 54.5 335.0 55.4 333.8 57.2 332.5 57.6 331.2 61.5 330.0 63.4 328.8 66.5 327.5 69.6 326.2 76.3 325.0 53.4 323.8 53.5 322.5 53.5 321.2 54.7 320.0 54.2 318.8 55.0 317.5 56.0 316.2 56.0 315.0 58.0 313.8 59.1 312.5 60.3 311.2 63.2 310.0 63.2 308.8 67.4 307.5 68.4 306.2 70.5 305.0 73.0 303.8 81.1 302.5 78.9 301.2 86.6 300.0 52.3 298.8 52.7 297.5 52.6 296.2 52.9 295.0 53.0 293.8 53.2 292.5 54.2 291.2 54.7 290.0 54.6 288.8 55.9 287.5 55.9 286.2 56.9 285.0 58.6 283.8 57.5 282.5 59.1 281.2 62.1 280.0 63.6 278.8 67.7 277.5 73.2 276.2 77.1 275.0 57.3 273.8 59.5 272.5 60.6 271.2 59.2 270.0 59.9 268.8 60.9 267.5 63.8 266.2 65.9 265.0 64.9 263.8 66.2 262.5 70.5 261.2 71.4 260.0 70.4 258.8 75.2 257.5 73.0 256.2 77.4 255.0 82.2 253.8 85.4 252.5 89.9 251.2 90.8 250.0 51.3 248.8 51.7 247.5 51.7 246.2 51.6 245.0 51.9 243.8 52.1 242.5 52.1 241.2 52.0 240.0 52.2 238.8 52.5 237.5 52.9 236.2 53.5 235.0 54.9 233.8 56.0 232.5 57.4 231.2 58.1 230.0 61.8 228.8 64.1 227.5 66.2 226.2 71.3 225.0 53.7 223.8 54.1 222.5 55.0 221.2 55.8 220.0 55.9 218.8 57.7 217.5 58.8 216.2 59.9 215.0 61.2 213.8 63.1 212.5 64.2 211.2 63.6 210.0 65.9 208.8 70.6 207.5 71.9 206.2 76.6 205.0 75.5 203.8 80.9 202.5 84.1 201.2 92.5 200.0 53.2 198.8 53.6 197.5 53.4 196.2 53.1 195.0 54.0 193.8 53.9 192.5 54.8 191.2 56.0 190.0 55.1 188.8 56.0 187.5 58.2 186.2 57.9 185.0 59.3 183.8 59.9 182.5 61.4 181.2 64.9 180.0 67.4 178.8 73.5 177.5 76.7 176.2 75.7 175.0 57.8 173.8 58.3 172.5 58.7 171.2 59.1 170.0 61.2 168.8 63.0 167.5 60.9 166.2 63.1 165.0 63.0 163.8 64.3 162.5 64.7 161.2 66.2 160.0 69.1 158.8 69.6 157.5 71.5 156.2 72.2 155.0 74.1 153.8 80.9 152.5 78.6 151.2 84.0 150.0 51.3 148.8 51.5 147.5 51.6 146.2 51.6 145.0 51.7 143.8 51.4 142.5 51.8 141.2 51.9 140.0 52.1 138.8 52.4 137.5 52.9 136.2 53.1 135.0 53.4 133.8 54.2 132.5 54.5 131.2 56.1 130.0 58.5 128.8 60.6 127.5 62.8 126.2 66.7 125.0 53.0 123.8 53.3 122.5 54.6 121.2 55.0 120.0 54.8 118.8 55.5 117.5 56.0 116.2 58.0 115.0 59.0 113.8 60.4 112.5 60.9 111.2 62.2 110.0 62.5 108.8 68.1 107.5 68.3 106.2 70.2 105.0 72.6 103.8 81.5 102.5 79.6 101.2 85.1 100.0 52.2 98.8 52.6 97.5 52.4 96.2 53.1 95.0 53.1 93.8 53.3 92.5 54.2 91.2 54.8 90.0 54.9 88.8 55.5 87.5 55.2 86.2 57.0 85.0 57.7 83.8 60.1 82.5 61.5 81.2 62.3 80.0 65.2 78.8 68.3 77.5 72.8 76.2 74.9 75.0 58.6 73.8 58.5 72.5 59.7 71.2 60.2 70.0 59.2 68.8 61.4 67.5 61.1 66.2 64.4 65.0 64.2 63.8 64.1 62.5 69.5 61.2 67.5 60.0 71.6 58.8 74.5 57.5 74.2 56.2 76.8 55.0 76.5 53.8 82.9 52.5 86.8 51.2 84.9 50.0 51.4 48.8 51.5 47.5 51.4 46.2 51.6 45.0 51.8 43.8 51.6 42.5 51.7 41.2 51.8 40.0 52.3 38.8 52.5 37.5 53.0 36.2 53.5 35.0 53.7 33.8 55.3 32.5 55.7 31.2 56.7 30.0 58.5 28.8 61.4 27.5 67.4 26.2 68.0 25.0 54.5 23.8 54.3 22.5 55.0 21.2 56.1 20.0 56.3 18.8 56.3 17.5 56.8 16.2 59.4 15.0 59.5 13.8 59.7 12.5 60.9 11.2 63.8 10.0 63.9 8.8 68.3 7.5 68.1 6.2 72.8 5.0 77.3 3.8 77.1 2.5 86.8 1.2 86.2 0.0 51.1Z" fill="#d9a6dc"/><use href="#me-wave-ch" y="100"/></svg>](#editing-the-clip)<span class="me-file">drum_loop.wav</span>[<span style="--x:0.6;--c:#c8a24a">Intro</span><span style="--x:25.5;--c:#5a8fc9">Fill</span><span style="--x:50.4;--c:#57b06a">Break</span><span style="--x:75.3;--c:#c76b6b">Outro</span>](#labels){ .me-labels }</span>
<span class="me-scroll">[](#layout)</span>
<span class="me-props">
<span class="me-col">
[CLIP](#properties-panel){ .me-sec }
<span class="me-row"><span class="me-tog">WARP</span><span class="me-tog me-on">BEAT</span><span class="me-tog">REV</span>:general-favorite:</span>
[STRETCH](#properties-panel){ .me-sec }
<span class="me-kv"><span>Mode</span><span class="me-field me-menu">Signalsmith</span></span>
<span class="me-kv"><span>Speed</span><span class="me-field me-dimval">1.000 <small>x</small></span></span>
<span class="me-kv"><span>Beats</span><span class="me-field">16.00 <small>beats</small></span></span>
<span class="me-kv"><span>BPM</span><span class="me-field">174.0 <small>BPM</small></span></span>
<span class="me-kv"><span>Key</span><span class="me-field me-menu">--</span></span>
[TRANSIENTS](#transients){ .me-sec }
<span class="me-kv"><span>Sens</span><span class="me-field">0%</span></span>
[PITCH](#properties-panel){ .me-sec }
<span class="me-kv"><span>Semi</span><span class="me-field">0.00 <small>st</small></span><span class="me-tog">ANALOG</span></span>
</span>
<span class="me-col">
<span class="me-row">[FADES](#properties-panel){ .me-sec .me-fold }<small>auto-xfade</small><span class="me-switch"></span></span>
<span class="me-row"><small>In</small><span class="me-gap"></span><small>Out</small></span>
<span class="me-row"><span class="me-field">0.000 <small>s</small></span><span class="me-field">0.000 <small>s</small></span></span>
<span class="me-row me-icons">:inspector-fade_linear::inspector-fade_convex::inspector-fade_concave::inspector-fade_scurve:<span class="me-gap"></span>:inspector-fade_linear::inspector-fade_convex::inspector-fade_concave::inspector-fade_scurve:</span>
<span class="me-row me-icons">:inspector-fade_gain::inspector-fade_speedramp:<span class="me-gap"></span>:inspector-fade_gain::inspector-fade_speedramp:</span>
[MIX](#properties-panel){ .me-sec }
<span class="me-kv"><span>Vol</span><span class="me-field">0.0 <small>dB</small></span></span>
<span class="me-kv"><span>Gain</span><span class="me-field">0.0 <small>dB</small></span></span>
<span class="me-kv"><span>Pan</span><span class="me-field">C</span></span>
</span>
</span>
</div>

The Waveform Editor is the audio clip editor in the bottom panel. It opens when you select an audio clip and shows the clip's source file, where you trim and loop the clip, place warp markers and labels, and draw its gain envelope.

## Layout

- **Toolbar** across the top (see [Toolbar](#toolbar)).
- **Rail** down the left edge. Its buttons switch the [spectrogram](#spectrogram) and the [transient markers](#transients) on and off; the button at the bottom opens and closes the properties panel.
- **Ruler and waveform** in the middle. The waveform draws in the clip's colour, one lane per channel.
- **Properties panel** on the right (see [Properties Panel](#properties-panel)). Drag its left edge to resize it.
- **Scroll bar** along the bottom. Drag its thumb to scroll and its ends to zoom.

## Toolbar

- **Clip name** on the left.
- **Tools** in the centre. While the waveform is showing, they are **Pointer**, **Label** and **Marker** (see [Warp Markers](#warp-markers) and [Labels](#labels)). While the gain envelope is showing, they are **Line**, **Pen** and **Flat** (see [Gain Envelope](#gain-envelope)).
- **Maximise** expands the editor to fill the window; click again to restore.
- **Power** enables or disables the clip.
- **Grid** - the grid division (click it to pick one), **AUTO** to follow the zoom level, and **SNAP** to snap to the grid.
- **Loop** turns the clip's loop on or off. On a free clip it puts the clip into beat mode first (see [Looping](../clips.md#looping)).
- **Gain envelope** shows or hides the clip's gain envelope.

## Editing the Clip

With the **Pointer**:

- Drag the left edge of the audio to trim the clip's start, and the right edge to trim its end (when the clip doesn't loop).
- Hold ++shift++ while dragging an edge to stretch the clip instead of trimming it.
- When the clip loops, drag the phase marker to change where playback starts in the loop, and drag the ends of the loop in the ruler to change the loop region.
- Drag in the body of the waveform to zoom around the pointer.

Double-click the ruler above the ticks to fit the view to the loop, or to the whole clip when it doesn't loop. A double-click on the ticks themselves places the edit cursor.

Right-click the waveform for **Show Pre-Marker Audio** and **Show Post-Marker Audio**, the slice commands (see [Slicing](../clips.md#slicing)), and **Reset Beat Grid Origin** once the grid origin has been moved. When the clip holds recorded takes, the menu also offers **Expand Takes** / **Collapse Takes** and the comp commands; see [Recording Takes & Comping](../recording-comping.md#comping-audio).

## Warp Markers

With **Warp** on (in the properties panel or the Inspector), warp markers pin points in the audio to positions on the timeline. They draw as hairlines with a flag at the top. See [Warp Markers](../clips.md#warp-markers) for what they do.

- **Add** - choose the **Marker** tool and click the waveform. A dotted guide follows the pointer to show where the marker will land. The tool is greyed out while Warp is off.
- **Move** - drag a marker to stretch the audio on either side of it. ++alt++ + drag moves it without stretching.
- **Delete** - double-click a marker, or right-click it and choose **Remove Warp Marker**.

## Gain Envelope

Click the gain envelope button in the toolbar to show the clip's [gain envelope](../clips.md#gain-envelope). The waveform dims, the sample markers hide, and the curve draws in the clip's colour over dB guides. An empty envelope is a flat 0 dB line. The envelope ranges from silence up to +6 dB.

Draw it with the envelope tools:

| Tool | What it does |
|------|--------------|
| **Line** | Drag a straight ramp. The default tool |
| **Pen** | Draw the envelope freehand |
| **Flat** | Click a grid cell to hold it at one level |

Each stroke is one undo step. The same tool edits the envelope on the clip in the arrangement while it is shown there. Whether the envelope is shown is saved with each clip.

## Labels

Labels name a part of the source file - a verse, a breath, a hit - without changing how the clip plays. A label is a named, coloured region, or a single point.

- **Add** - choose the **Label** tool, then click to place a point label or drag to label a region.
- **Move or resize** - drag a label's flag on the top edge of the waveform to move it, or drag its ends to resize a region. Moving a label never stretches the audio.
- **Edit** - right-click a label for **Rename Label...**, **Colour** and **Delete Label**. Double-clicking a label also deletes it.

Labels are saved with the project, follow linked ghost clips and pasted copies, and travel with the file into the [Media Library](media-library.md). The Inspector lists a clip's labels in its **Labels** section.

## Spectrogram

The spectrogram button in the rail swaps the waveform for a spectrogram, which shows the frequencies in the audio over time, from 30 Hz to 20 kHz on a logarithmic scale. Stereo files show the sum of both channels. The choice applies to every clip and is remembered between sessions.

The waveform stays on screen until the spectrogram is ready, which on a long file can take a moment. Markers, labels and the envelope draw on top as usual.

## Transients

The transient button in the rail shows or hides the transient markers MAGDA detects in the audio. How many it finds is set by **Sens** in the properties panel, or **Transient sensitivity** in the Inspector; even at the lowest setting the strongest onsets still show, so hide them with the button when you don't need them.

## Properties Panel

The panel on the right gives quick access to the clip's settings without switching to the [Inspector](inspector.md):

| Section | Controls |
|---------|----------|
| **Clip** | **WARP**, **BEAT** and **REV** (reverse) |
| **Stretch** | The stretch **Mode**, **Speed**, the source length in **Beats** and **BPM**, and its **Key** |
| **Transients** | **Sens** (detection sensitivity) |
| **Pitch** | **Semi** (semitones) and **Analog** |
| **Fades** | Fade in and out, with curves and behaviours, and the crossfade lengths. On an arrangement clip, the **auto-xfade** switch in the section header makes a crossfade where clips overlap |
| **Mix** | **Vol**, **Gain** and **Pan** |

A bookmark button saves the clip's current settings to the [Media Library](media-library.md). See [Audio Clip Properties](../clips.md#audio-clip-properties) for what each setting does.

Selecting several audio clips at once keeps the editor open in a multi-clip mode, where the Fades section edits all of them together.

## Clips on ARA Tracks

On a track with an [ARA plugin](../ara.md), the plugin plays the raw file, so the editor stands down MAGDA's own processing: **Loop** and the gain envelope button are greyed out, warp markers and the loop phase marker can't be edited, and ++shift++ + dragging an edge trims instead of stretching.

## Edit in an External Editor

To do destructive editing in another application (Audacity, iZotope RX, and so on), right-click an audio clip and choose **Edit in External Editor**. MAGDA hands the clip's source file to the application you have configured, so changes you save there are reflected back in MAGDA.

Set which application to use under **External audio editor** in **Settings > Paths** (see [Settings](../interface/preferences.md#external-audio-editor)). If none is set, MAGDA asks you to choose one first.
