#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "inspector/clip/sections/ClipFadesSection.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief Audio clip properties side panel for the bottom panel
 *
 * Shows audio clip properties grouped into inspector-style sections
 * (Clip, Stretch, Pitch, Mix) with separator lines between sections.
 * Displayed alongside the waveform editor in a split layout.
 */
class AudioClipPropertiesContent : public PanelContent, public magda::ClipManagerListener {
  public:
    AudioClipPropertiesContent();
    ~AudioClipPropertiesContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::AudioClipProperties;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::AudioClipProperties, "Properties", "Audio clip properties",
                "Properties"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // ClipManagerListener
    void clipSelectionChanged(magda::ClipId clipId) override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipsChanged() override;

  private:
    void updateFromClip();
    void createControls();

    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;

    // Section labels
    std::unique_ptr<juce::Label> clipSectionLabel_;
    std::unique_ptr<juce::Label> stretchSectionLabel_;
    std::unique_ptr<juce::Label> pitchSectionLabel_;
    std::unique_ptr<juce::Label> mixSectionLabel_;

    // Separator Y positions for painting
    std::vector<int> separatorYPositions_;

    // Clip section
    std::unique_ptr<juce::TextButton> warpToggle_;
    std::unique_ptr<juce::TextButton> autoTempoToggle_;
    std::unique_ptr<juce::TextButton> reverseToggle_;

    // Stretch section
    std::unique_ptr<juce::Label> speedLabel_;
    std::unique_ptr<DraggableValueLabel> stretchValue_;
    std::unique_ptr<juce::Label> modeLabel_;
    std::unique_ptr<juce::ComboBox> stretchModeCombo_;
    std::unique_ptr<juce::Label> bpmLabel_;
    std::unique_ptr<DraggableValueLabel> bpmValue_;
    std::unique_ptr<juce::Label> beatsLabel_;
    std::unique_ptr<DraggableValueLabel> beatsValue_;
    // Source key root. Mirrors the inspector's KEY combo and writes back
    // to the clip only until the user explicitly saves to the library.
    std::unique_ptr<juce::Label> keyLabel_;
    std::unique_ptr<juce::ComboBox> keyRootCombo_;
    std::unique_ptr<juce::TextButton> saveLibraryButton_;

    // Pitch section
    std::unique_ptr<juce::Label> pitchLabel_;
    std::unique_ptr<DraggableValueLabel> pitchValue_;
    std::unique_ptr<juce::TextButton> analogPitchToggle_;

    // Fades section
    std::unique_ptr<ClipFadesSection> fadesSection_;

    // Transient Detection section
    std::unique_ptr<juce::Label> transientSectionLabel_;
    std::unique_ptr<juce::Label> transientSensLabel_;
    std::unique_ptr<DraggableValueLabel> transientSensValue_;

    // Mix section
    std::unique_ptr<juce::Label> volLabel_;
    std::unique_ptr<DraggableValueLabel> volumeValue_;
    std::unique_ptr<juce::Label> gainLabel_;
    std::unique_ptr<DraggableValueLabel> gainValue_;
    std::unique_ptr<juce::Label> panLabel_;
    std::unique_ptr<DraggableValueLabel> panValue_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioClipPropertiesContent)
};

}  // namespace magda::daw::ui
