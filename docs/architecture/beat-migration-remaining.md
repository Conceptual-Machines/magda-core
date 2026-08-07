# Beat Migration Remaining Work

Goal: clip and automation placement should be beat-authoritative. Seconds should remain only for true time domains: audio source duration, source offset, source loop ranges, rendering/export, recording wall-clock capture, and temporary UI pixel/playhead boundaries.

Current committed state:
- Automation points and snapping are beat-named and edit snapping is separate from recording.
- `ClipInfo::placement` is the canonical clip placement model.
- Audio clip creation has beat-first APIs.
- `ClipManager` has beat-first move, resize, duplicate, split, and trim APIs while older seconds APIs remain as compatibility shims.
- `ClipCommands` placement operations now take explicit `BeatPosition`/`BeatDuration` wrappers for create, move, resize, duplicate, split, and paste. Remaining seconds callers must convert locally at UI or source-duration boundaries instead of silently passing raw doubles.
- `tests/test_midi_clip_sync.cpp` uses beat placement setup instead of writing `clip.startTime`/`clip.length` directly.
- `clip.isBeatsAuthoritative()`/`clip.isBeatAuthoritative()` is gone.

Still left:
- UI drag/edit code still often computes placement through timeline seconds before constructing beat-native commands. Convert call sites that already work in bars/beats or clip placement to stay in beats end-to-end.
- Time-selection, render/export, recording, import from audio file duration, waveform display, and engine bridge code may remain seconds-based at their boundaries.
- Tests still directly assign/assert `clip.startTime` and `clip.length` in many places. Convert placement tests to `setPlacementBeats`, `placement.startBeat`, and `placement.lengthBeats`; keep seconds assertions only where verifying derived cache or true time behavior.
- `ClipInfo::startTime` and `ClipInfo::length` remain transitional derived caches. Do not add new direct writes. Rename/remove them only after callers no longer depend on them as placement state.

Guardrail:
- Do not add broad `timelineSecondsToBeats` adapter helpers in core as a convenience path. Conversions are acceptable only at explicit seconds boundaries, and should be local enough that the boundary is obvious.
