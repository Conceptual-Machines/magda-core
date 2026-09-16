// Where a playing slot's audio is at, from the audio thread (#2674). A wrap
// heard early has two possible sources -- the handle's pass and the voice's
// source loop -- and neither is visible from the message thread after the
// block has gone. Off unless a host installs a sink; nothing here allocates.

#pragma once

#include <cstdint>

namespace magda::engine {

struct PlaybackTraceEntry {
    enum class Kind : std::uint8_t { PassWrap, VoiceWindow, Prime, OpeningAudio };
    Kind kind = Kind::VoiceWindow;
    std::int64_t clip = 0;
    double beat = 0.0;  // timeline beat at the start of the block
    double a = 0.0;     // PassWrap: pass beats.   VoiceWindow: source sample the block opens at
    double b = 0.0;     // PassWrap: wrap beat.    VoiceWindow: source sample the block closes at
    double c = 0.0;     // PassWrap: origin beat.  VoiceWindow: loop start samples
    double d = 0.0;     // PassWrap: elapsed.      VoiceWindow: loop length samples
};

using PlaybackTraceSink = void (*)(const PlaybackTraceEntry&, void* context);

/// Message thread, before the audio starts. A null sink turns the trace off.
void setPlaybackTraceSink(PlaybackTraceSink sink, void* context);

/// Audio thread. Calls the sink when one is installed.
void playbackTrace(const PlaybackTraceEntry& entry);

}  // namespace magda::engine
