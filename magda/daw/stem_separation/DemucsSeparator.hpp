// Demucs ONNX backend for stem separation (issue #1288).
//
// Wraps Meta's htdemucs v4 (hybrid transformer, 4 stems) via the StemSplitio
// single-file ONNX export, which re-implements STFT/iSTFT in-graph so the
// host contract collapses to: feed 7.8 s chunks of 44.1 kHz stereo waveform
// (1, 2, 343980), read (1, 4, 2, 343980) stems. Per-segment normalization is
// baked into the graph too — no host-side preprocessing beyond resampling.
//
// Pimpl over Ort::Session, mirroring transcription/BasicPitchTranscriber, so
// onnxruntime headers don't escape and the same MAGDA_HAVE_CLAP gate decides
// availability. Full-track processing follows the export's own reference:
// chunks at 25% overlap with a trapezoid weight, weight-normalized
// overlap-add, last chunk zero-padded and trimmed.

#pragma once

#include <filesystem>
#include <vector>

#include "StemSeparator.hpp"

namespace magda::stems {

// The export's chunking geometry, exposed for tests (compiled regardless of
// the ONNX gate).
namespace demucs {
constexpr int kSampleRate = 44100;
constexpr int kNumStems = 4;
constexpr int kSegmentSamples = 343980;                            // 7.8 s
constexpr int kOverlapSamples = kSegmentSamples / 4;               // 85995
constexpr int kStrideSamples = kSegmentSamples - kOverlapSamples;  // 257985

// Ones with a linear fade-in over the first kOverlapSamples and a mirrored
// fade-out at the end (numpy linspace endpoints: w[0] == 0).
std::vector<float> trapezoidWindow();
}  // namespace demucs

class DemucsSeparator : public StemSeparator {
  public:
    // Loads the ONNX model at modelPath. On failure (missing file, bad
    // model, or a build without ONNX) construction leaves the backend
    // unusable and separate() returns empty; check isLoaded().
    explicit DemucsSeparator(const std::filesystem::path& modelPath);
    ~DemucsSeparator() override;

    DemucsSeparator(const DemucsSeparator&) = delete;
    DemucsSeparator& operator=(const DemucsSeparator&) = delete;

    // True when this build has the ONNX backend compiled in at all.
    [[nodiscard]] static constexpr bool backendAvailable() noexcept {
#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] bool isLoaded() const noexcept;

    std::vector<Stem> separate(const juce::AudioBuffer<float>& input, double sampleRate,
                               const Progress& progress) override;

    [[nodiscard]] const char* modelId() const noexcept override {
        return "htdemucs";
    }

    [[nodiscard]] std::vector<juce::String> stemNames() const override {
        return {"Drums", "Bass", "Other", "Vocals"};
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::stems
