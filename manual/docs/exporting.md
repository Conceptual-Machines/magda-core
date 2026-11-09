# Exporting

**File > Export Audio…** renders your song to a stereo audio file, and **File > Export MIDI…** writes its MIDI clips to a Standard MIDI File. Both open a dialog for the options, then a save dialog for the file.

To hand a whole project to another DAW, use **File > Export DAWproject…** instead (see [File Formats](reference/file-formats.md#dawproject-interchange)).

## Export Audio

Choose **File > Export Audio…**, set the options, and click **Export**.

### Format

| Option | Choices |
|---|---|
| **Format** | **WAV**, **FLAC**, **Opus** or **MP3**. The dialog opens on the format you exported last |
| **Sample Rate** | **44.1 kHz**, **48 kHz**, **96 kHz** or **192 kHz**. It starts at the project's sample rate, rounded down to one of these. MP3 goes up to 48 kHz |
| **Bit Depth** | WAV: **16-bit**, **24-bit** or **32-bit Float**. FLAC: **16-bit** or **24-bit**. It starts at the project's **Render Bit Depth** from [Project Settings](arrangement-view.md#project-settings) |
| **Dither** | **Off**, **TPDF** (the default) or **Noise Shaped**. Not available at 32-bit Float, which needs none |
| **Bitrate** | Opus and MP3 only, in place of **Bit Depth** and **Dither**. Opus: 64 to 320 kbps, 160 kbps by default. MP3: **VBR V0**, **V2** or **V4**, or **CBR** at 128, 192, 256 or 320 kbps; VBR V2 by default. MAGDA remembers the last bitrate you used for each |

See [File Formats](reference/file-formats.md#audio-formats) for what each format stores.

### Options

- **Normalize to 0 dB (peak)** — Raises or lowers the whole file so its loudest peak sits at 0 dB. Off by default.
- **Real-time render (slower, avoids plugin glitches)** — Renders at playback speed rather than as fast as possible, for plugins that misbehave when run faster than real time. Off by default.
- **Lead-in Silence** — Up to 2 seconds of silence before the audio, in steps of 0.1 s. 0 s by default.

### Export Range

| Range | What it exports |
|---|---|
| **Entire Song** | From the start of the arrangement to the end of its last clip. The default |
| **Time Selection** | The arrangement's [time selection](arrangement-view.md#selection). Available when there is one; a selection made only on automation lanes doesn't count. A selection over some of the tracks still exports the whole mix over that time |
| **Loop Region** | The loop region. Available while looping is on |

### What goes into the file

The export is the whole mix as it reaches the master, through every track's chain and the master's own effects. A powered-off device stays off in the file (see [Device Power](fx-chain.md#device-power)), and a [disabled track](tracks.md#disabling-a-track) renders nothing. The [chord track](chord-track.md) is left out.

If the project holds a routed [External FX or External Instrument](fx-chain.md#external-hardware-inserts), MAGDA first plays the export range once in real time to record the hardware, in a box titled *Capturing External Hardware* with a **Cancel** button, and then renders.

Opus and MP3 files carry the **Title**, **Artist**, **Album**, **Year**, **Genre** and **Comment** from the [Metadata](arrangement-view.md#metadata) tab of Project Settings as tags; empty fields are left out. A WAV file instead carries the tempo, the time signature and the project's key, when one is set, in the loop information that samplers and loop-aware DAWs read. An **Entire Song** export is marked as a one-shot and a **Loop Region** export as a loop. With lead-in silence the tempo is left out, since the file no longer starts on the beat. FLAC files carry no tags.

### Where the file goes

After **Export**, a save dialog opens in the folder set under **Settings > Rendering** (Documents unless you chose another), with a name built from the **Export** pattern there, `<project-name>_<date-time>` by default. See [Rendering](interface/preferences.md#rendering). In an export, `<track-name>` reads `master` and `<clip-name>` reads the project name, and spaces in the project name become underscores. MAGDA adds the format's extension and asks before replacing an existing file.

A progress window, *Exporting Audio…*, follows the render and has a **Cancel** button. Playback stops while MAGDA renders and starts again afterwards if it was playing. When the file is written, a message gives its full path.

## Export MIDI

**File > Export MIDI…** writes the MIDI clips in the arrangement to a `.mid` file.

| Option | Choices |
|---|---|
| **MIDI Format** | **Type 1 (Multi-Track)**, the default: a track per MAGDA track that has MIDI clips, named after it, after a first track holding the tempo and time signature. **Type 0 (Single Track)**: everything in one track |
| **Export Range** | **Entire Song**, **Time Selection** or **Loop Region**, available as for audio. **Entire Song** runs from the start of the arrangement to the end of its last MIDI clip |

The file holds the clips' notes, CC and pitch bend, all on MIDI channel 1. The range starts at the beginning of the file, and notes that cross its end are cut there. The tempo is written once, the project tempo, with the project's time signature. Session view clips and automation lanes aren't exported.

The save dialog opens in your Documents folder with the project's name. If no MIDI clip has anything in the range, MAGDA says so instead.

## Other ways to render

- **Edit > Render Selected Clip(s)** and **Edit > Render Time Selection** render to audio clips in the project; see [Render](arrangement-view.md#render).
- [Freezing a track](tracks.md#freezing-tracks) renders it to a file that plays in its place.
- Programs connected through the [Remote API](reference/remote-api.md) can render a range with [`engine.renderRange`](reference/remote-api-operations.md#engine).
