#pragma once

namespace magda::time_stretch_mode {

// PINNED: MAGDA-owned constants, persisted in project files as these integers.
// They happen to equal tracktion::engine::TimeStretcher::Mode today, so the
// engine bridge is a cast and a static_assert in the audio layer holds that
// equality. If the engine ever renumbers, the bridge grows a mapping - these
// values do not move. See docs/architecture/persisted-enums.md.
inline constexpr int kDisabled = 0;
inline constexpr int kSoundTouchNormal = 3;
inline constexpr int kSoundTouchBetter = 4;
inline constexpr int kSignalsmith = 15;

}  // namespace magda::time_stretch_mode
