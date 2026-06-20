# Recording Takes & Comping

When you loop-record over a section, MAGDA keeps every pass as a separate **take** instead of overwriting the previous one. You can then **comp** the takes together, swiping in the best part of each to build a single composite performance. This works for both audio and MIDI clips.

## Recording takes

1. Set the loop region over the part you want to record (see [Transport](transport.md)).
2. Enable **Loop** in the transport bar.
3. Arm the track and record.

Each pass around the loop is captured as its own take. Nothing is lost between passes, so you can keep playing until you have a take you like.

A live **Recording...** preview is drawn over the timeline as you record, showing the incoming waveform (audio) or notes (MIDI) growing under the playhead so you can see what was captured before recording stops.

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
