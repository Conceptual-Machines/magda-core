# Stem Separation

Stem separation splits one audio clip into several, each carrying a different part of the sound: vocals on one track, drums on another, and so on. MAGDA does this offline on your own machine, writing real audio files into the project.

Right-click an audio clip and choose **Split into Stems**, then pick an engine.

## Engines

Three engines are available, trading quality against speed and download size.

| Engine | Stems | Needs a download |
|--------|-------|------------------|
| **Harmonic / Percussive (HPSS)** | Harmonic, Percussive | No |
| **Vocals / Drums / Bass / Other (Demucs)** | Drums, Bass, Other, Vocals | Yes, 301 MB |
| **Vocals / Accompaniment (Spleeter)** | Vocals, Accompaniment | Yes, 74 MB |

**HPSS** is plain signal processing with no machine-learning model behind it. It separates sustained, pitched content from transient content, so it is the right tool for pulling a drum layer away from a pad or a guitar, and the wrong tool for isolating a singer. It needs no download and works in every build.

**Demucs** gives the best separation of the three and is the slowest. It produces the familiar four-way split.

**Spleeter** is quicker and lighter than Demucs, at the cost of quality, and only splits two ways: the vocal and everything else.

!!! note "Intel macOS"
    Demucs and Spleeter run on the ONNX runtime, which is not part of the Intel macOS build. On those builds the submenu offers HPSS only.

## Downloading the models

Demucs and Spleeter weights are not bundled with MAGDA — they are fetched on demand.

An engine whose weights are missing shows in the menu with a trailing `...`. Choosing it opens **AI Settings > Models** with the **Stem separation** category selected, rather than failing. You can also go there directly at any time to install or remove either model. See [AI Settings](interface/ai-settings.md#stem-separation).

Downloads come from HuggingFace, are checked against a pinned SHA-256, and land in MAGDA's application data folder under `StemSeparation/models`. The Models page shows the source repository for each one as a clickable link, and lets you remove weights you no longer want to keep on disk.

## What a split produces

Clicking an engine starts a two-phase operation.

**Immediately**, one empty audio track per stem appears directly beneath the source track, wrapped in a group named after the source file (`Guitar Take 3 Stems`). The track area reflects the operation as soon as you click, so you can see what is coming.

**When separation finishes**, a clip lands on each stem track. Each one is a duplicate of the source clip with its audio file swapped for the rendered stem, so start position, offset, loop, stretch mode, and warp markers all stay exactly where they were on the original. Take and comp data is not carried over — that belongs to the original recording, not to a stem of it.

Finally the **source track is muted**, so the stems do not play on top of the material they came from. Unmute it whenever you want to compare.

A loading banner reads *Splitting into stems...* with a live percentage while the split runs. A toast confirms completion, or reports the failure.

### Undo

The split lands as two undo steps: **Split into Stems** creates the tracks and the group, and **Add Stem Clips** adds the clips and mutes the source. Undoing the second restores the source track's mute state along with the clips.

If separation fails part-way, the empty tracks and group created in the first phase are removed for you.

## Where the files go

Stems are written as 32-bit float WAV into a `stems/` folder inside the project's media directory, one subfolder per split named after the source and the engine:

```
<project media>/stems/Guitar Take 3 - htdemucs/
    Guitar Take 3 - Drums.wav
    Guitar Take 3 - Bass.wav
    Guitar Take 3 - Other.wav
    Guitar Take 3 - Vocals.wav
```

Splitting the same clip twice never overwrites an earlier result; the second run gets its own folder.

## Limits

- **One clip at a time.** The menu item is unavailable for a multiple selection.
- **One split at a time.** Engines are greyed out while another split is running.
- **The source file must be on disk.** Clips whose audio has gone missing cannot be split.
- **Very long files are refused.** Separation decodes the whole file into memory, and MAGDA rejects anything that would need more than about a gigabyte rather than attempting it.

Separation is CPU-intensive and runs off the audio thread, so playback keeps going while it works, but expect a Demucs pass over a full song to take a while.
