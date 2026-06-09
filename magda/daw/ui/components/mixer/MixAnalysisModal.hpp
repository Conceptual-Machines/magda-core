#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MixAnalysisService.hpp"

namespace magda {

/**
 * @brief CallOutBox content showing the MEASURED mix analysis (#886).
 *
 * Pre-agent data only: a per-track levels table + a list of frequency
 * collisions. Drives MixAnalysisService for the chosen mode -- shows cached
 * findings instantly, a spinner while an offline render gathers, or a
 * "play the mix / Stop" prompt for a live capture. The LLM conversation lives
 * in the console, not here. Dismisses on click-away (CallOutBox).
 */
class MixAnalysisModal : public juce::Component, public MixAnalysisService::Listener {
  public:
    /// `mode` selects which gather to show/run. If the service already has cached
    /// data for that mode it is shown immediately; otherwise the run is started.
    explicit MixAnalysisModal(MixAnalysisService::Mode mode);
    ~MixAnalysisModal() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;  // block input once shown + rendering

    // MixAnalysisService::Listener
    void mixAnalysisChanged() override;

  private:
    enum class State { Idle, Loading, Listening, Results, Error };

    void refresh();        // recompute State from the service and relayout
    void renderResults();  // fill the text view from the cached Input
    State deriveState() const;
    // While an offline render runs it commandeers the live edit, so block ALL
    // input (transport, mixer) until it finishes or is stopped -- mirrors how
    // export renders behind a modal window. Enter/exit modal state to match.
    void updateBlocking();

    MixAnalysisService::Mode mode_;
    State state_ = State::Loading;

    // Theme look-and-feel (theme fonts for the button/labels). Declared first so
    // it outlives the child components that reference it.
    std::unique_ptr<juce::LookAndFeel> lookAndFeel_;

    juce::Label titleLabel_;
    juce::Label statusLabel_;         // progress / "play the mix" / error line
    juce::TextEditor findings_;       // read-only measured-findings table
    juce::TextButton primaryButton_;  // Stop (running) / Re-run (results)

    // Spinner (loading state): a rotating arc driven by a lightweight timer.
    struct Spinner : public juce::Component, private juce::Timer {
        float angle = 0.0f;
        Spinner();
        ~Spinner() override;
        void paint(juce::Graphics& g) override;
        void timerCallback() override;
    };
    Spinner spinner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAnalysisModal)
};

}  // namespace magda
