#include "AutomationPointInspector.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/AutomationCommands.hpp"
#include "core/AutomationInfo.hpp"
#include "core/ControlTarget.hpp"
#include "core/ParameterUtils.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {
void styleLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(11.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
}

}  // namespace

AutomationPointInspector::AutomationPointInspector() {
    countLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    countLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(countLabel_);

    styleLabel(valueLabel_, "Value");
    addChildComponent(valueLabel_);
    valueValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    valueValue_->onValueChange = [this]() {
        if (!selection_.isValid())
            return;
        const double curReal = valueValue_->getValue();
        const double deltaReal = curReal - valueDragStart_;
        if (std::abs(deltaReal) < 1.0e-9)
            return;

        auto& undo = magda::UndoManager::getInstance();
        undo.beginCompoundOperation("Set Automation Value");
        for (auto pid : selection_.pointIds) {
            const auto* p = findPoint(pid);
            if (!p)
                continue;
            const double newReal =
                juce::jlimit(static_cast<double>(info_.minValue),
                             static_cast<double>(info_.maxValue), normToReal(p->value) + deltaReal);
            const double newNorm = juce::jlimit(0.0, 1.0, realToNorm(newReal));
            undo.executeCommand(std::make_unique<magda::MoveAutomationPointCommand>(
                selection_.laneId, selection_.clipId, pid, p->beatPosition, newNorm));
        }
        undo.endCompoundOperation();
        valueDragStart_ = curReal;
    };
    addChildComponent(*valueValue_);

    styleLabel(posLabel_, "Pos");
    addChildComponent(posLabel_);
    posValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::BarsBeats);
    posValue_->setBarsBeatsIsPosition(true);
    posValue_->setRange(0.0, 100000.0, 0.0);
    posValue_->onValueChange = [this]() {
        if (!selection_.isSinglePoint())
            return;
        const auto* p = findPoint(selection_.pointIds.front());
        if (!p)
            return;
        const double newBeat = juce::jmax(0.0, posValue_->getValue());
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::MoveAutomationPointCommand>(
                selection_.laneId, selection_.clipId, selection_.pointIds.front(), newBeat,
                p->value));
    };
    addChildComponent(*posValue_);
}

