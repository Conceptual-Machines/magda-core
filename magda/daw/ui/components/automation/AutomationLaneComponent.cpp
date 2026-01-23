#include "AutomationLaneComponent.hpp"

#include <cmath>

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

    // Curve editor fills full content area - scale labels are painted on top
    if (curveEditor_) {
        curveEditor_->setBounds(contentBounds);
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
        curveEditor_->snapTimeToGrid = snapTimeToGrid;
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

        int x = static_cast<int>(clip->startTime * pixelsPerSecond_);
        int width = static_cast<int>(clip->length * pixelsPerSecond_);
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

    // Hide Lane option
    menu.addItem(1, "Hide Lane");

    // Show menu
    auto options = juce::PopupMenu::Options().withTargetComponent(this);

    auto laneId = laneId_;  // Capture for lambda
    menu.showMenuAsync(options, [laneId](int result) {
        if (result == 1) {
            // Defer to avoid destroying component during callback
            juce::MessageManager::callAsync(
                [laneId]() { AutomationManager::getInstance().setLaneVisible(laneId, false); });
        }
    });
}

void AutomationLaneComponent::paintScaleLabels(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.getHeight() <= 0)
        return;

    // Background for scale area
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRect(area);

    // Right border
    g.setColour(juce::Colour(0xFF333333));
    g.drawVerticalLine(area.getRight() - 1, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

    // Draw scale labels at key positions: 100%, 50%, 0%
    g.setColour(juce::Colour(0xFF888888));
    g.setFont(9.0f);

    const double values[] = {1.0, 0.5, 0.0};
    for (double value : values) {
        int y = area.getY() + valueToPixel(value, area.getHeight());
        juce::String label = formatScaleValue(value);

        // Draw label right-aligned with small margin
        auto labelBounds = juce::Rectangle<int>(2, y - 5, area.getWidth() - 6, 10);

        // Constrain to area
        if (labelBounds.getY() < area.getY()) {
            labelBounds.setY(area.getY());
        }
        if (labelBounds.getBottom() > area.getBottom()) {
            labelBounds.setY(area.getBottom() - 10);
        }

        g.drawText(label, labelBounds, juce::Justification::centredRight);

        // Draw small tick mark
        g.drawHorizontalLine(y, static_cast<float>(area.getRight() - 4),
                             static_cast<float>(area.getRight() - 1));
    }
}

juce::String AutomationLaneComponent::formatScaleValue(double normalizedValue) const {
    const auto* lane = getLaneInfo();
    if (!lane)
        return juce::String(static_cast<int>(normalizedValue * 100)) + "%";

    switch (lane->target.type) {
        case AutomationTargetType::TrackVolume: {
            // Volume: 0.8 = 0dB, 0 = -inf
            if (normalizedValue <= 0.001) {
                return "-inf";
            }
            double dB = 20.0 * std::log10(normalizedValue / 0.8);
            if (dB > 0) {
                return "+" + juce::String(static_cast<int>(std::round(dB)));
            }
            return juce::String(static_cast<int>(std::round(dB)));
        }

        case AutomationTargetType::TrackPan: {
            // Pan: 0 = full left, 0.5 = center, 1 = full right
            if (normalizedValue < 0.48) {
                int percent = static_cast<int>((0.5 - normalizedValue) * 200);
                return juce::String(percent) + "L";
            } else if (normalizedValue > 0.52) {
                int percent = static_cast<int>((normalizedValue - 0.5) * 200);
                return juce::String(percent) + "R";
            }
            return "C";
        }

        default:
            // Generic 0-100%
            return juce::String(static_cast<int>(normalizedValue * 100)) + "%";
    }
}

int AutomationLaneComponent::valueToPixel(double value, int areaHeight) const {
    return static_cast<int>((1.0 - value) * areaHeight);
}

}  // namespace magda
