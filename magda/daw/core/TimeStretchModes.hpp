#pragma once

namespace magda::time_stretch_mode {

// These values are persisted in project files and must stay aligned with
// tracktion::engine::TimeStretcher::Mode.
inline constexpr int kDisabled = 0;
inline constexpr int kSoundTouchNormal = 3;
inline constexpr int kSoundTouchBetter = 4;
inline constexpr int kSignalsmith = 15;

}  // namespace magda::time_stretch_mode
