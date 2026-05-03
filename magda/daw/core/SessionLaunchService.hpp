#pragma once

#include <vector>

#include "TypeIds.hpp"

namespace magda {

/// Centralised scene-launch / stop-track / group-scene semantics.
///
/// Both the SessionView (UI scene buttons) and SessionApi (Lua / scripts /
/// controllers) used to inline the same loop — "for each track, trigger the
/// clip in this scene if present, else stop the track". The duplication
/// meant any tweak (quantization, empty-slot behaviour, group handling) had
/// to be made twice and could silently drift between surfaces.
///
/// All entry points are stateless and read singletons (ClipManager,
/// TrackManager) directly. Keep them small — this is glue, not a
/// reimplementation of playback logic.
namespace SessionLaunchService {

/// Apply scene-launch semantics to a specific list of tracks.
/// For each track: trigger (track, sceneIndex) if a clip exists, else stop
/// the track. Order matches `trackIds`.
void launchScene(const std::vector<TrackId>& trackIds, int sceneIndex);

/// Convenience: apply scene-launch semantics to every track in TrackManager.
/// Used by the API and any caller that operates on the whole grid; UI
/// callers usually pass their visible-track subset to launchScene() above.
void launchSceneAllTracks(int sceneIndex);

}  // namespace SessionLaunchService

}  // namespace magda
