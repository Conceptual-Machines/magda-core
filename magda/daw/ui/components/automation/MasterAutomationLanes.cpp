#include "MasterAutomationLanes.hpp"

#include <algorithm>
#include <cmath>

#include "AutomationLaneComponent.hpp"
#include "core/AutomationInfo.hpp"

namespace magda {

namespace {

int laneHeightPx(const AutomationLaneInfo& lane, double verticalZoom) {
    return lane.expanded ? (AutomationLaneComponent::HEADER_HEIGHT +
                            static_cast<int>(lane.height * verticalZoom) +
                            AutomationLaneComponent::RESIZE_HANDLE_HEIGHT)
                         : AutomationLaneComponent::HEADER_HEIGHT;
}

}  // namespace

std::vector<AutomationLaneId> visibleMasterAutomationLanes() {
    auto& manager = AutomationManager::getInstance();
    std::vector<AutomationLaneId> result;
    // Honour the global "Hide All Automation Lanes" override, same as the
    // per-track lanes, so the toggle affects the master band too.
    if (!manager.isGlobalLaneVisibilityEnabled())
        return result;
    // Edit-scoped lanes (Tempo) are project-global concerns and live at the top
    // of the master band rather than in a separate pinned block.
    for (auto laneId : manager.getEditScopedLanes()) {
        const auto* lane = manager.getLane(laneId);
        if (lane && lane->visible)
            result.push_back(laneId);
    }
    for (auto laneId : manager.getLanesForTrack(MASTER_TRACK_ID)) {
        const auto* lane = manager.getLane(laneId);
        if (lane && lane->visible)
            result.push_back(laneId);
    }
    return result;
}

int masterAutomationBandHeight(double verticalZoom) {
    auto& manager = AutomationManager::getInstance();
    int total = 0;
    for (auto laneId : visibleMasterAutomationLanes()) {
        if (const auto* lane = manager.getLane(laneId))
            total += laneHeightPx(*lane, verticalZoom);
    }
    return total;
}

// ============================================================================
// MasterAutomationHeaderPanel
// ============================================================================

MasterAutomationHeaderPanel::MasterAutomationHeaderPanel() {
    AutomationManager::getInstance().addListener(this);
    rebuildButtons();
}

MasterAutomationHeaderPanel::~MasterAutomationHeaderPanel() {
    AutomationManager::getInstance().removeListener(this);
}

void MasterAutomationHeaderPanel::setVerticalZoom(double zoom) {
    if (std::abs(zoom - verticalZoom_) < 1e-6)
        return;
    verticalZoom_ = zoom;
    layoutButtons();
    repaint();
}

void MasterAutomationHeaderPanel::paint(juce::Graphics& g) {
    auto& manager = AutomationManager::getInstance();
    int y = 0;
    for (auto laneId : visibleMasterAutomationLanes()) {
        const auto* lane = manager.getLane(laneId);
        if (!lane)
            continue;
        int h = laneHeightPx(*lane, verticalZoom_);
        // Master-band lanes carry the resize handle on the top edge, so inset
        // the header below it to line up with the lane content.
        paintAutomationLaneHeader(g, *lane, y, getWidth(), h,
                                  AutomationLaneComponent::RESIZE_HANDLE_HEIGHT);
        y += h;
    }
}

void MasterAutomationHeaderPanel::resized() {
    layoutButtons();
}

void MasterAutomationHeaderPanel::automationLanesChanged() {
    rebuildButtons();
    layoutButtons();
    repaint();
}

void MasterAutomationHeaderPanel::automationLanePropertyChanged(AutomationLaneId /*laneId*/) {
    // Re-sync toggle visuals (bypass / snap) and reposition; a collapse/expand
    // changes button visibility.
    rebuildButtons();
    layoutButtons();
    repaint();
}

void MasterAutomationHeaderPanel::rebuildButtons() {
    auto wanted = visibleMasterAutomationLanes();

    // Drop orphans.
    buttons_.erase(std::remove_if(buttons_.begin(), buttons_.end(),
                                  [&](const std::unique_ptr<AutoLaneHeaderButtons>& entry) {
                                      return std::find(wanted.begin(), wanted.end(),
                                                       entry->laneId) == wanted.end();
                                  }),
                   buttons_.end());

    auto& manager = AutomationManager::getInstance();
    for (auto laneId : wanted) {
        auto existing = std::find_if(
            buttons_.begin(), buttons_.end(),
            [&](const std::unique_ptr<AutoLaneHeaderButtons>& e) { return e->laneId == laneId; });
        if (existing == buttons_.end())
            buttons_.push_back(makeAutoLaneHeaderButtons(laneId, *this));
    }

    for (auto& entry : buttons_) {
        if (const auto* lane = manager.getLane(entry->laneId))
            syncAutoLaneHeaderButtonStates(*entry, *lane);
    }
}

void MasterAutomationHeaderPanel::layoutButtons() {
    auto& manager = AutomationManager::getInstance();
    int y = 0;
    for (auto laneId : visibleMasterAutomationLanes()) {
        const auto* lane = manager.getLane(laneId);
        if (!lane)
            continue;
        auto it = std::find_if(
            buttons_.begin(), buttons_.end(),
            [&](const std::unique_ptr<AutoLaneHeaderButtons>& e) { return e->laneId == laneId; });
        if (it != buttons_.end())
            layoutAutoLaneHeaderButtons(**it, *lane, y,
                                        AutomationLaneComponent::RESIZE_HANDLE_HEIGHT);
        y += laneHeightPx(*lane, verticalZoom_);
    }
}

// ============================================================================
// MasterAutomationContentPanel
// ============================================================================

MasterAutomationContentPanel::MasterAutomationContentPanel() {
    AutomationManager::getInstance().addListener(this);
    rebuildLanes();
}

MasterAutomationContentPanel::~MasterAutomationContentPanel() {
    AutomationManager::getInstance().removeListener(this);
}

void MasterAutomationContentPanel::setPixelsPerBeat(double pixelsPerBeat) {
    pixelsPerBeat_ = pixelsPerBeat;
    for (auto& entry : lanes_)
        if (entry.component)
            entry.component->setPixelsPerBeat(pixelsPerBeat);
}

void MasterAutomationContentPanel::setTempoBPM(double bpm) {
    tempoBPM_ = bpm;
    for (auto& entry : lanes_)
        if (entry.component)
            entry.component->setTempoBPM(bpm);
}

void MasterAutomationContentPanel::setVerticalZoom(double zoom) {
    if (std::abs(zoom - verticalZoom_) < 1e-6)
        return;
    verticalZoom_ = zoom;
    layoutLanes();
}

void MasterAutomationContentPanel::setTimelineWidth(int widthPx) {
    if (widthPx == timelineWidth_)
        return;
    timelineWidth_ = widthPx;
    layoutLanes();
}

void MasterAutomationContentPanel::resized() {
    layoutLanes();
}

void MasterAutomationContentPanel::automationLanesChanged() {
    rebuildLanes();
    if (onBandHeightChanged)
        onBandHeightChanged();
}

void MasterAutomationContentPanel::automationLanePropertyChanged(AutomationLaneId /*laneId*/) {
    // A visibility toggle changes which lanes exist (and the band height) but
    // arrives as a property change, not a lanes-changed notification — rebuild
    // when the visible set differs, otherwise just relayout (height/expand).
    auto wanted = visibleMasterAutomationLanes();
    bool sameSet = wanted.size() == lanes_.size();
    for (size_t i = 0; sameSet && i < wanted.size(); ++i)
        sameSet = (wanted[i] == lanes_[i].laneId);

    if (sameSet)
        layoutLanes();
    else
        rebuildLanes();

    if (onBandHeightChanged)
        onBandHeightChanged();
}

void MasterAutomationContentPanel::rebuildLanes() {
    for (auto& entry : lanes_)
        if (entry.component)
            removeChildComponent(entry.component.get());
    lanes_.clear();

    for (auto laneId : visibleMasterAutomationLanes()) {
        LaneEntry entry;
        entry.laneId = laneId;
        entry.component = std::make_unique<AutomationLaneComponent>(laneId);
        entry.component->setPixelsPerBeat(pixelsPerBeat_);
        entry.component->setTempoBPM(tempoBPM_);
        entry.component->snapBeatToGrid = snapBeatToGrid;
        entry.component->getGridSpacingBeats = getGridSpacingBeats;
        // The master band is pinned above the master strip and grows upward, so
        // the resize grab edge sits on top of each lane.
        entry.component->setResizeHandleAtTop(true);
        entry.component->onHeightChanged = [this](AutomationLaneId, int) {
            layoutLanes();
            if (onBandHeightChanged)
                onBandHeightChanged();
        };
        addAndMakeVisible(*entry.component);
        lanes_.push_back(std::move(entry));
    }
    layoutLanes();
}

void MasterAutomationContentPanel::layoutLanes() {
    auto& manager = AutomationManager::getInstance();
    int y = 0;
    for (auto& entry : lanes_) {
        const auto* lane = manager.getLane(entry.laneId);
        if (!lane || !entry.component)
            continue;
        int h = laneHeightPx(*lane, verticalZoom_);
        entry.component->setBounds(0, y, timelineWidth_, h);
        entry.component->setPixelsPerBeat(pixelsPerBeat_);
        entry.component->setTempoBPM(tempoBPM_);
        y += h;
    }
    // Size to the full timeline so the enclosing viewport scrolls horizontally.
    setSize(timelineWidth_, y);
}

}  // namespace magda
