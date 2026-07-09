#include "AutomationClipInspector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"
#include "core/AutomationCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr double kMinClipLength = 0.1;

void styleLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(11.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
}
}  // namespace

AutomationClipInspector::AutomationClipInspector() {
    // View icon (arrangement — automation clips are arrangement-only). The
    // bold view icons fill with "currentColor" (JUCE renders that black), so
    // recolor from black, matching the clip inspector.
    viewIcon_ = std::make_unique<magda::SvgButton>("View", BinaryData::iconarrangementboldm_svg,
                                                   BinaryData::iconarrangementboldm_svgSize);
    viewIcon_->setOriginalColor(juce::Colour(0xFF000000));
    viewIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    viewIcon_->setIconPadding(1.0f);
    viewIcon_->setInterceptsMouseClicks(false, false);
    viewIcon_->setTooltip("Arrangement clip");
    addChildComponent(*viewIcon_);

    // Clip type icon (automation clip).
    typeIcon_ = std::make_unique<magda::SvgButton>("Type", BinaryData::iconautomationboldm_svg,
                                                   BinaryData::iconautomationboldm_svgSize);
    typeIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    typeIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    typeIcon_->setIconPadding(1.0f);
    typeIcon_->setInterceptsMouseClicks(false, false);
    typeIcon_->setTooltip("Automation clip");
    addChildComponent(*typeIcon_);

    titleLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    using F = magda::DraggableValueLabel::Format;

    styleLabel(startLabel_, "Start");
    addChildComponent(startLabel_);
    startValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    startValue_->setBarsBeatsIsPosition(true);
    startValue_->setRange(0.0, 100000.0, 0.0);
    startValue_->onValueChange = [this]() {
        if (const auto* clip = getClip()) {
            const double newStart = juce::jmax(0.0, startValue_->getValue());
            if (newStart != clip->startBeats)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::MoveAutomationClipCommand>(clip->id, newStart));
        }
    };
    addChildComponent(*startValue_);

    styleLabel(endLabel_, "End");
    addChildComponent(endLabel_);
    endValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    endValue_->setBarsBeatsIsPosition(true);
    endValue_->setRange(0.0, 100000.0, 0.0);
    endValue_->onValueChange = [this]() {
        if (const auto* clip = getClip()) {
            const double newLength =
                juce::jmax(kMinClipLength, endValue_->getValue() - clip->startBeats);
            if (newLength != clip->lengthBeats)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::ResizeAutomationClipCommand>(clip->id, newLength,
                                                                         false));
        }
    };
    addChildComponent(*endValue_);

    styleLabel(lengthLabel_, "Length");
    addChildComponent(lengthLabel_);
    lengthValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    lengthValue_->setRange(kMinClipLength, 100000.0, 4.0);
    lengthValue_->onValueChange = [this]() {
        if (const auto* clip = getClip()) {
            const double newLength = juce::jmax(kMinClipLength, lengthValue_->getValue());
            if (newLength != clip->lengthBeats)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::ResizeAutomationClipCommand>(clip->id, newLength,
                                                                         false));
        }
    };
    addChildComponent(*lengthValue_);

    // Loop toggle: same chrome as the clip inspector's loop button. Off =
    // grey glyph; engaged = solid blue chip with a white glyph.
    styleLabel(loopLabel_, "Loop");
    addChildComponent(loopLabel_);
    loopToggle_ =
        std::make_unique<magda::SvgButton>("Loop", BinaryData::loop_svg, BinaryData::loop_svgSize);
    loopToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    loopToggle_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    loopToggle_->setActiveColor(juce::Colours::white);
    loopToggle_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    loopToggle_->setClickingTogglesState(false);
    loopToggle_->onClick = [this]() {
        if (const auto* clip = getClip())
            magda::AutomationManager::getInstance().setClipLooping(clip->id, !clip->looping);
    };
    addChildComponent(*loopToggle_);

    styleLabel(loopLengthLabel_, "Loop Len");
    addChildComponent(loopLengthLabel_);
    loopLengthValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    loopLengthValue_->setRange(kMinClipLength, 100000.0, 4.0);
    loopLengthValue_->onValueChange = [this]() {
        if (const auto* clip = getClip())
            magda::AutomationManager::getInstance().setClipLoopLength(
                clip->id, juce::jmax(kMinClipLength, loopLengthValue_->getValue()));
    };
    addChildComponent(*loopLengthValue_);
}

