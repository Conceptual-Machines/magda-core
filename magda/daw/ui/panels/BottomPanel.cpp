#include "BottomPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "PanelTabBar.hpp"
#include "state/PanelController.hpp"

namespace magda {

BottomPanel::BottomPanel() : TabbedPanel(daw::ui::PanelLocation::Bottom) {
    setName("Bottom Panel");

    // Create sidebar icons (no-op placeholders for now)
    sidebarIcon1_ = std::make_unique<juce::TextButton>("1");
    sidebarIcon1_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    sidebarIcon1_->setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    addAndMakeVisible(sidebarIcon1_.get());

    sidebarIcon2_ = std::make_unique<juce::TextButton>("2");
    sidebarIcon2_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    sidebarIcon2_->setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    addAndMakeVisible(sidebarIcon2_.get());

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

    // Draw sidebar background
    auto sidebarBounds = getLocalBounds().removeFromLeft(SIDEBAR_WIDTH);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(sidebarBounds);

    // Draw separator line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(SIDEBAR_WIDTH - 1, 0.0f, static_cast<float>(getHeight()));
}

void BottomPanel::resized() {
    TabbedPanel::resized();

    // Position sidebar icons at the top of the sidebar
    int iconSize = 24;
    int padding = (SIDEBAR_WIDTH - iconSize) / 2;

    sidebarIcon1_->setBounds(padding, padding, iconSize, iconSize);
    sidebarIcon2_->setBounds(padding, padding + iconSize + 4, iconSize, iconSize);
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
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(SIDEBAR_WIDTH);  // Skip sidebar
    return bounds;
}

}  // namespace magda
