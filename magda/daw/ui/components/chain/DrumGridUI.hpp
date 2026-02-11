#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "PadChainRowComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "SamplerUI.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
}
}  // namespace tracktion

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Custom inline UI for the Drum Grid plugin
 *
 * Layout:
 *   Left ~45%: 4x4 pad grid (16 pads visible per page, 4 pages = 64 pads)
 *   Right ~55%: Quick controls row + SamplerUI for selected pad
 *
 * Pads display note name + truncated sample name.
 * Pads are drop targets for audio files and plugins.
 * Click selects; selected pad highlighted.
 */
class DrumGridUI : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget {
  public:
    static constexpr int kPadsPerPage = 16;
    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 4;
    static constexpr int kTotalPads = 64;
    static constexpr int kNumPages = kTotalPads / kPadsPerPage;
    static constexpr int kPluginParamSlots = 16;

    DrumGridUI();
    ~DrumGridUI() override = default;

    //==============================================================================
    // Data update

    /** Update cached info for a single pad. Called from DeviceSlotComponent::updateCustomUI. */
    void updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                       float levelDb, float pan);

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

    /** Called when Load button is clicked for the selected pad. (padIndex) */
    std::function<void(int)> onLoadRequested;

    /** Called when Clear button is clicked for the selected pad. (padIndex) */
    std::function<void(int)> onClearRequested;

    /** Called when pad level changes. (padIndex, levelDb) */
    std::function<void(int, float)> onPadLevelChanged;

    /** Called when pad pan changes. (padIndex, pan -1..1) */
    std::function<void(int, float)> onPadPanChanged;

    /** Called when pad mute changes. (padIndex, muted) */
    std::function<void(int, bool)> onPadMuteChanged;

    /** Called when pad solo changes. (padIndex, soloed) */
    std::function<void(int, bool)> onPadSoloChanged;

    /** Called when a plugin is dropped onto a pad. (padIndex, DynamicObject with plugin info) */
    std::function<void(int, const juce::DynamicObject&)> onPluginDropped;

    /** Callback to get the MagdaSamplerPlugin for a given pad (returns nullptr if not a sampler) */
    std::function<daw::audio::MagdaSamplerPlugin*(int padIndex)> getPadSampler;

    /** Callback to get the te::Plugin for a given pad (any plugin type) */
    std::function<tracktion::engine::Plugin*(int padIndex)> getPadPlugin;

    /** Called when delete is clicked on a chain row. (padIndex) */
    std::function<void(int)> onPadDeleteRequested;

    /** Called when layout changes (e.g., chains panel toggled) so parent can resize. */
    std::function<void()> onLayoutChanged;

    /** Update the embedded SamplerUI for the given pad index */
    void updatePadSamplerUI(int padIndex);

    /** Populate param slots from a non-sampler plugin on the given pad */
    void refreshPluginParams(int padIndex);

    /** Rebuild visible chain rows from padInfos_. */
    void rebuildChainRows();

    /** Show or hide the chains panel. */
    void setChainsPanelVisible(bool visible);

    /** Whether the chains panel is currently visible. */
    bool isChainsPanelVisible() const {
        return chainsPanelVisible_;
    }

    //==============================================================================
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
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

        std::function<void(int)> onClicked;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;

      private:
        int padIndex_ = 0;
        juce::String noteName_;
        juce::String sampleName_;
        bool selected_ = false;
        bool hasSample_ = false;
        bool muted_ = false;
        bool soloed_ = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadButton)
    };

    //==============================================================================
    // Cached pad data
    struct PadInfo {
        juce::String sampleName;
        bool mute = false;
        bool solo = false;
        float level = 0.0f;
        float pan = 0.0f;
    };

    std::array<PadInfo, kTotalPads> padInfos_;
    int selectedPad_ = 0;
    int currentPage_ = 0;

    // Pad grid
    std::array<PadButton, kPadsPerPage> padButtons_;

    // Pagination
    juce::TextButton prevPageButton_{"<"};
    juce::TextButton nextPageButton_{">"};
    juce::Label pageLabel_;

    // Detail panel (compact quick controls row)
    juce::Label detailPadNameLabel_;
    juce::Label detailSampleNameLabel_;
    juce::Label levelLabel_;
    juce::Label panLabel_;
    TextSlider levelSlider_{TextSlider::Format::Decibels};
    TextSlider panSlider_{TextSlider::Format::Decimal};
    juce::TextButton muteButton_{"M"};
    juce::TextButton soloButton_{"S"};
    juce::TextButton loadButton_{"Load"};
    juce::TextButton clearButton_{"Clear"};

    // Embedded SamplerUI for selected pad
    SamplerUI padSamplerUI_;

    // Plugin parameter grid (for non-sampler plugins)
    std::array<std::unique_ptr<ParamSlotComponent>, kPluginParamSlots> pluginParamSlots_;
    std::unique_ptr<magda::SvgButton> pluginUIButton_;
    juce::Label pluginNameLabel_;

    // Chains panel
    bool chainsPanelVisible_ = true;
    juce::Label chainsLabel_;
    juce::Viewport chainsViewport_;
    juce::Component chainsContainer_;
    std::vector<std::unique_ptr<PadChainRowComponent>> chainRows_;
    std::unique_ptr<magda::SvgButton> chainsToggleButton_;

    // Plugin drop highlight
    int dropHighlightPad_ = -1;

    //==============================================================================
    void refreshPadButtons();
    void refreshDetailPanel();
    void goToPrevPage();
    void goToNextPage();

    /** Get MIDI note name for a pad index (pad 0 = note 36 = C2). */
    static juce::String getNoteName(int padIndex);

    /** Find which pad button (0-15) a screen point falls on, or -1 if none. */
    int padButtonIndexAtPoint(juce::Point<int> point) const;

    void setupLabel(juce::Label& label, const juce::String& text, float fontSize);
    void setupButton(juce::TextButton& button);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridUI)
};

}  // namespace magda::daw::ui
