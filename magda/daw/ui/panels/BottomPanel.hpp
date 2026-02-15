#pragma once

#include <functional>
#include <memory>

#include "../state/TimelineController.hpp"
#include "TabbedPanel.hpp"
#include "core/ClipManager.hpp"
#include "core/TrackManager.hpp"
#include "utils/ScopedListener.hpp"

namespace magda {

class DraggableValueLabel;
class SvgButton;

/**
 * @brief Bottom panel with automatic content switching based on selection
 *
 * Automatically shows:
 * - Empty content when nothing is selected
 * - TrackChain when a track is selected (no clip)
 * - PianoRoll when a MIDI clip is selected
 * - WaveformEditor when an audio clip is selected
 * - Tab bar with "Piano Roll" | "Drum Grid" for any MIDI clip
 */
class BottomPanel : public daw::ui::TabbedPanel,
                    public ClipManagerListener,
                    public TrackManagerListener,
                    public TimelineStateListener {
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
    void clipPropertyChanged(ClipId clipId) override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // TimelineStateListener
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

  protected:
    juce::Rectangle<int> getCollapseButtonBounds() override;
    juce::Rectangle<int> getTabBarBounds() override;
    juce::Rectangle<int> getContentBounds() override;

  private:
    void updateContentBasedOnSelection();

    // Editor tab icons for switching between Piano Roll and Drum Grid
    std::unique_ptr<SvgButton> pianoRollTab_;
    std::unique_ptr<SvgButton> drumGridTab_;
    bool showEditorTabs_ = false;
    bool updatingTabs_ = false;  // Guard against re-entrancy
    static constexpr int EDITOR_TAB_HEIGHT = 28;
    static constexpr int SIDEBAR_WIDTH = 32;

    // Persisted user preference: which MIDI editor view
    // 0 = Piano Roll (default), 1 = Drum Grid
    int lastEditorTabChoice_ = 0;
    ClipId lastEditorClipId_ = INVALID_CLIP_ID;  // Track which clip we auto-defaulted for

    void onEditorTabChanged(int tabIndex);

    // Header controls (visible when showEditorTabs_ is true)
    std::unique_ptr<juce::TextButton> timeModeButton_;
    std::unique_ptr<DraggableValueLabel> gridNumeratorLabel_;
    std::unique_ptr<juce::Label> gridSlashLabel_;
    std::unique_ptr<DraggableValueLabel> gridDenominatorLabel_;
    std::unique_ptr<juce::TextButton> autoGridButton_;
    std::unique_ptr<juce::TextButton> snapButton_;

    // Header control state
    bool relativeTimeMode_ = false;
    bool isAutoGrid_ = true;
    int gridNumerator_ = 1;
    int gridDenominator_ = 4;
    bool isSnapEnabled_ = true;

    // RAII listener registration — handles late TimelineController availability
    ScopedListener<TimelineController, TimelineStateListener> timelineListenerGuard_{this};

    void setupHeaderControls();
    void applyTimeModeToContent();
    void syncGridStateFromTimeline();
    void syncGridControlsFromContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BottomPanel)
};

}  // namespace magda
