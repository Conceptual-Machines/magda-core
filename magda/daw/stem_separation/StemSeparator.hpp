// Audio source separation (issue #1288).
//
// Backend-agnostic interface. A StemSeparator turns an audio buffer into named
// stems in the SAMPLES domain: it knows only channels, samples and Hz, and
// deliberately does NOT depend on the clip model or tempo. Placement of the
// resulting stems on tracks (the beats domain) happens one layer up in
// StemSeparationService, the only place the two domains meet. This mirrors
// transcription::Transcriber so DSP (HPSS) and ONNX (Demucs, Spleeter)
// backends are interchangeable to callers.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace magda::stems {

// One separated stem. Same channel count, length and sample rate as the
// input buffer handed to separate().
struct Stem {
    juce::String name;  // User-facing: "Vocals", "Drums", "Harmonic", ...
    juce::AudioBuffer<float> audio;
};

class StemSeparator {
  public:
    virtual ~StemSeparator() = default;

    // Called periodically from the worker thread with overall progress 0..1.
    // Return false to cancel; separate() then returns empty.
    using Progress = std::function<bool(float)>;

    // Separate PCM into stems. sampleRate is the buffer's rate; backends
    // resample internally to whatever the model expects and resample the
    // stems back. Returns empty on failure or cancellation (never throws to
    // the caller). Sums of the returned stems reconstruct the input (up to
    // model error), so the operation is non-destructive by construction.
    virtual std::vector<Stem> separate(const juce::AudioBuffer<float>& input, double sampleRate,
                                       const Progress& progress) = 0;

    // Stable identifier for the backend (e.g. "hpss", "htdemucs").
    [[nodiscard]] virtual const char* modelId() const noexcept = 0;

    // Stem names this backend produces, in output order.
    [[nodiscard]] virtual std::vector<juce::String> stemNames() const = 0;
};

}  // namespace magda::stems
