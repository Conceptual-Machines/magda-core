#pragma once

#include <functional>
#include <memory>

#include "../state/TimelineController.hpp"
#include "TabbedPanel.hpp"
#include "core/ClipManager.hpp"
#include "core/PluginPreferences.hpp"
#include "core/TrackManager.hpp"
#include "utils/ScopedListener.hpp"

namespace magda {

class DraggableValueLabel;
class SvgButton;

namespace daw::ui {
class AudioClipPropertiesContent;
class ChordPanelContent;
class PostFxPanelContent;
}  // namespace daw::ui

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
                    public juce::DragAndDropTarget,
                    public ClipManagerListener,
                    public TrackManagerListener,
                    public PluginPreferences::Listener,
                    public TimelineStateListener {
  public:
    BottomPanel();
    ~BottomPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Legacy API for compatibility
    void setCollapsed(bool collapsed);

    // Get the current content type being displayed
    daw::ui::PanelContentType getActiveContentType() const;

    // Callback for double-click on header — requests optimal panel height
    std::function<void()> onHeaderDoubleClick;

    // Callback when the piano roll's fullscreen toggle is clicked (issue #1282).
    // MainComponent assigns this to actually flip layout state.
    std::function<void()> onFullscreenToggleRequested;

    // Reflect the current fullscreen state on the piano roll's toggle icon.
    // Called by MainComponent after togglePianoRollFullscreen() flips state.
    void setPianoRollFullscreenActive(bool active);

    // ClipManagerListener
    void clipsChanged() override;
    void clipSelectionChanged(ClipId clipId) override;
    void clipPropertyChanged(ClipId clipId) override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // PluginPreferences::Listener
    void drumGridPreferenceChanged(const juce::String& pluginIdentifier) override;

    // TimelineStateListener
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // DragAndDropTarget implementation (plugin drops)
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

  protected:
    juce::Rectangle<int> getTabBarBounds() override;
    juce::Rectangle<int> getContentBounds() override;
    void onContentWillSwitch(daw::ui::PanelContent* outgoing,
                             daw::ui::PanelContent* incoming) override;

  private:
    void updateContentBasedOnSelection();

    bool shouldShowHeaderFor(daw::ui::PanelContent* content) const;
    void syncHeaderVisibility(daw::ui::PanelContent* content);

    // Editor tab icons for switching between Piano Roll and Drum Grid
    std::unique_ptr<SvgButton> pianoRollTab_;
    std::unique_ptr<SvgButton> drumGridTab_;

    // Fullscreen toggle for MIDI editor views (issue #1282).
    // Visible whenever the active content is PianoRoll or DrumGridClipView.
    std::unique_ptr<SvgButton> fullscreenToggle_;

    // Multi-track overlay selector (ghost notes from other tracks, #1281).
    // Visible alongside the editor tabs; lit while any overlay track is on.
    std::unique_ptr<SvgButton> overlayTracksButton_;
    void updateOverlayTracksButtonState();

    // Centralised header bar — content types populate it via populateHeader()
    class HeaderBar;
    std::unique_ptr<HeaderBar> headerBar_;

    void addMidiControlsToHeader();
    void removeMidiControlsFromHeader();
    void hideMidiHeaderControls();
    void layoutMidiHeaderControls(juce::Rectangle<int> headerBounds);

    bool showEditorTabs_ = false;
    bool updatingTabs_ = false;  // Guard against re-entrancy
    static constexpr int SIDEBAR_WIDTH = 32;

    // Persisted user preference: which MIDI editor view
    // 0 = Piano Roll (default), 1 = Drum Grid
    int lastEditorTabChoice_ = 0;
    ClipId lastEditorClipId_ = INVALID_CLIP_ID;  // Track which clip we auto-defaulted for

    void onEditorTabChanged(int tabIndex);
    void showDrumGridTabContextMenu(juce::Point<int> screenPos);

    // Mouse listener attached to drumGridTab_ to forward right-clicks to the
    // context-menu handler. SvgButton's onClick is left-click only.
    std::unique_ptr<juce::MouseListener> drumGridTabRightClick_;

    // Header controls (visible when showEditorTabs_ is true)
    std::unique_ptr<juce::TextButton> timeModeButton_;
    std::unique_ptr<DraggableValueLabel> gridNumeratorLabel_;
    std::unique_ptr<juce::Label> gridSlashLabel_;
    std::unique_ptr<DraggableValueLabel> gridDenominatorLabel_;
    std::unique_ptr<juce::TextButton> autoGridButton_;
    std::unique_ptr<juce::TextButton> snapButton_;
    std::unique_ptr<SvgButton> sliceButton_;
    std::unique_ptr<SvgButton> bendButton_;

    // Header control state
    bool relativeTimeMode_ = false;
    bool isAutoGrid_ = true;
    int gridNumerator_ = 1;
    int gridDenominator_ = 4;
    bool isSnapEnabled_ = true;

    // RAII listener registration — handles late TimelineController availability
    ScopedListener<TimelineController, TimelineStateListener> timelineListenerGuard_{this};

    bool showPluginDropOverlay_ = false;

    // Cached so a piano roll instance created later picks up the right icon.
    bool pianoRollFullscreenActive_ = false;

    // Audio clip properties side panel (right side, resizable)
    std::unique_ptr<daw::ui::AudioClipPropertiesContent> audioPropsPanel_;
    bool showPropsPanel_ = false;
    bool propsPanelCollapsed_ = false;
    int propsPanelWidth_ = 350;
    static constexpr int PROPS_MIN_WIDTH = 300;
    static constexpr int PROPS_MAX_WIDTH = 500;
    static constexpr int PROPS_COLLAPSE_THRESHOLD = 60;
    static constexpr int RESIZE_HANDLE_SIZE = 3;

    // Resize handle and collapse button for properties panel
    class PropsResizeHandle;
    std::unique_ptr<PropsResizeHandle> propsResizer_;
    std::unique_ptr<magda::SvgButton> propsCollapseButton_;

    // Chord analysis side panel (right side, for MIDI device tracks)
    std::unique_ptr<daw::ui::ChordPanelContent> chordPanel_;
    bool showChordPanel_ = false;
    bool chordPanelCollapsed_ = false;
    int chordPanelWidth_ = 450;
    static constexpr int CHORD_MIN_WIDTH = 300;
    static constexpr int CHORD_MAX_WIDTH = 600;

    std::unique_ptr<PropsResizeHandle> chordResizer_;
    std::unique_ptr<magda::SvgButton> chordCollapseButton_;

    // Post-FX side panel (right side, shown when a track is selected AND the
    // user has opened it via the TrackChain header toggle).
    std::unique_ptr<daw::ui::PostFxPanelContent> postFxPanel_;
    bool showPostFxPanel_ = false;  // derived: TrackChain content && postFxOpen_
    bool postFxOpen_ = false;       // user/auto open state (header toggle)
    // User-resized width, clamped to [POSTFX_MIN_WIDTH, 70% of the panel]. It
    // can shrink well below half and grow up to 70% (the FX chain keeps >= 30%).
    // -1 means "not yet sized" -> opens at ~35% of the panel.
    int postFxPanelWidth_ = -1;
    static constexpr int POSTFX_MIN_WIDTH = 240;

    std::unique_ptr<PropsResizeHandle> postFxResizer_;

    void ensureChordPanelCreated();
    void ensurePostFxPanelCreated();
    int effectivePostFxWidth() const;  // postFxPanelWidth_ clamped to [min, 70%]
    // Recompute showPostFxPanel_ and (re)create + point the panel at the track.
    // allowAutoReveal opens it when the track already has post-fx devices (used
    // on selection, not when the user explicitly toggles).
    void updatePostFxPanel(bool onTrackChain, TrackId selectedTrack, bool allowAutoReveal);
    void syncPostFxToggleButton();  // wire + light the TrackChain header toggle
    void setPostFxOpen(bool open);  // header-toggle handler
    void setupHeaderControls();
    void applyTimeModeToContent();
    void syncGridStateFromTimeline();
    void syncGridControlsFromContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BottomPanel)
};

}  // namespace magda
