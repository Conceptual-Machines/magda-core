#pragma once

#include <BinaryData.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "ChainPanel.hpp"
#include "core/ChainNodePath.hpp"
#include "drum_grid/PadLayerRow.hpp"
#include "layout/DashedAddButton.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

/**
 * @brief The Drum Grid's body in the v1 shell: [rail | pads | pad editor | chain] over a footer.
 *
 * The selected pad's chain shows in the rack's chain view. Pads take sample and plugin drops;
 * the rail toggles the editor and swaps the pads for the pad list. A pad with several layers
 * lists them above the chain view, which shows the selected layer (#3007).
 */
class DrumGridUI : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget,
                   public juce::Timer {
  public:
    static constexpr int kPadsPerPage = 16;
    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 4;
    static constexpr int kTotalPads = 64;
    static constexpr int kNumPages = kTotalPads / kPadsPerPage;
    static constexpr int kPluginParamSlots = 16;

    static constexpr int kRailWidth = 38;
    static constexpr int kEditorWidth = 252;
    static constexpr int kMinChainWidth = 264;
    static constexpr int kFooterHeight = 40;

    DrumGridUI();
    ~DrumGridUI() override;

    //==============================================================================
    // Data update

    /** Update cached info for a single pad. Called from DeviceSlotComponent::updateCustomUI. */
    void updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                       float levelDb, float pan, int chainIndex = -1, bool bypassed = false,
                       int busOutput = 0);

    /** Set which pad is selected and populate the detail panel. */
    void setSelectedPad(int padIndex);

    /** Get the currently selected pad index. */
    int getSelectedPad() const {
        return selectedPad_;
    }

    //==============================================================================
    // Callbacks (wired by DeviceSlotComponent)

    /** Called when a sample file is dropped onto a pad. (padIndex, file) */
    std::function<void(int, const juce::File&)> onSampleDropped;

    /** Called when Clear button is clicked for the selected pad. (padIndex) */
    std::function<void(int)> onClearRequested;

    /** Called when pad level changes. (padIndex, levelDb) */
    std::function<void(int, float)> onPadLevelChanged;

    /** Called when pad pan changes. (padIndex, pan -1..1) */
    std::function<void(int, float)> onPadPanChanged;

    /** Which fader drag the level and pan callbacks currently belong to.
        Bumped when a drag ends, so consecutive gestures on the same fader are
        separate undo steps rather than one merged run (#2211). */
    int getFaderGesture() const {
        return faderGesture_;
    }

    /** Called when pad mute changes. (padIndex, muted) */
    std::function<void(int, bool)> onPadMuteChanged;

    /** Called when pad solo changes. (padIndex, soloed) */
    std::function<void(int, bool)> onPadSoloChanged;

    /** Called when a plugin is dropped onto a pad. (padIndex, DynamicObject with plugin info) */
    std::function<void(int, const juce::DynamicObject&)> onPluginDropped;

    /** Called when the user explicitly asks to analyse a pad sample role. (padIndex) */
    std::function<void(int)> onAnalyzePadRoleRequested;

    /** Called when a pad is dragged and dropped onto another pad. (sourcePad, targetPad) */
    std::function<void(int, int)> onPadsSwapped;

    /** Called when pad output bus changes. (padIndex, busIndex) */
    std::function<void(int, int)> onPadOutputChanged;

    /** Called when play button is pressed/released on a pad. (padIndex, isNoteOn) */
    std::function<void(int, bool)> onNotePreview;

    /** @brief A pad's switches, faders and output as the model holds them. */
    struct PadMix {
        float level = 0.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
        int busOutput = 0;
    };

    /// Read at the poll rate, so a pad fader moved elsewhere (a mixer
    /// sub-channel) shows here. Nothing for a pad with no chain.
    std::function<std::optional<PadMix>(int padIndex)> getPadMix;

    /// Whether a pad has sounded since it was last asked. Read at the poll rate.
    std::function<bool(int padIndex)> consumePadTrigger;

    /// Where a change to the detail panel's collapsed state is kept.
    std::function<void(bool collapsed)> onDetailCollapsedChanged;

    /** @brief Show the detail panel as the model keeps it, without reporting a change. */
    void restoreDetailCollapsed(bool collapsed);

    /** Called when layout changes (e.g., chains panel toggled) so parent can resize. */
    std::function<void()> onLayoutChanged;

    /// The path of a pad's layer, its first for an invalid id; invalid while the pad has none.
    std::function<magda::ChainNodePath(int padIndex, magda::ChainId layerId)> getPadChainPath;

    /// A pad's layers as the model holds them, read whenever the pad is shown.
    std::function<std::vector<PadLayerView>(int padIndex)> getPadLayers;

    std::function<void(int padIndex)> onAddLayerRequested;
    std::function<void(int padIndex, magda::ChainId layerId)> onRemoveLayerRequested;
    std::function<void(int padIndex, magda::ChainId layerId, bool mute, bool solo, bool bypassed)>
        onLayerSwitchesChanged;
    /// Live, per move; coalesces into one undo step per fader gesture.
    std::function<void(magda::ChainId layerId, float volumeDb, float pan)> onLayerMixChanged;
    /// Once per gesture: a zone change recompiles the pad.
    std::function<void(magda::ChainId layerId, const magda::ChainZones& zones)> onLayerZonesChanged;

    /** Called by the add slot of a pad with no chain yet. (padIndex) */
    std::function<void(int)> onAddDeviceRequested;

    /** @brief Show the selected pad's chain again after the model changed it. */
    void refreshPadChain();

    /** Width the panels want: rail, pads, the editor when open, and the chain. */
    int getPreferredContentWidth() const;

    //==============================================================================
    // Component overrides
    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // DragAndDropTarget (for plugin drops)
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

  private:
    //==============================================================================
    /** Inner component representing a single pad button in the grid. */
    class PadButton : public juce::Component {
      public:
        PadButton();

        void setPadIndex(int index);
        void setNoteName(const juce::String& name);
        void setSampleName(const juce::String& name);
        void setSelected(bool selected);
        void setHasSample(bool has);
        void setMuted(bool muted);
        void setSoloed(bool soloed);
        void setTriggered(bool triggered);
        void setStripeColour(juce::Colour colour);

        std::function<void(int)> onClicked;
        std::function<void(int, bool)> onNotePreview;  // (padIndex, isNoteOn)
        std::function<void(int, juce::Point<int>)> onRightClicked;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;

      private:
        int padIndex_ = 0;
        juce::Colour stripe_;
        bool hovered_ = false;
        juce::String noteName_;
        juce::String sampleName_;
        bool selected_ = false;
        bool hasSample_ = false;
        bool muted_ = false;
        bool soloed_ = false;
        bool triggered_ = false;
        bool playPressed_ = false;
        std::unique_ptr<magda::SvgButton> playButton_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadButton)
    };

    //==============================================================================
    // Cached pad data (populated from chains by DeviceSlotComponent)
    struct PadInfo {
        juce::String sampleName;
        bool mute = false;
        bool solo = false;
        bool bypassed = false;
        float level = 0.0f;
        float pan = 0.0f;
        int chainIndex = -1;  // Index of the chain covering this pad, or -1 if empty
        int busOutput = 0;    // 0 = Main, 1+ = multi-out bus
    };

    std::array<PadInfo, kTotalPads> padInfos_;

    // Process-wide and monotonic. A token local to one DrumGridUI restarts at
    // zero every time the component is rebuilt -- reopening the device UI,
    // switching tracks -- and none of that is an undoable action, so the last
    // fader command can still be on top of the undo stack when the first drag
    // in the new UI arrives. Reusing the token would fold two sessions into
    // one step (#2211).
    static std::atomic<int> nextFaderGesture_;
    int faderGesture_ = nextFaderGesture_.fetch_add(1);
    int selectedPad_ = 0;
    int currentPage_ = 0;

    // Pad grid
    std::array<PadButton, kPadsPerPage> padButtons_;

    // Rail
    juce::TextButton editorToggle_{"i"};

    // Pad editor
    enum class EditorTab { Velocity, Key, Volume };
    EditorTab editorTab_ = EditorTab::Volume;
    bool detailCollapsed_ = false;
    magda::DraggableValueLabel velocityLowControl_{magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel velocityHighControl_{magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel velocityFadeLowControl_{magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel velocityFadeHighControl_{
        magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel keyLowControl_{magda::DraggableValueLabel::Format::MidiNote};
    magda::DraggableValueLabel keyHighControl_{magda::DraggableValueLabel::Format::MidiNote};
    magda::DraggableValueLabel keyFadeLowControl_{magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel keyFadeHighControl_{magda::DraggableValueLabel::Format::Integer};
    juce::TextButton roundRobinButton_;
    bool zoneDragging_ = false;
    magda::DraggableValueLabel levelControl_{magda::DraggableValueLabel::Format::Decibels};
    magda::DraggableValueLabel panControl_{magda::DraggableValueLabel::Format::Pan};
    juce::TextButton outputButton_;
    bool faderDragging_ = false;

    // Layers of the selected pad, and which one the chain view shows
    std::vector<PadLayerView> layers_;
    magda::ChainId selectedLayer_ = magda::INVALID_CHAIN_ID;
    std::vector<std::unique_ptr<PadLayerRow>> layerRows_;
    DashedAddButton addLayerButton_{"Add layer"};

    // Chain
    ChainPanel padChainView_;
    DashedAddButton emptyAddButton_;
    magda::SvgButton chainMuteButton_{"mute", BinaryData::master_on_svg,
                                      BinaryData::master_on_svgSize};
    magda::SvgButton chainSoloButton_{"solo", BinaryData::solo_svg, BinaryData::solo_svgSize};

    // Footer
    std::unique_ptr<juce::ArrowButton> prevPageButton_;
    std::unique_ptr<juce::ArrowButton> nextPageButton_;

    // Areas laid out in resized() and painted in paint()
    juce::Rectangle<int> railArea_, padsArea_, editorArea_, chainArea_, footerArea_;
    juce::Rectangle<int> editorHeaderArea_, chainHeaderArea_, pageTextArea_;
    juce::Rectangle<int> levelLabelArea_, panLabelArea_, outputLabelArea_;
    juce::Rectangle<int> tabsArea_, zoneRangeLabelArea_, zoneFadeLabelArea_, zoneBarArea_,
        roundRobinLabelArea_;

    // Plugin drop highlight
    int dropHighlightPad_ = -1;
    // File-drag hover preview: first absolute pad + number of audio files
    // being dragged, so we can highlight every pad that will receive a sample.
    int fileDropStartPad_ = -1;
    int fileDropCount_ = 0;

    //==============================================================================
    void setDetailCollapsed(bool collapsed);
    void refreshPadButtons();
    void styleControls();
    void layoutEditor(juce::Rectangle<int> area);
    void layoutChain(juce::Rectangle<int> area);
    void layoutFooter(juce::Rectangle<int> area);
    void paintEditor(juce::Graphics& g);
    void paintChainHeader(juce::Graphics& g);
    void showOutputMenu();
    bool selectedPadHasChain() const;

    std::vector<juce::Component*> volumeControls();
    std::vector<juce::Component*> zoneControls();
    const PadLayerView* selectedLayer() const;
    void refreshLayers();
    void refreshZoneControls();
    void commitZones();
    void paintZones(juce::Graphics& g);
    juce::Rectangle<int> tabBounds(EditorTab tab) const;

    /// Close the current fader gesture, so the next edit is a new undo step.
    void endFaderGesture();
    void refreshDetailPanel();
    void goToPrevPage();
    void goToNextPage();

    /** Get MIDI note name for a pad index (pad 0 = note 36 = C2). */
    static juce::String getNoteName(int padIndex);

    /** Find which pad button (0-15) a screen point falls on, or -1 if none. */
    int padButtonIndexAtPoint(juce::Point<int> point) const;

    void showPadContextMenu(int padIndex, juce::Point<int> screenPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridUI)
};

}  // namespace magda::daw::ui
