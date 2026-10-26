#include "AutomationClipInspector.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "clip/ClipInspectorStyle.hpp"
#include "core/AutomationCommands.hpp"
#include "core/AutomationInfo.hpp"
#include "core/UndoManager.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr double kMinClipLength = 0.1;
constexpr int kHeaderHeight = 40;
constexpr int kRowLabelWidth = 62;
constexpr int kHeadingHeight = 14;
}  // namespace

AutomationClipInspector::AutomationClipInspector() {
    // Colour swatch: same menu as the clip inspector (None / Inherit from
    // Track / palettes); sets the clip colour via an undoable command.
    colourSwatch_ = std::make_unique<magda::ColourSwatch>();
    colourSwatch_->onColourClicked = [this]() {
        const auto* clip = getClip();
        if (clip == nullptr)
            return;

        auto makeChip = [](juce::Colour colour) {
            juce::Image chip(juce::Image::ARGB, 14, 14, true);
            juce::Graphics cg(chip);
            cg.setColour(colour);
            cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(chip);
            return drawable;
        };

        juce::PopupMenu menu;
        menu.addItem(1, "None");
        menu.addSeparator();
        menu.addItem(2, "Inherit from Track");
        menu.addSeparator();
        const auto palette =
            magda::ProjectManager::getInstance().getCurrentProjectInfo().defaults.colourPalette;
        for (size_t i = 0; i < palette.size(); ++i) {
            const auto colour = juce::Colour(palette[i].colour);
            menu.addItem(static_cast<int>(i + 3), palette[i].name, true, false, makeChip(colour));
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(colourSwatch_.get()),
            [this, palette](int result) {
                if (result == 0)
                    return;
                const auto* clip = getClip();
                if (clip == nullptr)
                    return;

                juce::Colour newColour;
                if (result == 1) {
                    newColour = juce::Colour(0xFF444444);
                } else if (result == 2) {
                    const auto trackColour =
                        magda::AutomationManager::getInstance().getLaneTrackColour(
                            selection_.laneId);
                    if (!trackColour)
                        return;
                    newColour = *trackColour;
                } else {
                    const auto idx = static_cast<size_t>(result - 3);
                    if (idx >= palette.size())
                        return;
                    newColour = juce::Colour(palette[idx].colour);
                }
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetAutomationClipColourCommand>(clip->id, newColour));
            });
    };
    addChildComponent(*colourSwatch_);

    // Clip name (double-click to rename) over the lane target and length, as on the clip inspector.
    titleLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    titleLabel_.setBorderSize({});
    titleLabel_.setEditable(false, true);
    titleLabel_.onTextChange = [this]() {
        const auto* clip = getClip();
        const auto newName = titleLabel_.getText().trim();
        if (clip == nullptr || newName.isEmpty() || newName == clip->name)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::RenameAutomationClipCommand>(clip->id, newName));
    };
    addAndMakeVisible(titleLabel_);
    subline_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    subline_.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    subline_.setBorderSize({});
    addChildComponent(subline_);

    clip_style::columnHeading(startLabel_, "Start");
    clip_style::columnHeading(endLabel_, "End");
    clip_style::columnHeading(lengthLabel_, "Length");
    clip_style::caption(clipRowLabel_, "Clip");
    clip_style::caption(loopLabel_, "Loop");
    for (auto* label : {&startLabel_, &endLabel_, &lengthLabel_, &clipRowLabel_, &loopLabel_})
        addChildComponent(*label);

    using F = magda::DraggableValueLabel::Format;

    startValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    clip_style::field(*startValue_);
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

    endValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    clip_style::field(*endValue_);
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

    lengthValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    clip_style::field(*lengthValue_);
    // Duration, not position: 4 bars must read 4.0.000, not 5.1.000.
    lengthValue_->setBarsBeatsIsPosition(false);
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

    loopSwitch_.setTooltip("Loop");
    loopSwitch_.onClick = [this]() {
        if (const auto* clip = getClip())
            magda::AutomationManager::getInstance().setClipLooping(clip->id, !clip->looping);
    };
    addChildComponent(loopSwitch_);

    // Loop length sits under the LENGTH heading, in the Loop row.
    loopLengthValue_ = std::make_unique<magda::DraggableValueLabel>(F::BarsBeats);
    clip_style::field(*loopLengthValue_);
    loopLengthValue_->setTooltip("Loop length");
    // Duration, not position (see Length above).
    loopLengthValue_->setBarsBeatsIsPosition(false);
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
    g.fillAll(ActiveTheme::getBackgroundColour());
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

    refreshDisplay();
    resized();
}

