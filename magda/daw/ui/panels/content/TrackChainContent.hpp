#pragma once

#include <BinaryData.h>

#include <map>
#include <vector>

#include "../../../core/GainStagingManager.hpp"
#include "../../../core/LinkModeManager.hpp"
#include "../../themes/MixerLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/DeviceInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/ChordAuditionControl.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
}
}  // namespace tracktion

namespace magda::daw::ui {

class RackComponent;
class NodeComponent;
class DeviceSlotComponent;
class ModsPanelComponent;
class MacroPanelComponent;
class ModulatorEditorPanel;
class MacroEditorPanel;

/**
 * @brief Track chain panel content
 *
 * Displays a mockup of the selected track's signal chain with
 * track info (name, M/S/gain/pan) at the right border.
 */
class TrackChainContent : public PanelContent,
                          public magda::TrackManagerListener,
                          public magda::SelectionManagerListener,
                          public magda::LinkModeManagerListener,
                          public magda::GainStagingListener,
                          private juce::Timer {
  public:
    TrackChainContent();
    ~TrackChainContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::TrackChain;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::TrackChain, "Track Chain", "Track signal chain", "Chain"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void onActivated() override;
    void onDeactivated() override;

    // Header bar integration (controls reparented into BottomPanel's header bar)
    bool wantsHeader() const override {
        return true;
    }

    int getOptimalPanelHeight(int windowHeight) const override;

    void populateHeader(juce::Component& headerBar) override;
    void depopulateHeader(juce::Component& headerBar) override;
    void layoutHeader(juce::Rectangle<int> headerBounds) override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void trackDevicesChanged(magda::TrackId trackId) override;
    void deviceModifiersChanged(magda::TrackId trackId) override;
    void modulationNamesChanged(magda::TrackId trackId) override;
    void macroValueChanged(magda::TrackId trackId, magda::ChainScope scope, int ownerId,
                           int macroIndex, float value) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;

    // LinkModeManagerListener
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

    // GainStagingListener
    void gainStagingModeChanged(magda::GainStagingMode mode, magda::TrackId trackId) override;

    // Selection state for plugin browser context menu
    bool hasSelectedTrack() const;
    bool hasSelectedChain() const;
    magda::TrackId getSelectedTrackId() const {
        return selectedTrackId_;
    }
    magda::RackId getSelectedRackId() const {
        return selectedRackId_;
    }
    magda::ChainId getSelectedChainId() const {
        return selectedChainId_;
    }

    // Add device commands
    void addDeviceToSelectedTrack(const magda::DeviceInfo& device);
    void addDeviceToSelectedChain(const magda::DeviceInfo& device);

  private:
    juce::Label noSelectionLabel_;
    juce::Label linkModeLabel_;     // Shows "LINK MODE" when mod/macro linking is active
    juce::Label gainStagingLabel_;  // Shows "GAIN STAGING" while a staging pass is active

    // Reflect the gain-staging mode onto the toolbar button (active tint + tooltip).
    void refreshGainStagingButton();

    // Finish the capture and hand it to the AI agent off the message thread,
    // applying the agent's gain decisions when it returns.
    void runAiGainStagingPass();
    bool aiProcessing_ = false;  // an AI pass is waiting on the agent

    // Header feedback for an AI pass: a blink timer drives the "GETTING AI
    // RESULTS" banner while waiting, then the agent's summary is shown briefly.
    struct LambdaTimer : public juce::Timer {
        std::function<void()> onTick;
        void timerCallback() override {
            if (onTick)
                onTick();
        }
    };
    LambdaTimer aiBlinkTimer_;
    bool aiBlinkOn_ = true;

    // Transient overlay showing the agent's reasoning after a pass; any click
    // dismisses it.
    std::unique_ptr<juce::Component> aiReasoningOverlay_;
    void showAiReasoning(const juce::String& text);

    // Header bar controls - LEFT side (action buttons)
    std::unique_ptr<magda::SvgButton> globalModsButton_;    // Toggle global modulators panel
    std::unique_ptr<magda::SvgButton> gainStagingButton_;   // Start/stop a gain-staging pass
    std::unique_ptr<magda::SvgButton> macroButton_;         // Toggle global macros panel
    std::unique_ptr<magda::SvgButton> addRackButton_;       // Add rack button
    std::unique_ptr<magda::SvgButton> treeViewButton_;      // Show chain tree dialog
    std::unique_ptr<magda::SvgButton> presetButton_;        // MAGDA track-chain presets menu
    std::unique_ptr<magda::SvgButton> oscToggleButton_;     // Toggle oscilloscope in post-fx
    std::unique_ptr<magda::SvgButton> specToggleButton_;    // Toggle spectrum analyzer in post-fx
    std::unique_ptr<magda::SvgButton> levelsToggleButton_;  // Toggle levels meter in post-fx
    std::unique_ptr<magda::SvgButton> postFxPanelButton_;   // Show/hide the post-fx panel

    // Add or remove the named analysis device in the selected track's post-fx
    // (osc/spectrum are unique per kind there), then refresh the toggle states.
    void togglePostFxAnalysisDevice(const juce::String& pluginId, const juce::String& displayName);
    void refreshAnalysisToggles();  // light osc/spec buttons when present in post-fx

  public:
    // The post-fx panel lives in BottomPanel; the toggle button lives here.
    // BottomPanel wires these: onPostFxPanelToggled fires when the user clicks
    // the button, and setPostFxPanelOpen reflects the panel's open state back
    // onto the button.
    std::function<void(bool open)> onPostFxPanelToggled;
    void setPostFxPanelOpen(bool open);

  private:
    bool postFxPanelOpen_ = false;  // mirrored panel state, for the button's lit look

    // Currently-loaded chain preset name for the selected track (empty when
    // none). Cleared on track selection change so each track gets a fresh
    // "Save" target — we don't track per-track preset state at the model
    // level, this is purely a UI convenience for the active track.
    juce::String currentPresetName_;

    void showPresetMenu();
    void showSaveTrackPresetDialog();
    void saveCurrentTrackPreset();
    void loadTrackPresetByName(const juce::String& presetName);

    // Header bar controls - RIGHT side (track info)
    juce::Label trackNameLabel_;
    SvgButton muteButton_{"mute", BinaryData::master_on_svg,
                          BinaryData::master_on_svgSize};  // Track mute
    // Master uses a speaker toggle (matching the inspector/mixer) instead of "M".
    // Dual-icon like those surfaces: audible = master_on, muted = master_off_1.
    SvgButton masterMuteButton_{"masterMute", BinaryData::master_on_svg,
                                BinaryData::master_on_svgSize, BinaryData::master_off_1_svg,
                                BinaryData::master_off_1_svgSize};
    SvgButton soloButton_{"solo", BinaryData::solo_off_svg,
                          BinaryData::solo_off_svgSize};  // Track solo
    // Chord track mirrors its header: audition (mute) speaker + input monitor.
    std::unique_ptr<magda::ChordAuditionControl> chordSpeakerButton_;
    juce::TextButton monitorButton_;
    magda::DraggableValueLabel volumeLabel_{magda::DraggableValueLabel::Format::Decibels};
    magda::DraggableValueLabel panLabel_{magda::DraggableValueLabel::Format::Pan};
    std::unique_ptr<magda::SvgButton> chainBypassButton_;  // On/off - bypasses entire track chain

    // Global mods/macros panel visibility
    bool globalModsVisible_ = false;
    bool globalMacrosVisible_ = false;

    // Global mods panel (track-level modulators)
    std::unique_ptr<ModsPanelComponent> globalModsPanel_;
    std::unique_ptr<ModulatorEditorPanel> globalModEditorPanel_;
    int selectedGlobalModIndex_ = -1;
    bool globalModEditorVisible_ = false;

    // Global macros panel (track-level macros)
    std::unique_ptr<MacroPanelComponent> globalMacrosPanel_;
    std::unique_ptr<MacroEditorPanel> globalMacroEditorPanel_;
    int selectedGlobalMacroIndex_ = -1;
    bool globalMacroEditorVisible_ = false;

    void initGlobalModsPanel();
    void initGlobalMacrosPanel();
    void updateGlobalModsPanel();
    void updateGlobalMacrosPanel();
    void refreshVisibleModulationPanels();
    void showGlobalModEditor(int modIndex);
    void hideGlobalModEditor();
    void showGlobalMacroEditor(int macroIndex);
    void hideGlobalMacroEditor();

    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;
    magda::RackId selectedRackId_ = magda::INVALID_RACK_ID;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;

    // Custom look and feel for sliders
    magda::MixerLookAndFeel mixerLookAndFeel_;

    void updateFromSelectedTrack();
    void hideHeaderControls();
    void rebuildNodeComponents();
    int calculateTotalContentWidth() const;
    void layoutChainContent();
    void onAddDeviceClicked();
    void scrollToEndAsync();

    // Viewport for horizontal scrolling of chain content (with zoom support)
    class ZoomableViewport;
    std::unique_ptr<ZoomableViewport> chainViewport_;
    class ChainContainer;
    std::unique_ptr<ChainContainer> chainContainer_;
    juce::TextButton addDeviceButton_;

    // All node components in signal flow order (devices and racks unified)
    std::vector<std::unique_ptr<NodeComponent>> nodeComponents_;

    static constexpr int ARROW_WIDTH = 4;  // Small gap between device slots
    static constexpr int SLOT_SPACING = 8;
    static constexpr int DRAG_LEFT_PADDING = 12;  // Padding during drag for drop indicator
    static constexpr int APPEND_ZONE_WIDTH = 56;

    // Chain selection handling (internal)
    void onChainSelected(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);

    // Device selection management
    magda::DeviceId selectedDeviceId_ = magda::INVALID_DEVICE_ID;
    void onDeviceSlotSelected(magda::DeviceId deviceId);
    void clearDeviceSelection();

    static constexpr int MODS_PANEL_WIDTH = 160;
    static constexpr int MIN_CHAIN_HEIGHT = 280;  // Minimum content height before scrolling

    // Horizontal zoom
    float zoomLevel_ = 1.0f;
    static constexpr float MIN_ZOOM = 0.5f;
    static constexpr float MAX_ZOOM = 2.0f;
    static constexpr float ZOOM_STEP = 0.1f;
    void setZoomLevel(float zoom);
    int getScaledWidth(int width) const;

    // Zoom drag state (Alt+click-drag)
    bool isZoomDragging_ = false;
    int zoomDragStartX_ = 0;
    float zoomStartLevel_ = 1.0f;

    // Drag-to-reorder state
    NodeComponent* draggedNode_ = nullptr;
    int dragOriginalIndex_ = -1;
    int dragInsertIndex_ = -1;
    juce::Image dragGhostImage_;
    juce::Point<int> dragMousePos_;

    // External drop state (plugin drops from browser)
    int dropInsertIndex_ = -1;
    bool scrollToEndAfterNextDeviceChange_ = false;
    bool suppressNextImplicitScrollToEnd_ = false;

    // State preservation during rebuild - preserves ALL nodes' states
    std::map<juce::String, bool> savedCollapsedStates_;           // path -> collapsed
    std::map<juce::String, magda::ChainId> savedExpandedChains_;  // rackPath -> expanded chainId
    std::map<juce::String, bool> savedParamPanelStates_;          // path -> paramPanelVisible
    std::map<juce::String, int> savedCustomUITabStates_;          // path -> custom UI tab index
    std::map<juce::String, std::vector<tracktion::engine::Plugin*>>
        savedDrumPadCollapsedPlugins_;  // path -> collapsed plugin ptrs
    void saveNodeStates();
    void restoreNodeStates();

    // Helper methods for drag-to-reorder
    int findNodeIndex(NodeComponent* node) const;
    int calculateInsertIndex(int mouseX) const;
    int calculateIndicatorX(int index) const;
    int calculateAppendZoneX() const;

    // Timer callback for detecting stale drop state
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChainContent)
};

}  // namespace magda::daw::ui
