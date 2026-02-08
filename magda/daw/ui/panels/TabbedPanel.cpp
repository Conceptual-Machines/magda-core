#include "TabbedPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "content/MediaExplorerContent.hpp"
#include "content/inspector/InspectorContainer.hpp"

namespace magda::daw::ui {

TabbedPanel::TabbedPanel(PanelLocation location) : location_(location) {
    setName("Tabbed Panel");

    // Setup tab bar
    tabBar_.onTabClicked = [this](int index) {
        PanelController::getInstance().setActiveTab(location_, index);
    };
    addAndMakeVisible(tabBar_);

    // Setup collapse button
    setupCollapseButton();

    // Register as listener
    PanelController::getInstance().addListener(this);

    // Initialize from current state
    updateFromState();
}

TabbedPanel::~TabbedPanel() {
    PanelController::getInstance().removeListener(this);

    // Remove cached content components from child list before unique_ptrs destroy them,
    // to avoid corrupting the parent's child array during destruction.
    for (auto& [type, content] : contentCache_) {
        if (content)
            removeChildComponent(content.get());
    }
    contentCache_.clear();
}

void TabbedPanel::setupCollapseButton() {
    // Use text button as collapse control
    collapseFallbackButton_.setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    collapseFallbackButton_.setColour(juce::TextButton::buttonOnColourId,
                                      DarkTheme::getColour(DarkTheme::BUTTON_HOVER));
    collapseFallbackButton_.setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    collapseFallbackButton_.onClick = [this]() {
        PanelController::getInstance().toggleCollapsed(location_);
    };
    collapseFallbackButton_.setButtonText(getCollapseButtonText());
    addAndMakeVisible(collapseFallbackButton_);
}

void TabbedPanel::paint(juce::Graphics& g) {
    paintBackground(g);
    paintBorder(g);
}

void TabbedPanel::paintBackground(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void TabbedPanel::paintBorder(juce::Graphics& g) {
    g.setColour(DarkTheme::getBorderColour());

    // Draw borders based on panel location
    switch (location_) {
        case PanelLocation::Left:
            g.fillRect(0, 0, getWidth(), 1);                // Top
            g.fillRect(getWidth() - 1, 0, 1, getHeight());  // Right
            break;
        case PanelLocation::Right:
            g.fillRect(0, 0, getWidth(), 1);   // Top
            g.fillRect(0, 0, 1, getHeight());  // Left
            break;
        case PanelLocation::Bottom:
            g.fillRect(0, 0, getWidth(), 1);  // Top
            break;
    }
}

void TabbedPanel::resized() {
    auto bounds = getLocalBounds();

    if (collapsed_) {
        // In collapsed state, just show collapse button
        auto btnBounds = getCollapseButtonBounds();
        collapseFallbackButton_.setBounds(btnBounds);
        tabBar_.setVisible(false);
        if (activeContent_) {
            activeContent_->setVisible(false);
        }
    } else {
        // Normal state: content + tab bar + collapse button
        auto tabBarBounds = getTabBarBounds();
        tabBar_.setBounds(tabBarBounds);
        tabBar_.setVisible(true);

        auto btnBounds = getCollapseButtonBounds();
        collapseFallbackButton_.setBounds(btnBounds);

        auto contentBounds = getContentBounds();
        if (activeContent_) {
            if (contentBounds.getWidth() > 0 && contentBounds.getHeight() > 0) {
                activeContent_->setBounds(contentBounds);
                activeContent_->setVisible(true);
            } else {
                activeContent_->setVisible(false);
            }
        }
    }
}

juce::Rectangle<int> TabbedPanel::getContentBounds() {
    auto bounds = getLocalBounds();
    int tabBarHeight = PanelTabBar::BAR_HEIGHT;

    // Content above tab bar, with margin for collapse button
    auto content = bounds.withTrimmedBottom(tabBarHeight);
    if (content.getHeight() < 0)
        content.setHeight(0);
    return content;
}

juce::Rectangle<int> TabbedPanel::getTabBarBounds() {
    auto bounds = getLocalBounds();
    int tabBarHeight = PanelTabBar::BAR_HEIGHT;

    return bounds.removeFromBottom(tabBarHeight);
}

juce::Rectangle<int> TabbedPanel::getCollapseButtonBounds() {
    if (collapsed_) {
        // Centered in collapsed panel
        switch (location_) {
            case PanelLocation::Left:
            case PanelLocation::Right:
                return juce::Rectangle<int>(2, getHeight() / 2 - 10, 20, 20);
            case PanelLocation::Bottom:
                return juce::Rectangle<int>(getWidth() / 2 - 10, 2, 20, 20);
        }
    } else {
        // Top-right corner
        return juce::Rectangle<int>(getWidth() - 24, 4, 20, 20);
    }
    return {};
}

juce::String TabbedPanel::getCollapseButtonText() const {
    switch (location_) {
        case PanelLocation::Left:
            return collapsed_ ? ">" : "<";
        case PanelLocation::Right:
            return collapsed_ ? "<" : ">";
        case PanelLocation::Bottom:
            return collapsed_ ? "^" : "v";
    }
    return "";
}

void TabbedPanel::panelStateChanged(PanelLocation location, const PanelState& /*state*/) {
    if (location == location_) {
        updateFromState();
    }
}

void TabbedPanel::activeTabChanged(PanelLocation location, int /*tabIndex*/,
                                   PanelContentType contentType) {
    if (location == location_) {
        switchToContent(contentType);
    }
}

void TabbedPanel::panelCollapsedChanged(PanelLocation location, bool collapsed) {
    if (location == location_) {
        collapsed_ = collapsed;
        collapseFallbackButton_.setButtonText(getCollapseButtonText());

        if (onCollapseChanged) {
            onCollapseChanged(collapsed);
        }

        resized();
        repaint();
    }
}

void TabbedPanel::updateFromState() {
    const auto& state = PanelController::getInstance().getPanelState(location_);

    // Update tabs
    tabBar_.setTabs(state.tabs);
    tabBar_.setActiveTab(state.activeTabIndex);

    // Update collapsed state
    if (collapsed_ != state.collapsed) {
        collapsed_ = state.collapsed;
        collapseFallbackButton_.setButtonText(getCollapseButtonText());

        if (onCollapseChanged) {
            onCollapseChanged(collapsed_);
        }
    }

    // Switch to active content
    if (!state.tabs.empty()) {
        switchToContent(state.getActiveContentType());
    }

    resized();
    repaint();
}

void TabbedPanel::switchToContent(PanelContentType type) {
    // Deactivate old content
    if (activeContent_) {
        activeContent_->onDeactivated();
        activeContent_->setVisible(false);
    }

    // Get or create new content
    activeContent_ = getOrCreateContent(type);

    // Activate new content
    if (activeContent_) {
        activeContent_->onActivated();
        if (!collapsed_) {
            activeContent_->setBounds(getContentBounds());
            activeContent_->setVisible(true);
        }
    }

    repaint();
}

PanelContent* TabbedPanel::getOrCreateContent(PanelContentType type) {
    // Check cache
    auto it = contentCache_.find(type);
    if (it != contentCache_.end()) {
        return it->second.get();
    }

    // Create new content
    auto content = PanelContentFactory::getInstance().createContent(type);
    if (content) {
        addAndMakeVisible(*content);
        auto* ptr = content.get();

        // Initialize content with engine/controller references if it supports them
        // (using dynamic_cast to check if content has these methods)
        if (auto* inspectorContent = dynamic_cast<InspectorContainer*>(ptr)) {
            if (audioEngine_) {
                inspectorContent->setAudioEngine(audioEngine_);
            }
            if (timelineController_) {
                inspectorContent->setTimelineController(timelineController_);
            }
        }

        if (auto* mediaExplorerContent = dynamic_cast<MediaExplorerContent*>(ptr)) {
            if (audioEngine_) {
                mediaExplorerContent->setAudioEngine(audioEngine_);
            }
        }

        contentCache_[type] = std::move(content);
        return ptr;
    }

    return nullptr;
}

void TabbedPanel::setAudioEngine(magda::AudioEngine* engine) {
    audioEngine_ = engine;

    // Update any existing content
    for (auto& [type, content] : contentCache_) {
        if (auto* inspectorContent = dynamic_cast<InspectorContainer*>(content.get())) {
            inspectorContent->setAudioEngine(engine);
        }
        if (auto* mediaExplorerContent = dynamic_cast<MediaExplorerContent*>(content.get())) {
            mediaExplorerContent->setAudioEngine(engine);
        }
    }
}

void TabbedPanel::setTimelineController(magda::TimelineController* controller) {
    timelineController_ = controller;

    // Update any existing content
    for (auto& [type, content] : contentCache_) {
        if (auto* inspectorContent = dynamic_cast<InspectorContainer*>(content.get())) {
            inspectorContent->setTimelineController(controller);
        }
    }
}

}  // namespace magda::daw::ui
