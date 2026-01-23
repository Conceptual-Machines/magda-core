#include "AutomationLaneComponent.hpp"

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
    // Draw mode toggle button (pencil)
    drawModeButton_.setButtonText("✎");
    drawModeButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0x00000000));
    drawModeButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFFFFFFF));
    drawModeButton_.setClickingTogglesState(true);
    drawModeButton_.setToggleState(true, juce::dontSendNotification);  // Start in draw mode
    drawModeButton_.setTooltip("Toggle draw mode");
    drawModeButton_.onClick = [this]() {
        bool drawMode = drawModeButton_.getToggleState();
        if (curveEditor_) {
            curveEditor_->setDrawMode(drawMode ? AutomationDrawMode::Pencil
                                               : AutomationDrawMode::Select);
        }
        // Update button appearance
        drawModeButton_.setColour(juce::TextButton::textColourOffId,
                                  drawMode ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF666666));
    };
    addAndMakeVisible(drawModeButton_);

    // Name label
    nameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFCCCCCC));
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel_);

    // Update name
    if (const auto* lane = getLaneInfo()) {
        nameLabel_.setText(lane->getDisplayName(), juce::dontSendNotification);
    }
}

void AutomationLaneComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    juce::Colour bgColour = isSelected_ ? juce::Colour(0xFF2A2A2A) : juce::Colour(0xFF1E1E1E);
    g.fillAll(bgColour);

    // Header background
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

    // Armed indicator
    const auto* lane = getLaneInfo();
    if (lane && lane->armed) {
        g.setColour(juce::Colour(0xFFCC4444));
        g.fillRect(0, 0, 3, HEADER_HEIGHT);
    }
}

void AutomationLaneComponent::resized() {
    auto bounds = getLocalBounds();

    // Header layout
    auto headerBounds = bounds.removeFromTop(HEADER_HEIGHT);
    int buttonSize = HEADER_HEIGHT - 4;
    int margin = 2;

    drawModeButton_.setBounds(margin, margin, buttonSize, buttonSize);

    auto labelBounds = headerBounds;
    labelBounds.removeFromLeft(margin + buttonSize + 4);
    nameLabel_.setBounds(labelBounds.reduced(2));

    // Content area (leave room for resize handle at bottom)
    auto contentBounds = bounds;
    contentBounds.removeFromBottom(RESIZE_HANDLE_HEIGHT);

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

}  // namespace magda