AutomationClipInspector::~AutomationClipInspector() {
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationClipInspector::onActivated() {
    magda::AutomationManager::getInstance().addListener(this);
}

void AutomationClipInspector::onDeactivated() {
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationClipInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

const magda::AutomationClipInfo* AutomationClipInspector::getClip() const {
    if (!selection_.isValid())
        return nullptr;
    return magda::AutomationManager::getInstance().getClip(selection_.clipId);
}

void AutomationClipInspector::setSelectedClip(const magda::AutomationClipSelection& selection) {
    selection_ = selection;
    updateFromSelection();
}

void AutomationClipInspector::automationClipsChanged(magda::AutomationLaneId laneId) {
    if (selection_.isValid() && selection_.laneId == laneId)
        refreshDisplay();
}

void AutomationClipInspector::updateFromSelection() {
    const auto* clip = getClip();
    showControls(clip != nullptr);
    if (clip == nullptr)
        return;

    juce::String title = clip->name;
    if (const auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId))
        title = magda::getDisplayNameForTarget(lane->target) + "  -  " + clip->name;
    titleLabel_.setText(title, juce::dontSendNotification);

    refreshDisplay();
    resized();
}

void AutomationClipInspector::refreshDisplay() {
    const auto* clip = getClip();
    if (clip == nullptr) {
        showControls(false);
        return;
    }

    startValue_->setValue(clip->startBeats, juce::dontSendNotification);
    endValue_->setValue(clip->getEndBeats(), juce::dontSendNotification);
    lengthValue_->setValue(clip->lengthBeats, juce::dontSendNotification);
    loopToggle_->setActive(clip->looping);
    loopLengthValue_->setValue(clip->loopLengthBeats, juce::dontSendNotification);
    loopLengthValue_->setEnabled(clip->looping);
    loopLengthValue_->setAlpha(clip->looping ? 1.0f : 0.4f);
}

void AutomationClipInspector::showControls(bool show) {
    viewIcon_->setVisible(show);
    typeIcon_->setVisible(show);
    titleLabel_.setVisible(show);
    startLabel_.setVisible(show);
    startValue_->setVisible(show);
    endLabel_.setVisible(show);
    endValue_->setVisible(show);
    lengthLabel_.setVisible(show);
    lengthValue_->setVisible(show);
    loopLabel_.setVisible(show);
    loopToggle_->setVisible(show);
    loopLengthLabel_.setVisible(show);
    loopLengthValue_->setVisible(show);
}

void AutomationClipInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);
    if (getClip() == nullptr)
        return;

    constexpr int LABEL_H = 16;
    constexpr int VALUE_H = 24;
    constexpr int ROW_GAP = 8;
    constexpr int COL_GAP = 8;

    // Header: view icon + type icon + title (same row layout as the clip
    // inspector).
    {
        constexpr int ICON_SIZE = 20;
        constexpr int ICON_GAP = 6;
        auto headerRow = bounds.removeFromTop(24);
        viewIcon_->setBounds(
            headerRow.removeFromLeft(ICON_SIZE).withSizeKeepingCentre(ICON_SIZE, ICON_SIZE));
        headerRow.removeFromLeft(ICON_GAP);
        typeIcon_->setBounds(
            headerRow.removeFromLeft(ICON_SIZE).withSizeKeepingCentre(ICON_SIZE, ICON_SIZE));
        headerRow.removeFromLeft(ICON_GAP);
        titleLabel_.setBounds(headerRow);
    }
    bounds.removeFromTop(ROW_GAP);

    // Start | End | Length
    {
        auto row = bounds.removeFromTop(LABEL_H);
        const int colW = (row.getWidth() - 2 * COL_GAP) / 3;
        startLabel_.setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(COL_GAP);
        endLabel_.setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(COL_GAP);
        lengthLabel_.setBounds(row);
    }
    {
        auto row = bounds.removeFromTop(VALUE_H);
        const int colW = (row.getWidth() - 2 * COL_GAP) / 3;
        startValue_->setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(COL_GAP);
        endValue_->setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(COL_GAP);
        lengthValue_->setBounds(row);
    }
    bounds.removeFromTop(ROW_GAP);

    // Loop | Loop Len
    {
        auto row = bounds.removeFromTop(LABEL_H);
        const int colW = (row.getWidth() - COL_GAP) / 2;
        loopLabel_.setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(COL_GAP);
        loopLengthLabel_.setBounds(row);
    }
    {
        auto row = bounds.removeFromTop(VALUE_H);
        const int colW = (row.getWidth() - COL_GAP) / 2;
        loopToggle_->setBounds(row.removeFromLeft(colW).withWidth(VALUE_H));
        row.removeFromLeft(COL_GAP);
        loopLengthValue_->setBounds(row);
    }
}

}  // namespace magda::daw::ui
