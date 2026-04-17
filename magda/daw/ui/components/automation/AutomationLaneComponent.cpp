#include "AutomationLaneComponent.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../../audio/AutomationCurveSimplifier.hpp"
#include "../../../core/AutomationCommands.hpp"
#include "../../../core/ParameterUtils.hpp"
#include "../../../core/UndoManager.hpp"

namespace magda {

AutomationLaneComponent::AutomationLaneComponent(AutomationLaneId laneId) : laneId_(laneId) {
    setName("AutomationLaneComponent");

    // Register listeners
    AutomationManager::getInstance().addListener(this);
    SelectionManager::getInstance().addListener(this);

    setupHeader();
    rebuildContent();
}

AutomationLaneComponent::~AutomationLaneComponent() {
    AutomationManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
}

void AutomationLaneComponent::setupHeader() {
    // Name label is now painted by TrackHeadersPanel
    // Start curve editor in pencil draw mode by default
    if (curveEditor_) {
        curveEditor_->setDrawMode(AutomationDrawMode::Pencil);
    }
}

void AutomationLaneComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    juce::Colour bgColour = isSelected_ ? juce::Colour(0xFF2A2A2A) : juce::Colour(0xFF1E1E1E);
    g.fillAll(bgColour);

    // Header area - just a simple background (name is painted by TrackHeadersPanel)
    auto headerBounds = bounds.removeFromTop(HEADER_HEIGHT);
    g.setColour(juce::Colour(0xFF252525));
    g.fillRect(headerBounds);

    // Header border
    g.setColour(juce::Colour(0xFF333333));
    g.drawHorizontalLine(HEADER_HEIGHT - 1, 0.0f, static_cast<float>(getWidth()));

    // Bottom border / resize handle area
    auto resizeArea = getResizeHandleArea();
    g.setColour(juce::Colour(0xFF333333));
    g.fillRect(resizeArea);
    g.setColour(juce::Colour(0xFF444444));
    g.drawHorizontalLine(getHeight() - RESIZE_HANDLE_HEIGHT, 0.0f, static_cast<float>(getWidth()));
}

void AutomationLaneComponent::paintOverChildren(juce::Graphics& g) {
    // Scale labels in the left padding area - painted AFTER children so they appear on top
    auto scaleBounds = getLocalBounds();
    scaleBounds.removeFromTop(HEADER_HEIGHT);
    scaleBounds.removeFromBottom(RESIZE_HANDLE_HEIGHT);
    scaleBounds.setWidth(SCALE_LABEL_WIDTH);
    paintScaleLabels(g, scaleBounds);
}

void AutomationLaneComponent::resized() {
    auto bounds = getLocalBounds();

    // Skip header area (name is painted by TrackHeadersPanel)
    bounds.removeFromTop(HEADER_HEIGHT);

    // Content area (leave room for resize handle at bottom)
    auto contentBounds = bounds;
    contentBounds.removeFromBottom(RESIZE_HANDLE_HEIGHT);

    // Curve editor starts after scale labels
    if (curveEditor_) {
        auto curveArea = contentBounds;
        curveArea.removeFromLeft(SCALE_LABEL_WIDTH);
        curveEditor_->setBounds(curveArea);
    }

    updateClipPositions();
}

void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e) {
    // Check for resize handle
    if (isInResizeArea(e.y)) {
        isResizing_ = true;
        resizeStartY_ = e.y;
        const auto* lane = getLaneInfo();
        resizeStartHeight_ = lane ? lane->height : DEFAULT_LANE_HEIGHT;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // Right-click on header shows context menu
    if (e.y < HEADER_HEIGHT && e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }

    // Click on header selects lane
    if (e.y < HEADER_HEIGHT) {
        SelectionManager::getInstance().selectAutomationLane(laneId_);
    }
}

void AutomationLaneComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isResizing_) {
        int deltaY = e.y - resizeStartY_;
        int newHeight = juce::jlimit(MIN_LANE_HEIGHT, MAX_LANE_HEIGHT, resizeStartHeight_ + deltaY);

        // Update lane height in AutomationManager
        AutomationManager::getInstance().setLaneHeight(laneId_, newHeight);

        // Notify parent to update layout
        if (onHeightChanged) {
            onHeightChanged(laneId_, newHeight);
        }
    }
}

void AutomationLaneComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isResizing_) {
        isResizing_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void AutomationLaneComponent::mouseMove(const juce::MouseEvent& e) {
    if (isInResizeArea(e.y)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

bool AutomationLaneComponent::isInResizeArea(int y) const {
    return y >= getHeight() - RESIZE_HANDLE_HEIGHT;
}

bool AutomationLaneComponent::hitTest(int x, int y) {
    juce::ignoreUnused(x);
    // Always handle resize area at the bottom - prevent child components from receiving these
    // clicks
    if (isInResizeArea(y)) {
        return true;
    }
    // For other areas, use default behavior (let children handle if they're there)
    return Component::hitTest(x, y);
}

juce::Rectangle<int> AutomationLaneComponent::getResizeHandleArea() const {
    return juce::Rectangle<int>(0, getHeight() - RESIZE_HANDLE_HEIGHT, getWidth(),
                                RESIZE_HANDLE_HEIGHT);
}

// AutomationManagerListener
void AutomationLaneComponent::automationLanesChanged() {
    rebuildContent();
    repaint();
}

void AutomationLaneComponent::automationLanePropertyChanged(AutomationLaneId laneId) {
    if (laneId == laneId_) {
        if (const auto* lane = getLaneInfo()) {
            nameLabel_.setText(lane->getDisplayName(), juce::dontSendNotification);
        }
        repaint();
    }
}

void AutomationLaneComponent::automationClipsChanged(AutomationLaneId laneId) {
    if (laneId == laneId_) {
        rebuildClipComponents();
    }
}

// SelectionManagerListener
void AutomationLaneComponent::selectionTypeChanged(SelectionType newType) {
    juce::ignoreUnused(newType);
    syncSelectionState();
}

void AutomationLaneComponent::automationLaneSelectionChanged(
    const AutomationLaneSelection& selection) {
    juce::ignoreUnused(selection);
    syncSelectionState();
}

void AutomationLaneComponent::setPixelsPerSecond(double pps) {
    pixelsPerSecond_ = pps;
    if (curveEditor_) {
        curveEditor_->setPixelsPerSecond(pps);
    }
    updateClipPositions();
}

void AutomationLaneComponent::setPixelsPerBeat(double ppb) {
    pixelsPerBeat_ = ppb;
    if (curveEditor_) {
        curveEditor_->setPixelsPerBeat(ppb);
    }
    updateClipPositions();
}

void AutomationLaneComponent::setTempoBPM(double bpm) {
    tempoBPM_ = bpm;
    if (curveEditor_) {
        curveEditor_->setTempoBPM(bpm);
    }
}

int AutomationLaneComponent::getPreferredHeight() const {
    const auto* lane = getLaneInfo();
    if (lane) {
        return lane->expanded ? (HEADER_HEIGHT + lane->height) : HEADER_HEIGHT;
    }
    return HEADER_HEIGHT + DEFAULT_LANE_HEIGHT;
}

bool AutomationLaneComponent::isExpanded() const {
    const auto* lane = getLaneInfo();
    return lane ? lane->expanded : true;
}

const AutomationLaneInfo* AutomationLaneComponent::getLaneInfo() const {
    return AutomationManager::getInstance().getLane(laneId_);
}

void AutomationLaneComponent::rebuildContent() {
    const auto* lane = getLaneInfo();
    if (!lane)
        return;

    // Update name
    nameLabel_.setText(lane->getDisplayName(), juce::dontSendNotification);

    // Create appropriate content based on lane type
    if (lane->isAbsolute()) {
        // Absolute lane: single curve editor
        curveEditor_ = std::make_unique<AutomationCurveEditor>(laneId_);
        curveEditor_->setPixelsPerSecond(pixelsPerSecond_);
        curveEditor_->setPixelsPerBeat(pixelsPerBeat_);
        curveEditor_->setTempoBPM(tempoBPM_);
        curveEditor_->snapTimeToGrid = [this](double x) {
            if (snapTimeToGrid)
                return snapTimeToGrid(x);
            return x;
        };
        curveEditor_->getGridSpacingBeats = [this]() -> double {
            if (getGridSpacingBeats)
                return getGridSpacingBeats();
            return 1.0;
        };
        curveEditor_->setDrawMode(AutomationDrawMode::Pencil);  // Default to draw mode
        addAndMakeVisible(curveEditor_.get());

        clipComponents_.clear();
    } else {
        // Clip-based lane: clip components
        curveEditor_.reset();
        rebuildClipComponents();
    }

    resized();
}

void AutomationLaneComponent::rebuildClipComponents() {
    clipComponents_.clear();

    const auto* lane = getLaneInfo();
    if (!lane || !lane->isClipBased())
        return;

    auto& manager = AutomationManager::getInstance();
    for (auto clipId : lane->clipIds) {
        const auto* clip = manager.getClip(clipId);
        if (!clip)
            continue;

        auto cc = std::make_unique<AutomationClipComponent>(clipId, this);
        cc->setPixelsPerSecond(pixelsPerSecond_);
        addAndMakeVisible(cc.get());
        clipComponents_.push_back(std::move(cc));
    }

    updateClipPositions();
}

void AutomationLaneComponent::updateClipPositions() {
    auto& manager = AutomationManager::getInstance();
    int contentY = HEADER_HEIGHT;
    int contentHeight = getHeight() - HEADER_HEIGHT - RESIZE_HANDLE_HEIGHT;

    for (auto& cc : clipComponents_) {
        const auto* clip = manager.getClip(cc->getClipId());
        if (!clip)
            continue;

        int x = SCALE_LABEL_WIDTH + static_cast<int>(clip->startTime * pixelsPerBeat_);
        int width = static_cast<int>(clip->length * pixelsPerBeat_);
        cc->setBounds(x, contentY, juce::jmax(10, width), juce::jmax(10, contentHeight));
    }
}

void AutomationLaneComponent::syncSelectionState() {
    auto& selectionManager = SelectionManager::getInstance();

    bool wasSelected = isSelected_;
    isSelected_ = selectionManager.getSelectionType() == SelectionType::AutomationLane &&
                  selectionManager.getAutomationLaneSelection().laneId == laneId_;

    if (wasSelected != isSelected_) {
        repaint();
    }
}

void AutomationLaneComponent::showContextMenu() {
    juce::PopupMenu menu;

    enum MenuItem { HideLane = 1, SimplifyLane = 2 };

    menu.addItem(HideLane, "Hide Lane");

    // Only offer Simplify when there's meaningful data to reduce.
    const auto* lane = getLaneInfo();
    const bool canSimplify =
        (lane != nullptr) && lane->isAbsolute() && lane->absolutePoints.size() > 2;
    menu.addItem(SimplifyLane, "Simplify Curve", canSimplify);

    auto options = juce::PopupMenu::Options().withTargetComponent(this);

    auto laneId = laneId_;  // Capture for lambda
    menu.showMenuAsync(options, [laneId](int result) {
        switch (result) {
            case HideLane:
                juce::MessageManager::callAsync(
                    [laneId]() { AutomationManager::getInstance().setLaneVisible(laneId, false); });
                break;
            case SimplifyLane:
                juce::MessageManager::callAsync(
                    [laneId]() { AutomationLaneComponent::simplifyLane(laneId, 0.01); });
                break;
            default:
                break;
        }
    });
}

void AutomationLaneComponent::simplifyLane(AutomationLaneId laneId, double epsilon,
                                           const std::vector<AutomationPointId>& pointIdFilter) {
    auto& autoMgr = AutomationManager::getInstance();
    const auto* lane = autoMgr.getLane(laneId);
    if (lane == nullptr || !lane->isAbsolute() || lane->absolutePoints.size() <= 2)
        return;

    // Snapshot IDs + (time, value) pairs sorted by time. When a filter is
    // supplied we still sort everything by time, but only points inside the
    // filter are candidates for removal — others are pinned as "keep" so the
    // user's unselected points survive intact.
    struct Entry {
        AutomationPointId id;
        AutomationCurveSimplifier::Point p;
        bool inScope;
    };

    std::vector<AutomationPointId> filterSorted(pointIdFilter.begin(), pointIdFilter.end());
    std::sort(filterSorted.begin(), filterSorted.end());
    const bool hasFilter = !filterSorted.empty();

    std::vector<Entry> entries;
    entries.reserve(lane->absolutePoints.size());
    for (const auto& pt : lane->absolutePoints) {
        bool inScope =
            !hasFilter || std::binary_search(filterSorted.begin(), filterSorted.end(), pt.id);
        entries.push_back({pt.id, {pt.time, pt.value}, inScope});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.p.time < b.p.time; });

    std::vector<AutomationCurveSimplifier::Point> polyline;
    polyline.reserve(entries.size());
    for (const auto& e : entries)
        polyline.push_back(e.p);

    auto keepIdx = AutomationCurveSimplifier::simplify(polyline, epsilon);

    std::vector<bool> keep(entries.size(), false);
    for (auto idx : keepIdx)
        keep[idx] = true;
    // Pin out-of-scope points so the filtered variant never deletes them.
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].inScope)
            keep[i] = true;
    }

    size_t drops = 0;
    for (bool k : keep)
        if (!k)
            ++drops;
    if (drops == 0)
        return;

    auto& undo = UndoManager::getInstance();
    undo.beginCompoundOperation("Simplify Automation Curve");
    autoMgr.beginNotificationBatch();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (keep[i])
            continue;
        auto cmd = std::make_unique<DeleteAutomationPointCommand>(
            laneId, INVALID_AUTOMATION_CLIP_ID, entries[i].id);
        undo.executeCommand(std::move(cmd));
    }
    autoMgr.endNotificationBatch();
    undo.endCompoundOperation();
}

