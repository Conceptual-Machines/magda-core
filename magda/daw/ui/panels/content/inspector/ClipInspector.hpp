#pragma once

#include <array>
#include <unordered_set>

#include "../../common/BarsBeatsTicksLabel.hpp"
#include "../../common/DraggableValueLabel.hpp"
#include "../../common/SvgButton.hpp"
#include "../../common/TextSlider.hpp"
#include "BaseInspector.hpp"
#include "clip/sections/ChordProgressionSection.hpp"
#include "clip/sections/ClipFadesSection.hpp"
#include "clip/sections/ClipLabelsSection.hpp"
#include "clip/sections/ClipTakesSection.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "ui/components/common/CollapsibleSectionHeader.hpp"
#include "ui/components/common/GlyphChip.hpp"
#include "ui/components/common/SegmentStrip.hpp"
#include "ui/components/common/ToggleSwitch.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for clip properties
 *
 * A header (name, summary, save, power) over collapsible sections: Clip (position and
 * loop), Mix, Warp & Pitch and Fades for audio; Pitch and Groove for MIDI; Launch for session
 * clips. Edits apply to every selected clip.
 */
class ClipInspector : public BaseInspector,
                      public magda::ClipManagerListener,
                      private magda::ConfigListener {
  public:
    ClipInspector();
    ~ClipInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /**
     * @brief Set the currently selected clips (batch)
     * @param clipIds Set of clip IDs to inspect
     */
    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    /**
     * @brief Set the currently selected clip (convenience wrapper)
     * @param clipId The clip to inspect (INVALID_CLIP_ID for none)
     */
    void setSelectedClip(magda::ClipId clipId);

    // ClipManagerListener interface
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

  private:
    // Initialization helpers (split from constructor for code size)
    void initHeader();
    void initClipSection();
    void initWarpSection();
    void initSessionLaunchSection();
    void initPitchSection();
    void initGrooveSection();
    void initMixSection();
    void initFadesSection();
    void initViewport();
    void updateAudioSourceValueDisplays(const magda::ClipInfo& clip);
    void updatePitchDisplays(float pitchChange);

    // Current selection (supports single and multi-clip)
    std::unordered_set<magda::ClipId> selectedClipIds_;

    /** @brief Returns the primary clip ID (first in set) or INVALID_CLIP_ID if empty */
    magda::ClipId primaryClipId() const {
        return selectedClipIds_.empty() ? magda::INVALID_CLIP_ID : *selectedClipIds_.begin();
    }

    double projectBpm() const;

    // ---- Header ----------------------------------------------------------
    juce::Label clipNameValue_;
    juce::Label clipSubline_;
    std::unique_ptr<juce::Component> colourSwatch_;
    std::unique_ptr<magda::SvgButton> clipGhostIcon_;  // link glyph, ghost clips only
    GlyphChip saveLibraryButton_{"Save to library", GlyphChip::Glyph::Bookmark};
    std::unique_ptr<magda::SvgButton> clipEnabledToggle_;  // enable/disable clip (#1736)
    // The name field follows the track header style: filled with the clip colour in Full bar.
    struct NamePopupListener : juce::MouseListener {
        std::function<void()> onPopup;
        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu() && onPopup)
                onPopup();
        }
    };
    NamePopupListener namePopupListener_;
    bool nameFilled_ = false;
    juce::Colour nameFill_;
    juce::Rectangle<int> nameArea_;
    void applyHeaderStyle();
    void configChanged() override;
    void saveToLibrary();
    juce::String sublineFor(const magda::ClipInfo& clip) const;

    // ---- Clip: position and loop -----------------------------------------
    CollapsibleSectionHeader clipHeader_{"Clip", "clipInspector.clip"};
    juce::Label startColumnLabel_, endColumnLabel_, lengthColumnLabel_;
    juce::Label clipRowLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipStartValue_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipEndValue_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLengthValue_;
    ToggleSwitch clipLoopSwitch_;
    juce::Label loopRowLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopStartValue_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopEndValue_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopPhaseValue_;
    juce::Label clipLoopPhaseLabel_;  // PHASE or OFFSET, under the third column

    std::unique_ptr<ClipTakesSection> takesSection_;
    std::unique_ptr<ChordProgressionSection> chordProgressionSection_;

    // ---- Mix (audio) -----------------------------------------------------
    CollapsibleSectionHeader mixHeader_{"Mix", "clipInspector.mix"};
    juce::Label volumeLabel_, panLabel_, gainLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipVolumeValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipPanValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipGainValue_;
    juce::TextButton reverseToggle_;
    juce::TextButton gainEnvelopeToggle_;  // shows the clip's gain envelope (#3064)

    // ---- Warp & pitch (audio) --------------------------------------------
    CollapsibleSectionHeader warpHeader_{"Warp & Pitch", "clipInspector.warp"};
    ToggleSwitch clipWarpSwitch_;
    SegmentStrip playbackModeStrip_;  // Beat | Free: the clip's PlaybackIntent
    juce::ComboBox stretchModeCombo_;
    juce::Label transientSensitivityLabel_;
    std::unique_ptr<magda::DraggableValueLabel> transientSensitivityValue_;
    juce::Label tempoLabel_;
    juce::TextButton halveTempoButton_, doubleTempoButton_;
    juce::Label clipBpmValue_;
    juce::Label clipBpmUnitLabel_;
    juce::Label sourceLengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipBeatsLengthValue_;
    juce::Label speedLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipStretchValue_;
    juce::TextButton analogPitchToggle_;
    juce::Label pitchLabel_;
    std::unique_ptr<magda::DraggableValueLabel> pitchSemitonesValue_;
    std::unique_ptr<magda::DraggableValueLabel> pitchCentsValue_;
    // Source key root. Edits stay on the clip until explicitly saved.
    juce::Label clipKeyLabel_;
    juce::ComboBox clipKeyRootCombo_;
    double displayedSourceBpm_ = 0.0;

    // ---- Pitch (MIDI) ----------------------------------------------------
    CollapsibleSectionHeader midiPitchHeader_{"Pitch", "clipInspector.midiPitch"};
    juce::Label midiTransposeLabel_;
    std::array<juce::TextButton, 4> midiTransposeButtons_;  // -12, -1, +1, +12
    juce::Label midiTransposeHint_;

    // ---- Groove (MIDI) ---------------------------------------------------
    CollapsibleSectionHeader grooveHeader_{"Groove", "clipInspector.groove"};
    juce::TextButton grooveTemplateButton_;
    juce::Label grooveStrengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> grooveStrengthValue_;

    // Two-column groove template picker popup
    class GroovePickerPopup;
    void showGroovePicker();
    void onGrooveTemplateSelected(const juce::String& templateName);

    // ---- Fades (audio) ---------------------------------------------------
    std::unique_ptr<ClipFadesSection> fadesSection_;

    // ---- Labels (audio, #2316) -------------------------------------------
    std::unique_ptr<ClipLabelsSection> labelsSection_;
    void initLabelsSection();

    // ---- Launch (session) ------------------------------------------------
    CollapsibleSectionHeader launchHeader_{"Launch", "clipInspector.launch"};
    juce::Label launchQuantizeLabel_;
    juce::ComboBox launchQuantizeCombo_;
    juce::Label followActionLabel_;
    juce::ComboBox followActionCombo_;
    juce::Label followActionDelayLabel_;
    TextSlider followActionDelaySlider_;
    juce::Label followActionLoopCountLabel_;
    TextSlider followActionLoopCountSlider_;

    // Scrollable container for clip properties
    juce::Viewport clipPropsViewport_;
    class ClipPropsContainer : public juce::Component {
      public:
        void paint(juce::Graphics& g) override;
        std::vector<int> separatorYPositions;
    };
    ClipPropsContainer clipPropsContainer_;

    // Multi-selection range cache
    struct ClipRange {
        bool valid = false;  // True if at least one clip was processed
        float minPitchChange = 0.0f, maxPitchChange = 0.0f;
        float minVolumeDB = 0.0f, maxVolumeDB = 0.0f;
        float minPan = 0.0f, maxPan = 0.0f;
        float minGainDB = 0.0f, maxGainDB = 0.0f;
        double minSpeedRatio = 1.0, maxSpeedRatio = 1.0;
        bool allAudio = true, allMidi = true;
    };
    ClipRange clipRange_;

    // Drag-start tracking for multi-selection delta edits
    double multiSemitonesDragStart_ = 0.0;
    double multiCentsDragStart_ = 0.0;
    double multiVolumeDragStart_ = 0.0;
    double multiPanDragStart_ = 0.0;
    double multiGainDragStart_ = 0.0;
    double multiSpeedRatioDragStart_ = 0.0;
    double multiStartDragStart_ = 0.0;
    double multiEndDragStart_ = 0.0;
    double multiLengthDragStart_ = 0.0;

    // Update methods
    void updateFromSelectedClip();
    void updateLoopValueDisplays(const magda::ClipInfo& clip, double projectBPM, int numerator,
                                 int denominator);
    void showClipControls(bool show);
    void computeClipRange();
    void refreshClipRangeDisplay();
    void applyPitchDelta(double semitones);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipInspector)
};

}  // namespace magda::daw::ui
