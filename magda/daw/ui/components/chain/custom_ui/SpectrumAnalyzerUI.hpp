#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"

namespace magda::daw::ui {

/**
 * @brief FFT spectrum display for the Spectrum Analyzer analysis device.
 *
 * Polls the plugin's AudioTapBuffer on a timer, runs a Hann-windowed FFT, maps
 * magnitudes to a log-frequency / dB plot with temporal smoothing and a
 * decaying peak-hold trace. FFT size, display slope (tilt) and response speed
 * are user controls, persisted on the plugin.
 */
class SpectrumAnalyzerUI : public juce::Component, private juce::Timer {
  public:
    SpectrumAnalyzerUI();
    ~SpectrumAnalyzerUI() override;

    void setPlugin(daw::audio::SpectrumAnalyzerPlugin* plugin);

    // Compact mode hides the control row (FFT/slope/speed/colour) and uses the
    // full bounds for the plot — used by the mini visualizer on the mixer.
    void setCompact(bool compact);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    void timerCallback() override;
    void rebuildFft(int order);  // (re)allocate FFT + buffers for a 2^order transform
    float freqToX(float hz, juce::Rectangle<float> area) const;
    float dbToY(float db, juce::Rectangle<float> area) const;
    juce::Rectangle<float> plotArea() const;  // plot region (excludes the control row)

    bool compact_ = false;
    daw::audio::SpectrumAnalyzerPlugin* plugin_ = nullptr;

    int fftOrder_ = 11;
    int fftSize_ = 1 << 11;
    int numBins_ = (1 << 11) / 2;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window_;
    std::vector<float> readBuf_;
    std::vector<float> fftData_;
    std::vector<float> smoothedDb_;
    std::vector<float> peakDb_;

    float slopeDbPerOct_ = 4.5f;
    float smoothing_ = 0.5f;

    static constexpr float kMinDb = -100.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kPeakDecayDb = 0.6f;

    juce::ComboBox fftCombo_, slopeCombo_, speedCombo_, colourCombo_;
    juce::Label fftLabel_, slopeLabel_, speedLabel_, colourLabel_;

    juce::Point<int> mousePos_;
    bool mouseOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerUI)
};

}  // namespace magda::daw::ui
