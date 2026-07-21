// Spleeter ONNX backend for stem separation (issue #1288).
//
// Wraps Deezer's Spleeter 2-stem release via the sherpa-onnx conversion:
// one UNet per stem (vocals.onnx, accompaniment.onnx), both consuming the
// same magnitude-spectrogram tensor. Unlike Demucs, the DSP lives in the
// host: STFT (4096/1024 hann, center=false) -> first 1024 bins as magnitude,
// chunked into 512-frame splits -> each UNet predicts its stem's magnitude
// -> Wiener-combined soft masks applied to the full complex STFT (bins
// above 1024 zeroed) -> iSTFT. Faster and lighter than Demucs (~78 MB,
// RTF ~0.1 CPU) at lower separation quality.
//
// Pimpl over two Ort::Sessions, same MAGDA_HAVE_CLAP gating as the other
// ONNX backends.

#pragma once

#include <filesystem>

#include "StemSeparator.hpp"

namespace magda::stems {

// The conversion's DSP geometry, exposed for tests (compiled regardless of
// the ONNX gate).
namespace spleeter {
constexpr int kSampleRate = 44100;
constexpr int kFftSize = 4096;
constexpr int kHop = 1024;
constexpr int kNumBins = kFftSize / 2 + 1;  // 2049
constexpr int kKeptBins = 1024;             // model sees only the first 1024
constexpr int kFramesPerSplit = 512;

// Frames padded up to a whole number of 512-frame splits (no extra split
// when already aligned, matching the sherpa-onnx C++ guard).
constexpr int paddedFrameCount(int numFrames) {
    const int rem = numFrames % kFramesPerSplit;
    return rem == 0 ? numFrames : numFrames + (kFramesPerSplit - rem);
}
}  // namespace spleeter

class SpleeterSeparator : public StemSeparator {
  public:
    // Loads the two UNets. On failure (missing files, bad models, or a
    // build without ONNX) construction leaves the backend unusable and
    // separate() returns empty; check isLoaded().
    SpleeterSeparator(const std::filesystem::path& vocalsModelPath,
                      const std::filesystem::path& accompanimentModelPath);
    ~SpleeterSeparator() override;

    SpleeterSeparator(const SpleeterSeparator&) = delete;
    SpleeterSeparator& operator=(const SpleeterSeparator&) = delete;

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
        return "spleeter-2stems";
    }

    [[nodiscard]] std::vector<juce::String> stemNames() const override {
        return {"Vocals", "Accompaniment"};
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::stems
