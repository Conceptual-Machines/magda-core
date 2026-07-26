// Harmonic / percussive source separation (issue #1288).
//
// Pure DSP, no ML model: median filtering of the STFT magnitude (Fitzgerald
// 2010, the same scheme as librosa.decompose.hpss). Harmonic content forms
// horizontal ridges in the spectrogram (stable across time), percussive
// content vertical ones (broadband at one instant); a median filter along
// each axis enhances one and suppresses the other, and Wiener-style soft
// masks split the input. Always available: no download, no ONNX, and fast
// enough to run on any clip without a progress wait worth speaking of.
//
// Produces exactly two stems, "Harmonic" and "Percussive", which sum to the
// input sample-exactly (the soft masks are complementary by construction).

#pragma once

#include "StemSeparator.hpp"

namespace magda::stems {

class HpssSeparator : public StemSeparator {
  public:
    std::vector<Stem> separate(const juce::AudioBuffer<float>& input, double sampleRate,
                               const Progress& progress) override;

    [[nodiscard]] const char* modelId() const noexcept override {
        return "hpss";
    }

    [[nodiscard]] std::vector<juce::String> stemNames() const override {
        return {"Harmonic", "Percussive"};
    }
};

}  // namespace magda::stems
