# ARA Plugins

ARA plugins such as Melodyne or VocAlign work on a track's recordings directly rather than on the audio streaming past them. In MAGDA, inserting an ARA plugin on a track hands it every audio clip on that track, and the track plays through the plugin live. There is no transfer step and no separate render back.

## Before you start

MAGDA notes whether a plugin supports ARA when it scans your plugins. If you are upgrading from an earlier version of MAGDA, rescan your plugins once so ARA plugins are recognised: click **Rescan all** in **Settings > Plugins** (see [Plugin Settings](interface/plugin-settings.md#scanning)). Until then an ARA plugin loads as an ordinary effect with no access to your clips.

On macOS, use the plugin's VST3 or Audio Unit version. An Audio Unit v3 plugin that runs out of process can't be bound to ARA yet; use its VST3 instead.

## Inserting an ARA plugin

Add the plugin to an audio track like any other effect: drag it from the [Plugin Browser](panels/browsers.md#plugin-browser) onto the track or into its [FX chain](fx-chain.md). It can sit anywhere in the chain, including inside a rack.

As soon as it loads, every audio clip in the Arrangement on that track becomes a region in the plugin, at its position on the timeline, and the plugin starts analysing it. Clips you add, move or trim later follow automatically.

To edit, open the plugin's window from its device header. Opening the window selects the track's clips in the plugin, which is what the plugin shows and analyses. The plugin also sees the project's tempo and time signature, so its bar grid matches the arrangement.

Tracks that use the same ARA plugin share one plugin document. Each track's plugin window opens on that track's clips.

## Playing and rendering

The track plays through the plugin as you work:

- The plugin's playhead follows MAGDA's transport.
- Edits are heard as you make them, and clicking a note in the plugin auditions it.
- Moving or trimming a clip during playback is followed without a gap.
- Exports, **Bounce To New Track** and [freezing the track](tracks.md#freezing-tracks) render through the plugin.

Nothing is written back to the clips' audio files. To commit the plugin's edits to audio, bounce the clips to a new track or freeze the track.

## What happens to the clips

The plugin plays each clip from its source file, once and as recorded. MAGDA's own clip processing doesn't apply on that track, so the controls for it stand down:

- **Inspector** - the clip's mix, loop, warp and pitch controls are greyed out, Reverse and the gain envelope included, with a note naming the plugin.
- **Arrangement and Waveform Editor** - warp markers, the loop phase marker, fade and volume handles, and gain envelope editing are disabled. ++shift++ + dragging a clip edge resizes the clip instead of stretching it, and **Loop** is unavailable.
- **Playback** - a looping clip plays once, and a clip in beat mode or with a speed change plays the file at its own speed.
- **Drawing** - each clip draws the stretch of file the plugin plays: forwards, unstretched, with no loop repeats or warping, and with a dimmed waveform. Its header shows the plugin's initial, and the Inspector shows the same chip (hover it for *Played by* and the plugin's name).

Your clip settings are kept. Remove the plugin and they apply again.

## Saving

The plugin's edits are saved with the project. Each time you save, MAGDA stores them in the project's media folder, under `<Project>_Media/ara/`, one file per plugin, and restores them when the project reopens.

The edits stay with the clips' sources, so relinking a missing file, collecting media into the project or moving a file doesn't lose them.

## Limits

- **Arrangement audio clips only.** Session View clips and MIDI clips on the track aren't handed to the plugin.
- **No stretching or pitch from MAGDA.** Tempo following, warp, speed, pitch, reverse and looping are left to the plugin; MAGDA plays the raw file into it.
- **Use MAGDA's transport.** Play, stop and locate requests from inside the plugin's window don't move MAGDA's transport.
