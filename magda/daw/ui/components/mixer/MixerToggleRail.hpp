#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../common/SvgButton.hpp"
#include "core/MixAnalysisService.hpp"

namespace magda {

/**
 * @brief Vertical icon rail on the left edge of MixerView.
 *
 * One toggle per optional mixer pane (sends, I/O routing, monitor, mini
 * oscilloscope, mini spectrum, mini FX chain). State is persisted in
 * Config (mixerShowSends, mixerShowRouting, etc.) — `onToggleChanged` fires
 * after Config is updated so the view can relayout.
 */
class MixerToggleRail : public juce::Component, public MixAnalysisService::Listener {
  public:
    MixerToggleRail();
    ~MixerToggleRail() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // MixAnalysisService::Listener -- reflect busy/capturing on the analyze button.
    void mixAnalysisChanged() override;

    static constexpr int RAIL_WIDTH = 36;

    // Fired after a toggle is flipped and Config has been updated.
    std::function<void()> onToggleChanged;

  private:
    std::unique_ptr<SvgButton> analyzeButton_;  // whole-mix analysis (action, not a toggle)
    std::unique_ptr<SvgButton> sendsButton_;
    std::unique_ptr<SvgButton> routingButton_;
    std::unique_ptr<SvgButton> monitorButton_;
    std::unique_ptr<SvgButton> oscilloscopeButton_;
    std::unique_ptr<SvgButton> spectrumButton_;
    std::unique_ptr<SvgButton> fxChainButton_;

    void setupButton(std::unique_ptr<SvgButton>& btn, const juce::String& name, const char* svgData,
                     size_t svgSize, const juce::String& tooltip, bool initialState,
                     std::function<void(bool)> setter);
    static void applyToggleState(SvgButton* btn, bool on);

    // Analyze action: opens the measured-findings modal (offline render); the
    // button reflects the service's busy state.
    void setupAnalyzeButton();
    void openModal(MixAnalysisService::Mode mode);
    void updateAnalyzeButtonMode();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerToggleRail)
};

}  // namespace magda