// Convert real value to normalized position using ParameterInfo
static double realToNormalizedForTarget(double realValue, const ParameterInfo& info) {
    return static_cast<double>(
        ParameterUtils::realToNormalized(static_cast<float>(realValue), info));
}

void AutomationLaneComponent::paintScaleLabels(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.getHeight() <= 0 || area.getWidth() < 25)
        return;

    // Background for scale area
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRect(area);

    // Right border
    g.setColour(juce::Colour(0xFF333333));
    g.drawVerticalLine(area.getRight() - 1, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

    const auto* lane = getLaneInfo();
    if (!lane)
        return;

    // Get parameter info for this target
    ParameterInfo paramInfo = lane->target.getParameterInfo();

    g.setColour(juce::Colour(0xFF888888));
    g.setFont(9.0f);

    // Helper lambda to draw a label at a real value position
    auto drawLabelAtRealValue = [&](double realValue, const juce::String& label) {
        double normalizedValue = realToNormalizedForTarget(realValue, paramInfo);
        int y = area.getY() + valueToPixel(normalizedValue, area.getHeight());

        auto labelBounds = juce::Rectangle<int>(2, y - 5, area.getWidth() - 6, 10);
        if (labelBounds.getY() < area.getY())
            labelBounds.setY(area.getY());
        if (labelBounds.getBottom() > area.getBottom())
            labelBounds.setY(area.getBottom() - 10);

        g.drawText(label, labelBounds, juce::Justification::centredRight);
        g.drawHorizontalLine(y, static_cast<float>(area.getRight() - 4),
                             static_cast<float>(area.getRight() - 1));
    };

    // Choose label values based on parameter scale type
    if (paramInfo.scale == ParameterScale::FaderDB) {
        // Standard dB values for fader-style volume
        std::vector<std::pair<double, juce::String>> dbLabels;

        if (area.getHeight() >= 100) {
            dbLabels = {{6.0, "6"},    {0.0, "0"},    {-6.0, "6"},   {-12.0, "12"},
                        {-24.0, "24"}, {-48.0, "48"}, {-60.0, "inf"}};
        } else if (area.getHeight() >= 60) {
            dbLabels = {{6.0, "6"}, {0.0, "0"}, {-12.0, "12"}, {-60.0, "inf"}};
        } else {
            dbLabels = {{6.0, "6"}, {0.0, "0"}, {-60.0, "inf"}};
        }

        for (const auto& [db, label] : dbLabels) {
            drawLabelAtRealValue(db, label);
        }
    } else if (lane->target.type == AutomationTargetType::TrackPan) {
        // Pan: L, C, R (real values -1, 0, +1)
        drawLabelAtRealValue(1.0, "R");
        drawLabelAtRealValue(0.0, "C");
        drawLabelAtRealValue(-1.0, "L");
    } else if (paramInfo.scale == ParameterScale::Discrete && !paramInfo.choices.empty()) {
        // Discrete choices
        int numChoices = static_cast<int>(paramInfo.choices.size());
        int maxLabels = juce::jmin(numChoices, area.getHeight() >= 80 ? 5 : 3);
        int step = juce::jmax(1, numChoices / maxLabels);

        for (int i = 0; i < numChoices; i += step) {
            drawLabelAtRealValue(static_cast<double>(i), paramInfo.choices[static_cast<size_t>(i)]);
        }
    } else {
        std::vector<double> realValuesForLabels;

        if (paramInfo.isBipolar()) {
            // Pick the larger |bound| so ±max and ±half land on the lane
            // regardless of any asymmetry in the underlying range.
            float absMax = std::max(std::abs(paramInfo.minValue), std::abs(paramInfo.maxValue));
            realValuesForLabels = {absMax, absMax * 0.5, 0.0, -absMax * 0.5, -absMax};
        } else {
            // Unipolar: evenly spaced in normalized space.
            for (double norm : {1.0, 0.75, 0.5, 0.25, 0.0}) {
                realValuesForLabels.push_back(static_cast<double>(
                    ParameterUtils::normalizedToReal(static_cast<float>(norm), paramInfo)));
            }
        }

        for (double realValue : realValuesForLabels) {
            // Clamp to range and convert back to normalized for positioning.
            double clamped = juce::jlimit(static_cast<double>(paramInfo.minValue),
                                          static_cast<double>(paramInfo.maxValue), realValue);
            double normValue = static_cast<double>(
                ParameterUtils::realToNormalized(static_cast<float>(clamped), paramInfo));
            int y = area.getY() + valueToPixel(normValue, area.getHeight());

            auto labelBounds = juce::Rectangle<int>(2, y - 5, area.getWidth() - 6, 10);
            if (labelBounds.getY() < area.getY())
                labelBounds.setY(area.getY());
            if (labelBounds.getBottom() > area.getBottom())
                labelBounds.setY(area.getBottom() - 10);

            // Show the real value using the plugin's display text if available,
            // otherwise unit or fallback percentage.
            juce::String label;
            if (!paramInfo.valueTable.empty()) {
                int idx = juce::jlimit(
                    0, static_cast<int>(paramInfo.valueTable.size()) - 1,
                    static_cast<int>(std::round(normValue * (paramInfo.valueTable.size() - 1))));
                label = paramInfo.valueTable[static_cast<size_t>(idx)].trim();
            }
            if (label.isEmpty()) {
                juce::String numberText;
                if (paramInfo.isBipolar() && std::abs(clamped) > 0.001) {
                    numberText = (clamped > 0 ? "+" : "-") +
                                 juce::String(static_cast<int>(std::round(std::abs(clamped))));
                } else {
                    numberText = juce::String(static_cast<int>(std::round(clamped)));
                }
                label =
                    numberText + (paramInfo.unit.isNotEmpty() ? paramInfo.unit : juce::String("%"));
            }

            g.drawText(label, labelBounds, juce::Justification::centredRight);
            g.drawHorizontalLine(y, static_cast<float>(area.getRight() - 4),
                                 static_cast<float>(area.getRight() - 1));
        }
    }
}

