// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "custom_ui/TelemetrySources.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Faceplate for Spectral FX (#1263): a scrolling spectrogram of the device's output over
 *        one row of its controls.
 *
 * The spectrogram is the device's own STFT, read from its telemetry ring, so it shows exactly
 * what the effects did. A level strip on the right marks the gate threshold.
 */
class SpectralFxUI : public juce::Component, private juce::Timer {
  public:
    SpectralFxUI();
    ~SpectralFxUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();
    void setTelemetrySource(std::shared_ptr<SpectralFxTelemetrySource> telemetry);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    // Mix sits on the slot's side strip, not here.
    static constexpr int kControlCount = 5;
    static constexpr int kHistoryColumns = 256;

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    void timerCallback() override;
    void pullColumns();
    void paintSpectrogram(juce::Graphics& g);
    void paintLevelStrip(juce::Graphics& g);
    void applyColours();

    std::array<Control, kControlCount> controls_;
    bool configured_ = false;

    std::shared_ptr<SpectralFxTelemetrySource> telemetry_;
    std::size_t readPosition_ = 0;
    juce::Image history_;
    std::vector<float> columns_;

    juce::Rectangle<int> plotArea_, stripArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFxUI)
};

}  // namespace magda::daw::ui
