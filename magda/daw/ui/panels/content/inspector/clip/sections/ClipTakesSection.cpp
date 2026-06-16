#include "ClipTakesSection.hpp"

#include "../../../../../themes/DarkTheme.hpp"
#include "../../../../../themes/FontManager.hpp"
#include "../../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "audio/CompService.hpp"

namespace magda::daw::ui {

namespace {
constexpr int SECTION_H = 18;
constexpr int GAP = 4;
constexpr int ROW_H = 24;
constexpr int BTN_H = 22;
constexpr int EXPAND_W = 16;
}  // namespace

ClipTakesSection::ClipTakesSection() {
    initControls();
}

ClipTakesSection::~ClipTakesSection() {
    takesCombo_.setLookAndFeel(nullptr);
    clearCompButton_.setLookAndFeel(nullptr);
}

void ClipTakesSection::initControls() {
    sectionLabel_.setText("Takes", juce::dontSendNotification);
    sectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    sectionLabel_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(sectionLabel_);

    takesCombo_.setColour(juce::ComboBox::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    takesCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    takesCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    takesCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    takesCombo_.onChange = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (clip == nullptr || !clip->isAudio())
            return;
        const int idx = takesCombo_.getSelectedId() - 1;  // combo id = take index + 1
        if (idx < 0 || idx >= static_cast<int>(clip->audio().takes.size()))
            return;
        // Picking a take exits any active comp and fronts that take.
        clip->audio().compActive = false;
        clip->audio().comp.clear();
        clip->audio().currentTakeIndex = idx;
        clip->audio().source.filePath = clip->audio().takes[idx].filePath;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(primaryClipId());
    };
    addChildComponent(takesCombo_);

    expandButton_ = std::make_unique<magda::SvgButton>(
        "TakesExpand", BinaryData::expand_svg, BinaryData::expand_svgSize, BinaryData::collapse_svg,
        BinaryData::collapse_svgSize);
    expandButton_->onClick = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (clip == nullptr || !clip->isAudio())
            return;
        clip->takesExpanded = !clip->takesExpanded;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(primaryClipId());
    };
    addChildComponent(expandButton_.get());

    clearCompButton_.setButtonText("Clear Comp");
    clearCompButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    clearCompButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    clearCompButton_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clearCompButton_.onClick = [this]() {
        magda::CompService::getInstance().clearComp(primaryClipId());
    };
    addChildComponent(clearCompButton_);
}

bool ClipTakesSection::hasTakes() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
    return clip != nullptr && clip->isAudio() && clip->audio().takes.size() > 1;
}

bool ClipTakesSection::compActive() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
    return clip != nullptr && clip->isAudio() && clip->audio().compActive;
}

void ClipTakesSection::setClip(magda::ClipId clipId) {
    selectedClipIds_.clear();
    if (clipId != magda::INVALID_CLIP_ID)
        selectedClipIds_.insert(clipId);
    update();
}

void ClipTakesSection::setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds) {
    selectedClipIds_ = clipIds;
    update();
}

void ClipTakesSection::update() {
    const bool show = hasTakes();
    sectionLabel_.setVisible(show);
    takesCombo_.setVisible(show);
    if (expandButton_)
        expandButton_->setVisible(show);
    clearCompButton_.setVisible(show && compActive());

    if (!show)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
    const auto& audio = clip->audio();
    const int numTakes = static_cast<int>(audio.takes.size());
    const bool comp = audio.compActive;

    takesCombo_.clear(juce::dontSendNotification);
    for (int i = 0; i < numTakes; ++i)
        takesCombo_.addItem("Take " + juce::String(i + 1) + " / " + juce::String(numTakes), i + 1);
    takesCombo_.setTextWhenNothingSelected(comp ? "Comp" : "");
    takesCombo_.setSelectedId(comp ? 0 : audio.currentTakeIndex + 1, juce::dontSendNotification);

    if (expandButton_) {
        expandButton_->setActive(clip->takesExpanded);
        expandButton_->setTooltip(clip->takesExpanded ? "Collapse take lanes"
                                                      : "Expand take lanes");
    }

    resized();
}

int ClipTakesSection::getPreferredHeight() const {
    if (!isVisible() || !hasTakes())
        return 0;
    return SECTION_H + GAP + ROW_H + (compActive() ? GAP + BTN_H : 0);
}

void ClipTakesSection::resized() {
    auto area = getLocalBounds();
    sectionLabel_.setBounds(area.removeFromTop(SECTION_H));
    area.removeFromTop(GAP);

    auto row = area.removeFromTop(ROW_H);
    if (expandButton_) {
        expandButton_->setBounds(
            row.removeFromRight(EXPAND_W).withSizeKeepingCentre(EXPAND_W, EXPAND_W));
        row.removeFromRight(4);
    }
    takesCombo_.setBounds(row.reduced(0, 1));

    if (clearCompButton_.isVisible()) {
        area.removeFromTop(GAP);
        clearCompButton_.setBounds(area.removeFromTop(BTN_H).reduced(0, 1));
    }
}

}  // namespace magda::daw::ui
