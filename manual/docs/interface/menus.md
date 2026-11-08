# Menus

MAGDA's menu bar has seven menus: **File**, **Edit**, **View**, **Transport**, **Track**, **Window** and **Help**. On macOS the **MAGDA** application menu comes first and holds **Settings…**.

Items with a shortcut show it beside their name. Shortcuts follow your own mapping, set under [Settings > Shortcuts](preferences.md#shortcuts); the keys below are the defaults, with ++cmd++ on macOS and ++ctrl++ on Windows and Linux.

## Settings

**Settings…** (++cmd+comma++ / ++ctrl+comma++) opens the [Settings](preferences.md) window. It is in the **MAGDA** menu on macOS and at the bottom of the **Edit** menu on Windows and Linux.

## File

| Item | Description |
|------|-------------|
| New Project | Create a new empty project |
| Open Project… | Open an existing project file |
| Open Recent | Recently opened projects, and **Clear Recent Projects** |
| Recover Unsaved Project… | Recover or discard work from a session that ended unexpectedly (see [Recovering unsaved work](preferences.md#recovering-unsaved-work)) |
| Close Project | Close the current project |
| Save Project | Save the current project |
| Save Project As… | Save the current project to a new file |
| Project Settings… | Open the [Project Settings](../arrangement-view.md#project-settings) dialog (title and credits, song length, sample rate, render/bounce bit depth) |
| Collect Files… | Copy every externally-referenced audio file (clips, sampler and drum-pad samples) into the project's media folder and repoint the project at the copies, so it travels self-contained. Shows progress and can be cancelled. |
| Export Audio… | Render the song to a WAV, FLAC, Opus or MP3 file: the entire song, the time selection or the loop region (see [Export Audio](../exporting.md#export-audio)) |
| Export MIDI… | Write the arrangement's MIDI clips to a Standard MIDI File, Type 0 (single track) or Type 1 (multi-track), for the entire song, the time selection or the loop region (see [Export MIDI](../exporting.md#export-midi)) |
| Import DAWproject… | Import a `.dawproject` interchange file (see [File Formats](../reference/file-formats.md#dawproject-interchange)) |
| Export DAWproject… | Export the project as a `.dawproject` interchange file |
| Quit | Exit MAGDA (Windows and Linux; on macOS, quit from the **MAGDA** menu) |

## Edit

| Item | Description |
|------|-------------|
| Undo | Undo the last action, named in the menu (++cmd+z++) |
| Redo | Redo the last undone action (++cmd+shift+z++) |
| Cut / Copy / Paste | Cut, copy and paste the selection (++cmd+x++, ++cmd+c++, ++cmd+v++) |
| Duplicate | Duplicate the selection (++cmd+d++) |
| Duplicate Clip With Automation | Duplicate the selected clips with the automation under them (++cmd+alt+shift+d++) |
| Duplicate Clip Without Automation | Duplicate the selected clips without copying automation |
| Duplicate Clip as Ghost | Duplicate the selected clips as [ghost clips](../clips.md#linked-ghost-clips) that mirror the original's content |
| Make Clip Unique | Detach the selected ghost clips from their link group |
| Delete | Delete the selection (++delete++ / ++backspace++) |
| Split / Trim | Split clips at the cursor, or trim them to the time selection (++cmd+e++) |
| Split All Tracks at Cursor | Split every clip that crosses the edit cursor, on all tracks |
| Join Clips | Join two adjacent clips into one (++cmd+j++) |
| Render Selected Clip(s) | Render the selected clips to audio (++cmd+b++) |
| Render Time Selection | Render the time selection to audio (++cmd+shift+b++) |
| Insert Time | Insert empty time at the time selection, moving later content right |
| Duplicate Time Range | Duplicate the time selection, moving later content right |
| Duplicate Loop Range | Duplicate the loop region on all tracks, moving later content right |
| Range Editing | A submenu: **Copy**, **Cut** and **Delete Time Range**; **Copy**, **Cut** and **Delete Loop Range**; and **Paste (Ripple)**, which makes room for the clipboard before pasting into it |
| Select All | Select all clips, or all notes of the edited clip when the editor has keyboard focus (++cmd+a++, see [Panel focus](overview.md#panel-focus)) |
| Settings… | Windows and Linux only (++ctrl+comma++) |

The time-range items need a time selection, and the loop-range items need a loop region. **Cut** and **Delete** ripple: later content moves left to close the gap. See [Arrangement View](../arrangement-view.md) for range editing in detail.

## View

| Item | Description |
|------|-------------|
| Track Manager… | Open the Track Manager dialog (see below) |
| Headers on the Right | Toggle track headers to the right side of the arrangement |
| Zoom In / Zoom Out | Zoom the arrangement (++h++ / ++g++) |
| Zoom to Fit | Fit all content in the visible area |
| Zoom Loop to Fit | Zoom to show the loop region |
| Zoom Selection to Fit | Zoom to show the current selection |
| Enter Full Screen | Toggle full-screen mode |

### Track Manager

The Track Manager lets you control which tracks are visible in each view. Open it from **View > Track Manager…**.

Each track has a checkbox for each view: **Live**, **Arrange** and **Mix**. The master channel has a row of its own. Click the checkboxes to show or hide individual tracks per view. This is useful for keeping the mixer focused on relevant tracks or hiding utility tracks from the arrangement.

## Transport

| Item | Description |
|------|-------------|
| Play | Start playback. While MAGDA is playing, the item reads **Pause** |
| Stop | Stop playback |
| Record | Start/stop recording |
| Loop | Toggle loop playback |
| Go to Start | Move the playhead to the beginning |
| Go to End | Move the playhead to the end |
| Add Marker | Add a marker at the playhead (++cmd+m++) |
| Previous Marker / Next Marker | Jump to the previous or next marker (++cmd+alt+arrow-left++ / ++cmd+alt+arrow-right++) |
| Recent Capture | Show what you just played, to choose what to keep (++cmd+shift+c++, see [Capturing what you just heard](../recording-comping.md#capturing-what-you-just-heard)) |

## Track

| Item | Description |
|------|-------------|
| Add Track | Add a new track (++cmd+t++) |
| Add Group Track | Add a new group track (++cmd+shift+t++) |
| Add Aux Track | Add a new aux/return track |
| Add Chord Track | Add the project's [chord track](../chord-track.md) (disabled once one exists) |
| Add Separator | Add a [separator](../tracks.md#separators) after the selected track |
| Delete Track | Delete the selected track |
| Duplicate Track | Duplicate the selected track with clips and FX chain (++cmd+d++) |
| Duplicate Track Without Content | Duplicate track structure and FX chain only, no clips (++cmd+shift+d++) |
| Duplicate Track Content Only | Duplicate the track's clips onto a new track with an empty FX chain (++cmd+alt+d++) |
| Mute Track | Toggle mute on the selected track (++m++) |
| Solo Track | Toggle solo on the selected track (++shift+s++) |

## Window

| Item | Description |
|------|-------------|
| Minimize | Minimize the window |
| Zoom | Zoom the window to fill the screen |
| Bring All to Front | Bring all MAGDA windows to front |

## Help

| Item | Description |
|------|-------------|
| Online Manual | Open this manual in the browser |
| Check for Updates… | Ask GitHub whether a newer MAGDA release is available |
| About MAGDA | Show version and credits |

### Check for Updates

**Help > Check for Updates…** asks the MAGDA GitHub releases feed whether a newer version is available, and tells you either way. MAGDA also runs the same check silently on launch, at most once every 24 hours, and only shows a dialog if a newer version is found. No telemetry is sent — the check is a plain public-releases request and carries no identifying information.

A stable release is told only about newer stable releases. A pre-release, such as a beta, is also told about newer pre-releases, so `1.0.0-beta.1` offers `1.0.0-beta.2` as well as `1.0.0`.