void AutomationClipInspector::refreshDisplay() {
    const auto* clip = getClip();
    if (clip == nullptr) {
        showControls(false);
        return;
    }

    if (!titleLabel_.isBeingEdited())
        titleLabel_.setText(clip->name, juce::dontSendNotification);
    if (clip->colour == juce::Colour(0xFF444444))
        colourSwatch_->clearColour();
    else
        colourSwatch_->setColour(clip->colour);
    const auto& project = magda::ProjectManager::getInstance().getCurrentProjectInfo();

    juce::StringArray parts{"Automation"};
    if (const auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId))
        parts.add(magda::getDisplayNameForTarget(lane->target));
    const double bars = clip->lengthBeats / juce::jmax(1, project.timeSignatureNumerator);
    const bool wholeBars = std::abs(bars - std::round(bars)) < 0.01;
    parts.add((wholeBars ? juce::String(juce::roundToInt(bars)) : juce::String(bars, 2)) +
              (wholeBars && juce::roundToInt(bars) == 1 ? " bar" : " bars"));
    subline_.setText(parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 ")),
                     juce::dontSendNotification);

    for (auto* label :
         {startValue_.get(), endValue_.get(), lengthValue_.get(), loopLengthValue_.get()})
        label->setTimeSignature(project.timeSignatureNumerator, project.timeSignatureDenominator);
    startValue_->setValue(clip->startBeats, juce::dontSendNotification);
    endValue_->setValue(clip->getEndBeats(), juce::dontSendNotification);
    lengthValue_->setValue(clip->lengthBeats, juce::dontSendNotification);
    loopSwitch_.setOn(clip->looping);
    loopLengthValue_->setValue(clip->loopLengthBeats, juce::dontSendNotification);
    loopLengthValue_->setEnabled(clip->looping);
    loopLengthValue_->setAlpha(clip->looping ? 1.0f : 0.4f);
}

void AutomationClipInspector::showControls(bool show) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             colourSwatch_.get(), &titleLabel_, &subline_, &startLabel_, &endLabel_, &lengthLabel_,
             &clipRowLabel_, startValue_.get(), endValue_.get(), lengthValue_.get(), &loopLabel_,
             &loopSwitch_, loopLengthValue_.get()})
        c->setVisible(show);
}

void AutomationClipInspector::resized() {
    auto bounds = getLocalBounds().reduced(10, 8);
    if (getClip() == nullptr)
        return;

    // Header: colour spine | name over summary.
    {
        auto row = bounds.removeFromTop(kHeaderHeight);
        colourSwatch_->setBounds(row.removeFromLeft(4).reduced(0, 3));
        row.removeFromLeft(10);
        auto text = row.reduced(0, 2);
        titleLabel_.setBounds(text.removeFromTop(20));
        subline_.setBounds(text.removeFromTop(14));
    }
    bounds.removeFromTop(6);

    const int fieldW = (bounds.getWidth() - kRowLabelWidth - 4 * 3) / 3;
    auto cells = [&](juce::Rectangle<int> row) {
        row.removeFromLeft(kRowLabelWidth);
        std::array<juce::Rectangle<int>, 3> out{};
        for (auto& cell : out) {
            row.removeFromLeft(4);
            cell = row.removeFromLeft(fieldW);
        }
        return out;
    };

    auto headings = cells(bounds.removeFromTop(kHeadingHeight));
    startLabel_.setBounds(headings[0]);
    endLabel_.setBounds(headings[1]);
    lengthLabel_.setBounds(headings[2]);
    bounds.removeFromTop(2);

    auto clipRow = bounds.removeFromTop(clip_style::kFieldHeight);
    clipRowLabel_.setBounds(clipRow.withWidth(kRowLabelWidth));
    auto clipCells = cells(clipRow);
    startValue_->setBounds(clipCells[0]);
    endValue_->setBounds(clipCells[1]);
    lengthValue_->setBounds(clipCells[2]);
    bounds.removeFromTop(clip_style::kGap);

    auto loopRow = bounds.removeFromTop(clip_style::kFieldHeight);
    auto label = loopRow.withWidth(kRowLabelWidth);
    loopSwitch_.setBounds(label.removeFromLeft(ToggleSwitch::kWidth));
    label.removeFromLeft(6);
    loopLabel_.setBounds(label);
    loopLengthValue_->setBounds(cells(loopRow)[2]);
}

}  // namespace magda::daw::ui