AutomationPointInspector::~AutomationPointInspector() {
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationPointInspector::onActivated() {
    magda::AutomationManager::getInstance().addListener(this);
}

void AutomationPointInspector::onDeactivated() {
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationPointInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

const std::vector<magda::AutomationPoint>* AutomationPointInspector::sourcePoints() const {
    auto& mgr = magda::AutomationManager::getInstance();
    if (selection_.clipId != magda::INVALID_AUTOMATION_CLIP_ID) {
        if (const auto* clip = mgr.getClip(selection_.clipId))
            return &clip->points;
        return nullptr;
    }
    if (const auto* lane = mgr.getLane(selection_.laneId); lane && lane->isAbsolute())
        return &lane->absolutePoints;
    return nullptr;
}

const magda::AutomationPoint* AutomationPointInspector::findPoint(
    magda::AutomationPointId id) const {
    const auto* pts = sourcePoints();
    if (!pts)
        return nullptr;
    for (const auto& p : *pts)
        if (p.id == id)
            return &p;
    return nullptr;
}

double AutomationPointInspector::normToReal(double normalized) const {
    return magda::ParameterUtils::normalizedToReal(static_cast<float>(normalized), info_);
}

double AutomationPointInspector::realToNorm(double real) const {
    return magda::ParameterUtils::realToNormalized(static_cast<float>(real), info_);
}

void AutomationPointInspector::setSelectedPoints(const magda::AutomationPointSelection& selection) {
    selection_ = selection;
    updateFromSelection();
}

void AutomationPointInspector::automationPointsChanged(magda::AutomationLaneId laneId) {
    if (selection_.isValid() && selection_.laneId == laneId)
        refreshDisplay();
}

void AutomationPointInspector::updateFromSelection() {
    const bool valid = selection_.isValid();
    showControls(valid);
    if (!valid)
        return;

    auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId);
    if (!lane) {
        showControls(false);
        return;
    }
    info_ = magda::getParameterInfoForTarget(lane->target);

    juce::String title = magda::getDisplayNameForTarget(lane->target);
    if (!selection_.isSinglePoint())
        title += " (" + juce::String(selection_.pointIds.size()) + " points)";
    countLabel_.setText(title, juce::dontSendNotification);

    using F = magda::DraggableValueLabel::Format;
    if (info_.scale == magda::ParameterScale::FaderDB ||
        info_.unit == magda::technicalText(magda::TechnicalTextToken::Decibels)) {
        valueValue_->setFormat(F::Decibels);
        valueValue_->setSuffix("");
    } else if (lane->target.kind == magda::ControlTarget::Kind::TrackPan) {
        valueValue_->setFormat(F::Pan);
        valueValue_->setSuffix("");
    } else {
        valueValue_->setFormat(F::Raw);
        valueValue_->setSuffix(info_.unit.isNotEmpty() ? " " + info_.unit : "");
        valueValue_->setDecimalPlaces(2);
    }
    valueValue_->setRange(info_.minValue, info_.maxValue, info_.defaultValue);
    valueValue_->setDoubleClickResetsValue(false);

    // Position field is single-point only (a delta on many points is unclear).
    const bool single = selection_.isSinglePoint();
    posLabel_.setVisible(single);
    posValue_->setVisible(single);

    refreshDisplay();
    resized();
}

void AutomationPointInspector::refreshDisplay() {
    if (!selection_.isValid())
        return;

    const auto* rep = findPoint(selection_.pointIds.front());
    if (!rep)
        return;

    const double real = normToReal(rep->value);
    valueValue_->setValue(real, juce::dontSendNotification);
    valueDragStart_ = real;

    if (selection_.isSinglePoint()) {
        valueValue_->clearTextOverride();
        posValue_->setValue(rep->beatPosition, juce::dontSendNotification);
        posDragStart_ = rep->beatPosition;
    } else {
        // Multiple points: show the range of values across the selection. The
        // field still drags as a delta applied to every selected point.
        double minR = real, maxR = real;
        for (auto id : selection_.pointIds) {
            if (const auto* p = findPoint(id)) {
                const double r = normToReal(p->value);
                minR = juce::jmin(minR, r);
                maxR = juce::jmax(maxR, r);
            }
        }
        if (maxR - minR > 1.0e-9)
            valueValue_->setTextOverride(valueValue_->formatForDisplay(minR) + " .. " +
                                         valueValue_->formatForDisplay(maxR));
        else
            valueValue_->clearTextOverride();
    }
}

void AutomationPointInspector::showControls(bool show) {
    countLabel_.setVisible(show);
    valueLabel_.setVisible(show);
    valueValue_->setVisible(show);
    const bool single = show && selection_.isSinglePoint();
    posLabel_.setVisible(single);
    posValue_->setVisible(single);
}

void AutomationPointInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);
    if (!selection_.isValid())
        return;

    constexpr int LABEL_H = 16;
    constexpr int VALUE_H = 24;
    constexpr int ROW_GAP = 8;
    constexpr int COL_GAP = 8;

    countLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(ROW_GAP);

    // Value | Pos
    {
        auto row = bounds.removeFromTop(LABEL_H);
        int halfW = (row.getWidth() - COL_GAP) / 2;
        valueLabel_.setBounds(row.removeFromLeft(halfW));
        row.removeFromLeft(COL_GAP);
        posLabel_.setBounds(row);
    }
    {
        auto row = bounds.removeFromTop(VALUE_H);
        int halfW = (row.getWidth() - COL_GAP) / 2;
        valueValue_->setBounds(row.removeFromLeft(halfW));
        row.removeFromLeft(COL_GAP);
        posValue_->setBounds(row);
    }
}

}  // namespace magda::daw::ui
