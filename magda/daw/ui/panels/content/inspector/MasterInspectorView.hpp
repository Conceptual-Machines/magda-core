// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../../../components/common/CollapsibleSectionHeader.hpp"
#include "../../../components/common/DraggableValueLabel.hpp"
#include "../../../components/common/SvgButton.hpp"
#include "../../../components/mixer/LevelMeter.hpp"
#include "../../../components/mixer/MiniChainList.hpp"

namespace magda {
class AudioEngine;
}

namespace magda::daw::ui {

/**
 * @brief The inspector for the master track: header, Output (volume and live peak meter),
 *        FX (the compact master chain) and Latency (the chain's total and its largest
 *        contributors).
 *
 * Shown by TrackInspector over its own controls while the master is selected.
 */
class MasterInspectorView : public juce::Component, private juce::Timer {
  public:
    MasterInspectorView();
    ~MasterInspectorView() override;

    void setAudioEngine(magda::AudioEngine* engine) {
        audioEngine_ = engine;
    }

    /// Re-read the master channel and chain: after any master or device change.
    void refresh();

    /// Re-entrant from an FX row's own bypass toggle: repaints, never rebuilds.
    void syncBypass(magda::DeviceId deviceId, bool bypassed) {
        fxList_.syncBypass(deviceId, bypassed);
    }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    struct Contributor {
        juce::String name;
        double ms = 0.0;
    };

    void timerCallback() override;
    void refreshLatency();
    void layoutSections();
    void applyColours();

    magda::AudioEngine* audioEngine_ = nullptr;

    juce::Label nameLabel_;
    juce::Label summaryLabel_;
    std::unique_ptr<SvgButton> muteButton_;

    CollapsibleSectionHeader outputHeader_{"Output", "master.output"};
    juce::Label volumeCaption_;
    std::unique_ptr<magda::DraggableValueLabel> volume_;
    magda::LevelMeter meter_;
    juce::Label peakLabel_;

    CollapsibleSectionHeader fxHeader_{"FX", "master.fx"};
    juce::Label noFxLabel_;
    juce::Viewport fxViewport_;
    magda::MiniChainList fxList_;

    CollapsibleSectionHeader latencyHeader_{"Latency", "master.latency"};
    double totalMs_ = 0.0;
    int totalSamples_ = 0;
    double sampleRate_ = 0.0;
    std::vector<Contributor> contributors_;

    // Painted areas placed by resized().
    juce::Rectangle<int> headerArea_;
    juce::Rectangle<int> meterScaleArea_;
    juce::Rectangle<int> latencyTotalArea_;
    juce::Rectangle<int> contributorsArea_;
    std::vector<int> separatorYs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterInspectorView)
};

}  // namespace magda::daw::ui
