#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <magda/sdk/tap/SampleRing.hpp>
#include <memory>
#include <vector>

namespace magda::daw::ui {

/// The spectrum of a device's input and output, drawn under its faceplate's curve.
class SpectrumOverlay {
  public:
    SpectrumOverlay();

    /// Forget both traces, for a faceplate bound to a different device.
    void reset();
    /// Fold in whatever the taps gained since the last call. Message thread.
    void update(const magda::engine::SampleRing& input, const magda::engine::SampleRing& output);
    /// Input dim, output in the info accent, between @p minHz and @p maxHz.
    void draw(juce::Graphics& g, juce::Rectangle<float> area, double sampleRate, float minHz,
              float maxHz, const std::function<float(float hz)>& frequencyToX) const;

  private:
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kNumBins = kFftSize / 2;
    static constexpr float kMinDb = -90.0f;
    static constexpr float kMaxDb = 0.0f;

    juce::dsp::FFT fft_{kFftOrder};
    juce::dsp::WindowingFunction<float> window_{static_cast<size_t>(kFftSize),
                                                juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> readBuffer_;
    std::vector<float> fftData_;
    std::vector<float> inputDb_;
    std::vector<float> outputDb_;
    size_t lastInputPosition_ = 0;
    size_t lastOutputPosition_ = 0;
    double lastUpdateSeconds_ = 0.0;  // For smoothing by elapsed time.

    void updateTrace(const magda::engine::SampleRing& tap, size_t& lastPosition,
                     std::vector<float>& traceDb, double elapsedSeconds);
};

}  // namespace magda::daw::ui
