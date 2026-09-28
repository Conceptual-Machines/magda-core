# Loop length intent (#2675)

Repeat in native and Tracktion playback.

1. Import a recording longer than eight bars into a Session slot and enable beat
   mode. Set its source tempo to 100 BPM, then use the waveform loop handles to
   choose a four-bar loop inside the recording.
2. Correct the source tempo to 120 BPM. The loop should still show four bars,
   repeat at four-bar boundaries, and retain its source start position.
3. Move the whole loop region without resizing it, then correct the source tempo
   again. Its musical length should remain four bars.
4. Save and reopen the project. Correct the source tempo again and check that
   the musical count is still preserved.
5. Import a whole two-second file at 120 BPM. Correct its source tempo to
   240 BPM. The loop should cover the entire file and show eight beats.
6. Edit that loop's musical length in the inspector, undo, and redo. Verify that
   the source-region length and the musical edit are restored respectively.
   Undo once more, then correct its source tempo. The restored loop should
   retain the whole source region.

Also open a project saved before this change: existing loops should retain
their source regions when their tempo is corrected.
