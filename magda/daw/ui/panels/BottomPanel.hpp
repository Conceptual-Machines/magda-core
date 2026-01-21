#pragma once

#include <functional>
#include <memory>

#include "TabbedPanel.hpp"
#include "core/ClipManager.hpp"
#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief Bottom panel with automatic content switching based on selection
 *
 * Automatically shows:
 * - Empty content when nothing is selected
 * - TrackChain when a track is selected (no clip)
 * - PianoRoll when a MIDI clip is selected
 * - WaveformEditor when an audio clip is selected
 */
class BottomPanel : public daw::ui::TabbedPanel,
                    public ClipManagerListener,
                    public TrackManagerListener {
  public:
    BottomPanel();
    ~BottomPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Legacy API for compatibility
    void setCollapsed(bool collapsed);

    // ClipManagerListener
    void clipsChanged() override;
    void clipSelectionChanged(ClipId clipId) override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

  protected:
    juce::Rectangle<int> getCollapseButtonBounds() override;
    juce::Rectangle<int> getTabBarBounds() override;
    juce::Rectangle<int> getContentBounds() override;

  private:
    void updateContentBasedOnSelection();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BottomPanel)
};

}  // namespace magda
