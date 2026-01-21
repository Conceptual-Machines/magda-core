#include "BottomPanel.hpp"

#include "PanelTabBar.hpp"
#include "state/PanelController.hpp"

namespace magda {

BottomPanel::BottomPanel() : TabbedPanel(daw::ui::PanelLocation::Bottom) {
    setName("Bottom Panel");

    // Register as listener for selection changes
    ClipManager::getInstance().addListener(this);
    TrackManager::getInstance().addListener(this);

    // Set initial content based on current selection
    updateContentBasedOnSelection();
}

BottomPanel::~BottomPanel() {
    ClipManager::getInstance().removeListener(this);
    TrackManager::getInstance().removeListener(this);
}

void BottomPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Bottom, collapsed);
}

void BottomPanel::paint(juce::Graphics& g) {
    TabbedPanel::paint(g);
}

void BottomPanel::resized() {
    TabbedPanel::resized();
}

void BottomPanel::clipsChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::clipSelectionChanged(ClipId /*clipId*/) {
    updateContentBasedOnSelection();
}

void BottomPanel::tracksChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::trackSelectionChanged(TrackId /*trackId*/) {
    updateContentBasedOnSelection();
}

void BottomPanel::updateContentBasedOnSelection() {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    ClipId selectedClip = clipManager.getSelectedClip();
    TrackId selectedTrack = trackManager.getSelectedTrack();

    daw::ui::PanelContentType targetContent = daw::ui::PanelContentType::Empty;

    if (selectedClip != INVALID_CLIP_ID) {
        // A clip is selected - show appropriate editor
        const auto* clip = clipManager.getClip(selectedClip);
        if (clip) {
            if (clip->type == ClipType::MIDI) {
                targetContent = daw::ui::PanelContentType::PianoRoll;
            } else if (clip->type == ClipType::Audio) {
                targetContent = daw::ui::PanelContentType::WaveformEditor;
            }
        }
    } else if (selectedTrack != INVALID_TRACK_ID) {
        // Only a track is selected (no clip) - show track chain
        targetContent = daw::ui::PanelContentType::TrackChain;
    }
    // else: nothing selected - show empty

    // Switch to the appropriate content via PanelController
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetContent);
}

juce::Rectangle<int> BottomPanel::getCollapseButtonBounds() {
    if (isCollapsed()) {
        return juce::Rectangle<int>(getWidth() / 2 - 10, 2, 20, 20);
    } else {
        // Collapse button on the right side of the header
        return juce::Rectangle<int>(getWidth() - 28, 4, 20, 20);
    }
}

juce::Rectangle<int> BottomPanel::getTabBarBounds() {
    // No tab bar for bottom panel - content is auto-switched based on selection
    return juce::Rectangle<int>();
}

juce::Rectangle<int> BottomPanel::getContentBounds() {
    return getLocalBounds();
}

}  // namespace magda
