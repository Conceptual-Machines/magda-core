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
#include "drum_grid/PadMixRow.hpp"
#include "layout/DashedAddButton.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

/**
 * @brief The Drum Grid's body in the v1 shell: [pads | pad mix over the pad's chain] over a footer.
 *
 * The selected pad's chain shows in the rack's chain view, the same chain its mixer channel
 * shows (#3144). Pads take sample and plugin drops.
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

    static constexpr int kMinChainWidth = 264;
    static constexpr int kFooterHeight = 24;

    DrumGridUI();
    ~DrumGridUI() override;

    //==============================================================================
    // Data update

    /** Update cached info for a single pad. Called from DeviceSlotComponent::updateCustomUI. */
    void updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                       float levelDb, float pan, int chainIndex = -1, bool bypassed = false);

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

    /** Which fader drag the pad level and pan callbacks currently belong to.
        Bumped when a drag ends, so consecutive gestures on the same fader are
        separate undo steps rather than one merged run (#2211). */
    int getFaderGesture() const {
        return faderGesture_;
    }

    /** Called when a plugin is dropped onto a pad. (padIndex, DynamicObject with plugin info) */
    std::function<void(int, const juce::DynamicObject&)> onPluginDropped;

    /** Called when the user explicitly asks to analyse a pad sample role. (padIndex) */
    std::function<void(int)> onAnalyzePadRoleRequested;

    /** Called when a pad is dragged and dropped onto another pad. (sourcePad, targetPad) */
    std::function<void(int, int)> onPadsSwapped;

    /** Called when play button is pressed/released on a pad. (padIndex, isNoteOn) */
    std::function<void(int, bool)> onNotePreview;

    /** @brief A pad's switches and faders, which are its mixer channel's (#3010). */
    struct PadMix {
        float level = 0.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
    };

    /// Read at the poll rate, so a pad's channel moved in the mixer shows here.
    /// Nothing for a pad with no chain.
    std::function<std::optional<PadMix>(int padIndex)> getPadMix;

    /// Whether a pad has sounded since it was last asked. Read at the poll rate.
    std::function<bool(int padIndex)> consumePadTrigger;

    /** Called when layout changes (e.g., chains panel toggled) so parent can resize. */
    std::function<void()> onLayoutChanged;

    /// The path of a pad's chain; invalid while the pad has none.
    std::function<magda::ChainNodePath(int padIndex)> getPadChainPath;

    /// A device dropped on the chain area of a pad that has a chain.
    std::function<void(int padIndex, const magda::DeviceInfo& device)> onDeviceDroppedOnPad;

    /// The mix row: the pad's channel fader and switches, and its power (#3010).
    std::function<void(int padIndex, float volumeDb, float pan)> onPadMixChanged;
    std::function<void(int padIndex, bool mute, bool solo, bool bypassed)> onPadSwitchesChanged;

    /** Called by the add slot of a pad with no chain yet. (padIndex) */
    std::function<void(int)> onAddDeviceRequested;

    /** @brief Show the selected pad's chain again after the model changed it. */
    void refreshPadChain();

    /** @brief Full width, or within the max content width, past which the pad chain scrolls. */
    int getPreferredContentWidth() const;
    /** @brief The pads: everything but the pad chain. */
    int fixedContentWidth() const;
    /** @brief The width this grid may take; 0 = no limit. */
    void setMaxContentWidth(int maxWidth);

    //==============================================================================
    // Component overrides
    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

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

    // Chain, under the selected pad's mix
    PadMixRow padMixRow_;
    ChainPanel padChainView_;
    DashedAddButton emptyAddButton_;

    // Footer
    std::unique_ptr<juce::ArrowButton> prevPageButton_;
    std::unique_ptr<juce::ArrowButton> nextPageButton_;

    // Areas laid out in resized() and painted in paint()
    int maxContentWidth_ = 0;
    juce::Rectangle<int> padsArea_, chainArea_, footerArea_, pageTextArea_;

    // Plugin drop highlight
    int dropHighlightPad_ = -1;
    // File-drag hover preview: first absolute pad + number of audio files
    // being dragged, so we can highlight every pad that will receive a sample.
    int fileDropStartPad_ = -1;
    int fileDropCount_ = 0;

    //==============================================================================
    void refreshPadButtons();
    void layoutChain(juce::Rectangle<int> area);
    void layoutFooter(juce::Rectangle<int> area);
    void paintEmptyChain(juce::Graphics& g);
    bool selectedPadHasChain() const;
    void refreshPadRow();

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

    /// A drop that landed on the chain area rather than on a pad: true when taken.
    bool dropOnChainArea(juce::Point<int> point, const juce::var& description,
                         const juce::StringArray& files);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridUI)
};

}  // namespace magda::daw::ui