juce::String AutomationLaneComponent::formatScaleValue(double normalizedValue) const {
    const auto* lane = getLaneInfo();
    if (!lane)
        return juce::String(static_cast<int>(normalizedValue * 100)) + "%";

    // Get parameter info and convert to real value
    ParameterInfo paramInfo = lane->target.getParameterInfo();
    float realValue =
        ParameterUtils::normalizedToReal(static_cast<float>(normalizedValue), paramInfo);

    // Use compact formatting for automation lane labels
    switch (paramInfo.scale) {
        case ParameterScale::FaderDB: {
            // Volume: show absolute dB value (no sign)
            if (normalizedValue <= 0.001) {
                return "inf";
            }
            return juce::String(static_cast<int>(std::abs(std::round(realValue))));
        }

        case ParameterScale::Boolean:
            return realValue >= 0.5f ? "On" : "Off";

        case ParameterScale::Discrete: {
            int index = static_cast<int>(std::round(realValue));
            if (index >= 0 && index < static_cast<int>(paramInfo.choices.size())) {
                return paramInfo.choices[static_cast<size_t>(index)];
            }
            return juce::String(index);
        }

        default:
            break;
    }

    // Handle pan specially (unit is empty but it's a special case)
    if (lane->target.type == AutomationTargetType::TrackPan) {
        if (realValue < -0.02f) {
            int percent = static_cast<int>(std::abs(realValue) * 100);
            return juce::String(percent) + "L";
        } else if (realValue > 0.02f) {
            int percent = static_cast<int>(realValue * 100);
            return juce::String(percent) + "R";
        }
        return "C";
    }

    DBG("[LANE-FMT] norm=" << normalizedValue << " real=" << realValue << " unit='"
                           << paramInfo.unit << "'"
                           << " vtSize=" << paramInfo.valueTable.size()
                           << " scale=" << static_cast<int>(paramInfo.scale)
                           << " lane=" << lane->getDisplayName());

    // Live plugin display text — exact, no quantization.
    if (paramInfo.displayText) {
        auto text = paramInfo.displayText->format(static_cast<float>(normalizedValue));
        if (text.isNotEmpty())
            return text;
    }

    // Value table fallback.
    if (!paramInfo.valueTable.empty()) {
        int idx = juce::jlimit(0, static_cast<int>(paramInfo.valueTable.size()) - 1,
                               static_cast<int>(std::round(static_cast<float>(normalizedValue) *
                                                           (paramInfo.valueTable.size() - 1))));
        auto text = paramInfo.valueTable[static_cast<size_t>(idx)].trim();
        if (text.isNotEmpty())
            return text;
    }

    // Generic: show the real value in the parameter's unit.
    // Fall back to percentage only for unit-less parameters.
    if (paramInfo.unit.isNotEmpty()) {
        int rounded = static_cast<int>(std::round(realValue));
        juce::String numberText;
        if (paramInfo.isBipolar() && rounded > 0)
            numberText = "+" + juce::String(rounded);
        else
            numberText = juce::String(rounded);
        return numberText + paramInfo.unit;
    }
    return juce::String(static_cast<int>(normalizedValue * 100)) + "%";
}

int AutomationLaneComponent::valueToPixel(double value, int areaHeight) const {
    return static_cast<int>((1.0 - value) * areaHeight);
}

}  // namespace magda
