# Beat Migration Remaining Work

Goal: clip and automation placement should be beat-authoritative. Seconds should remain only for true time domains: audio source duration, source offset, source loop ranges, rendering/export, recording wall-clock capture, and temporary UI pixel/playhead boundaries.

Current committed state:
- Automation points and snapping are beat-named and edit snapping is separate from recording.
- `ClipInfo::placement` is the canonical clip placement model.
- Audio clip creation has beat-first APIs.
- `ClipManager` has beat-first move, resize, duplicate, split, and trim APIs while older seconds APIs remain as compatibility shims.
- `ClipCommands` placement operations now take explicit `BeatPosition`/`BeatDuration` wrappers for create, move, resize, duplicate, split, and paste. Remaining seconds callers must convert locally at UI or source-duration boundaries instead of silently passing raw doubles.
- `ClipInfo` stores no timeline seconds (#2791): `startTime`/`length` are gone, and readers derive seconds from placement beats, through the tempo map where the tempo varies.
- `clip.isBeatsAuthoritative()`/`clip.isBeatAuthoritative()` is gone.

Still left:
- UI drag/edit code still often computes placement through timeline seconds before constructing beat-native commands. Convert call sites that already work in bars/beats or clip placement to stay in beats end-to-end.
- Time-selection, render/export, recording, import from audio file duration, waveform display, and engine bridge code may remain seconds-based at their boundaries.

Guardrail:
- Do not add broad `timelineSecondsToBeats` adapter helpers in core as a convenience path. Conversions are acceptable only at explicit seconds boundaries, and should be local enough that the boundary is obvious.
