#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"

namespace magda::daw::ui {

/**
 * @brief FFT spectrum display for the Spectrum Analyzer analysis device.
 *
 * Polls the plugin's AudioTapBuffer on a timer, runs a Hann-windowed FFT, maps
 * magnitudes to a log-frequency / dB plot with exponential temporal smoothing
 * and a decaying peak-hold trace.
 */
class SpectrumAnalyzerUI : public juce::Component, private juce::Timer {
  public:
    SpectrumAnalyzerUI();
    ~SpectrumAnalyzerUI() override;

    void setPlugin(daw::audio::SpectrumAnalyzerPlugin* plugin) {
        plugin_ = plugin;
    }

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    float freqToX(float hz, juce::Rectangle<float> area) const;
    float dbToY(float db, juce::Rectangle<float> area) const;

    daw::audio::SpectrumAnalyzerPlugin* plugin_ = nullptr;

    static constexpr int kFftOrder = 11;  // 2048-point FFT
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kNumBins = kFftSize / 2;

    static constexpr float kMinDb = -100.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;

    juce::dsp::FFT fft_{kFftOrder};
    juce::dsp::WindowingFunction<float> window_{static_cast<size_t>(kFftSize),
                                                juce::dsp::WindowingFunction<float>::hann};

    std::vector<float> readBuf_;     // kFftSize newest samples
    std::vector<float> fftData_;     // 2 * kFftSize (in-place real transform)
    std::vector<float> smoothedDb_;  // per-bin, exponentially smoothed
    std::vector<float> peakDb_;      // per-bin peak-hold with decay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerUI)
};

}  // namespace magda::daw::ui
