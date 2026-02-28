#include "TabbedPanel.hpp"

#include <BinaryData.h>

#include "../themes/DarkTheme.hpp"
#include "content/MediaExplorerContent.hpp"
#include "content/inspector/InspectorContainer.hpp"

namespace magda::daw::ui {

TabbedPanel::TabbedPanel(PanelLocation location) : location_(location), tabBar_(location) {
    setName("Tabbed Panel");

    // Setup tab bar with callbacks
    tabBar_.onTabClicked = [this](int index) {
        PanelController::getInstance().setActiveTab(location_, index);
    };
    tabBar_.onCollapseClicked = [this]() {
        PanelController::getInstance().toggleCollapsed(location_);
    };
    addAndMakeVisible(tabBar_);

    // Setup expand button for collapsed thin-bar state (side panels only)
    setupExpandButton();

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

void TabbedPanel::setupExpandButton() {
    // Only side panels need an expand button for the collapsed thin-bar state.
    // Bottom panel collapse is handled by FooterBar.
    if (location_ == PanelLocation::Bottom)
        return;

    // Expand button shown when collapsed → show "open" icon
    const char* svgData = (location_ == PanelLocation::Right) ? BinaryData::left_open_svg
                                                              : BinaryData::right_open_svg;
    size_t svgSize = (location_ == PanelLocation::Right) ? BinaryData::left_open_svgSize
                                                         : BinaryData::right_open_svgSize;

    expandButton_ = std::make_unique<magda::SvgButton>("Expand", svgData, svgSize);
    expandButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    expandButton_->onClick = [this]() {
        PanelController::getInstance().toggleCollapsed(location_);
    };
    expandButton_->setVisible(false);
    addAndMakeVisible(*expandButton_);
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
    if (collapsed_) {
        // In collapsed state, show expand button at the footer position (side panels only)
        if (expandButton_) {
            constexpr int btnSize = 20;
            int btnY =
                getHeight() - PanelTabBar::BAR_HEIGHT + (PanelTabBar::BAR_HEIGHT - btnSize) / 2;
            expandButton_->setBounds(2, btnY, btnSize, btnSize);
            expandButton_->setVisible(true);
            expandButton_->toFront(false);
        }
        tabBar_.setVisible(false);
        if (activeContent_)
            activeContent_->setVisible(false);
    } else {
        // Hide expand button in normal state
        if (expandButton_)
            expandButton_->setVisible(false);

        // Normal state: tab bar (footer) + content
        auto tabBarBounds = getTabBarBounds();
        tabBar_.setBounds(tabBarBounds);
        tabBar_.setVisible(true);

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

    // Content above tab bar (footer)
    return bounds.withTrimmedBottom(tabBarHeight);
}

juce::Rectangle<int> TabbedPanel::getTabBarBounds() {
    auto bounds = getLocalBounds();
    int tabBarHeight = PanelTabBar::BAR_HEIGHT;

    return bounds.removeFromBottom(tabBarHeight);
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
        tabBar_.setCollapseState(collapsed);

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
        tabBar_.setCollapseState(collapsed_);

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
