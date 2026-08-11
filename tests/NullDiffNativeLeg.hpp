#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffCompare.hpp"

/**
 * @file NullDiffNativeLeg.hpp
 * @brief A null-diff case rendered through the native engine (#2040).
 *
 * The engine's own offline render, the engine's own compiler and the engine's
 * own voice pool. Nothing here reimplements a step of it: a harness that
 * rendered through a second implementation would prove things about the second
 * implementation.
 *
 * What this file does own is the host side the engine deliberately does not
 * have. A snapshot carries paths because that is what the model holds, and
 * turning one into a reader is the host's job (io/AudioFileReader.hpp), so the
 * factory that opens a WAV lives here. So does the device standing in for the
 * synth a MIDI clip would play, which records what arrives instead of making a
 * sound: the plan compiles no ClipMidi op for a track whose chain consumes no
 * MIDI, so the capture point and the reason the MIDI exists at all are the same
 * thing.
 */

namespace magda::nulldiff {

struct NativeRender {
    juce::AudioBuffer<float> audio;
    MidiStream midi;

    /// Set when the case could not be rendered at all, which is never reported
    /// as a residual.
    std::string failure;

    /// What the engine said it could not do. Anything in here is a finding
    /// whether or not the audio nulled: a snapshot that dropped a clip and a
    /// render that matched it are two bugs, not none.
    std::vector<std::string> diagnostics;

    /// Clips that wanted a voice and had none, and MIDI a block could not fit.
    /// Silence nobody counted is indistinguishable from silence that was meant,
    /// so both are read back and reported.
    int starvedVoices = 0;
    int droppedMidiEvents = 0;
};

/// Render @p value through the native engine, over its own beat range.
NativeRender renderNative(const Case& value);

}  // namespace magda::nulldiff
