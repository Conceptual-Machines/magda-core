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
 * @brief Custom faceplate for Nimbus (magda_clouds), the granular texture
 *        processor ported from Mutable Instruments Clouds.
 *
 * Three full-height panels side by side (#3201): PARAMETERS, Position Density Size Texture
 * Pitch over the blend routes; FREEZE and PLAYBACK MODE; and the GRAIN CLOUD, an animated
 * record-buffer view with the grain window and cloud, which the sidebar can hide.
 *
 * The grain-buffer view is decorative for this first iteration (no signal-in
 * tap yet): a synthetic waveform with the grain window driven by Position/Size
 * and an animated grain cloud driven by Density/Texture. Continuous controls
 * are LinkableTextSliders; Mode and Freeze are drawn as clickable segments.
 */
class NimbusUI : public juce::Component, private juce::Timer {
  public:
    NimbusUI();
    ~NimbusUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    // Bind live telemetry so the grain-buffer view shows the real input.
    void setTelemetrySource(std::shared_ptr<NimbusTelemetrySource> telemetry);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    /// Show or hide the GRAIN CLOUD; the device's width follows preferredWidth().
    void setCloudShown(bool shown);
    int preferredWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void lookAndFeelChanged() override;

  private:
    // Param indices — must match MutableCloudsPlugin::ParamIndex.
    enum Param {
        kPosition = 0,
        kSize,
        kPitch,
        kDensity,
        kTexture,
        kDryWet,
        kSpread,
        kFeedback,
        kReverb,
        kMode,
        kFreeze,
        kNumParams
    };

    static constexpr int kNumModes = 4;

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
        juce::Rectangle<int> tile;  ///< The painted cell behind the caption and value.
    };

    void timerCallback() override;
    void layoutRow(juce::Rectangle<int> row, const std::vector<int>& indices);
    void paintBuffer(juce::Graphics& g);

    std::array<Control, kNumParams> controls_;

    std::shared_ptr<NimbusTelemetrySource> telemetry_;
    int curMode_ = 0;
    bool freeze_ = false;
    float animPhase_ = 0.0f;

    juce::Rectangle<int> grainArea_, bufferVizArea_, paramsArea_, ctrlArea_;
    bool cloudShown_ = true;
    juce::Rectangle<int> freezeBtn_, modeLabelRect_, blendLabelRect_;
    std::array<juce::Rectangle<int>, kNumModes> modeBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NimbusUI)
};

}  // namespace magda::daw::ui
