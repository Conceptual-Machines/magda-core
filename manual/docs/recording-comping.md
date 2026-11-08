# Recording Takes & Comping

When you loop-record over a section, MAGDA keeps every pass as a separate **take** instead of overwriting the previous one. You can then **comp** the takes together, swiping in the best part of each to build a single composite performance. This works for both audio and MIDI clips.

## Recording takes

1. Set the loop region over the part you want to record (see [Transport](transport.md)).
2. Enable **Loop** in the transport bar.
3. Arm the track and record.

Each pass around the loop is captured as its own take. Nothing is lost between passes, so you can keep playing until you have a take you like.

A live **Recording...** preview is drawn over the timeline as you record, showing the incoming waveform (audio) or notes (MIDI) growing under the playhead so you can see what was captured before recording stops.

## Capturing what you just heard

MAGDA can keep what you play, so an idea you played without recording is not lost. While capture is on, it keeps the master output and the notes you play on MIDI devices, whether or not the transport is running. Capture is off until you turn it on.

Click **MEM**'s dot or its caption in the transport bar to turn capture on or off; the meter beside them doesn't respond to clicks. Turning capture off stops it writing but keeps what it already holds, so you can still open, play, select and import it. Turning it on again carries on from where it stopped.

The **MEM** meter shows what is held: its level, with what you just played drawn bright, then how long that is and how full the buffer is. It reads *off* while capture is off. The dot is lit while it is recording.

Click the capture button beside the meter (++cmd+shift+c++, or **Transport > Recent Capture**) to open the capture panel under the transport. It shows the audio and the notes over the whole buffer, oldest at the left, with what you just played already selected:

- **With the transport stopped**, that is your last phrase, back to a pause of a few seconds, with a second of tail for the release.
- **Along with the transport**, it is the last bars of that pass. A stop or a loop wrap ends a pass.

Zoom and scroll with the ruler and scroll bar, as in the waveform editor. The play button plays the selection, or what is in view when nothing is selected. Drag across the lanes to select something else (click to clear, double-click to go back to what you just played), then press **Import Audio** to put the master audio on a new **Capture Audio** track, or **Import MIDI** to put the notes on a new **Capture MIDI** track. Notes from several MIDI devices go on a track each, named after the device. A stretch the transport played lands at the bars where you heard it; anything else lands at the playhead. The click is never included. Notes stay in the buffer after their MIDI device is unplugged.

**Settings > General**, under **Engine**, has the same switch (*Keep what you play, so it can be captured afterwards*) and two lengths:

- **Capture Buffer** — how much is kept: 5 minutes by default, up to 4 hours. The first minute is held in memory and anything longer in a file on disk; the setting shows the size each length takes.
- **Capture Length** — caps what is selected for you, 8 bars by default.

The file goes in the folder set under **Capture History** in **Settings > Paths**, in a `MAGDA Capture` subfolder; by default that is the system's temporary folder. Point it at a drive with room for long buffers. If the folder can't be used or is short of space, the history stays in memory, up to 30 minutes. Changing **Capture Buffer** or the folder starts a new history.

## Selecting a take

Select the recorded clip and open the **Inspector**. The **Takes** section lists the captured passes:

- The take selector reads **Take N / M** — choose which take plays back.
- **Expand take lanes** / **Collapse take lanes** toggles the stacked take view in the editor.
- **Clear Comp** removes the comp and returns to a single selected take (shown only while a comp is active).

When a comp is assembled, the selector reads **Comp** instead of a take number.

## Comping audio

Expand the take lanes on an audio clip to open the [Waveform Editor](panels/waveform-editor.md) take view. Each take is drawn as a stacked lane labelled **Take 1**, **Take 2**, and so on, with the active take marked by an accent bar down its side.

To build a comp, **swipe horizontally across a take lane** over the range you want from that take. The swiped region is added to the comp and shaded in the clip colour; swipe a different range on another lane to pull that section from a different take. MAGDA renders the selected regions into a single composite clip as you go.

## Comping MIDI

MIDI clips comp the same way. In the [Piano Roll](panels/piano-roll.md), click **Show take lanes** in the editor header to reveal the folded take strip below the grid. Each take gets its own mini-roll, and all lanes share a single folded pitch axis (the union of every take's pitches) so notes line up across takes on the same beats.

- **Click** a lane to bring that take to the front as the active take.
- **Swipe** across a lane over a beat range to assign that range of the comp to that take.
- **Right-click** a lane to clear the comp.

As with audio, the result is a single MIDI clip assembled from the chosen regions.
