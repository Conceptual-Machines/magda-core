#include "TabbedPanel.hpp"

#include <BinaryData.h>

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
    // Determine initial icon based on location (collapse direction)
    const char* svgData = BinaryData::collapse_left_svg;
    size_t svgSize = BinaryData::collapse_left_svgSize;

    switch (location_) {
        case PanelLocation::Left:
            svgData = BinaryData::collapse_left_svg;
            svgSize = BinaryData::collapse_left_svgSize;
            break;
        case PanelLocation::Right:
            svgData = BinaryData::collapse_right_svg;
            svgSize = BinaryData::collapse_right_svgSize;
            break;
        case PanelLocation::Bottom:
            svgData = BinaryData::collapse_down_svg;
            svgSize = BinaryData::collapse_down_svgSize;
            break;
    }

    collapseButton_ = std::make_unique<magda::SvgButton>("Collapse", svgData, svgSize);
    collapseButton_->onClick = [this]() {
        PanelController::getInstance().toggleCollapsed(location_);
    };
    addAndMakeVisible(*collapseButton_);
}

void TabbedPanel::updateCollapseIcon() {
    if (!collapseButton_)
        return;

    const char* svgData = nullptr;
    size_t svgSize = 0;

    switch (location_) {
        case PanelLocation::Left:
            svgData = collapsed_ ? BinaryData::collapse_right_svg : BinaryData::collapse_left_svg;
            svgSize =
                collapsed_ ? BinaryData::collapse_right_svgSize : BinaryData::collapse_left_svgSize;
            break;
        case PanelLocation::Right:
            svgData = collapsed_ ? BinaryData::collapse_left_svg : BinaryData::collapse_right_svg;
            svgSize =
                collapsed_ ? BinaryData::collapse_left_svgSize : BinaryData::collapse_right_svgSize;
            break;
        case PanelLocation::Bottom:
            svgData = collapsed_ ? BinaryData::collapse_up_svg : BinaryData::collapse_down_svg;
            svgSize =
                collapsed_ ? BinaryData::collapse_up_svgSize : BinaryData::collapse_down_svgSize;
            break;
    }

    collapseButton_->updateSvgData(svgData, svgSize);
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
        // In collapsed state, just show collapse button centered
        auto btnBounds = getCollapseButtonBounds();
        if (collapseButton_)
            collapseButton_->setBounds(btnBounds);
        tabBar_.setVisible(false);
        if (activeContent_)
            activeContent_->setVisible(false);
    } else {
        // Normal state: tab bar (footer) + collapse button + content
        auto tabBarBounds = getTabBarBounds();
        tabBar_.setBounds(tabBarBounds);
        tabBar_.setVisible(true);

        auto btnBounds = getCollapseButtonBounds();
        if (collapseButton_)
            collapseButton_->setBounds(btnBounds);

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

    switch (location_) {
        case PanelLocation::Left:
        case PanelLocation::Right:
            // Tab bar at bottom (footer)
            return bounds.withTrimmedBottom(tabBarHeight);
        case PanelLocation::Bottom:
            // Tab bar at bottom, collapse button area at top (header)
            bounds.removeFromTop(tabBarHeight);     // header with collapse button
            bounds.removeFromBottom(tabBarHeight);  // tab bar footer
            return bounds;
    }
    return bounds.withTrimmedBottom(tabBarHeight);
}

juce::Rectangle<int> TabbedPanel::getTabBarBounds() {
    auto bounds = getLocalBounds();
    int tabBarHeight = PanelTabBar::BAR_HEIGHT;

    return bounds.removeFromBottom(tabBarHeight);
}

juce::Rectangle<int> TabbedPanel::getCollapseButtonBounds() {
    constexpr int btnSize = 20;

    if (collapsed_) {
        // Centered in the collapsed thin bar
        switch (location_) {
            case PanelLocation::Left:
            case PanelLocation::Right:
                return juce::Rectangle<int>(2, getHeight() / 2 - btnSize / 2, btnSize, btnSize);
            case PanelLocation::Bottom:
                return juce::Rectangle<int>(getWidth() / 2 - btnSize / 2, 2, btnSize, btnSize);
        }
    } else {
        switch (location_) {
            case PanelLocation::Left:
            case PanelLocation::Right: {
                // Footer area — right side of tab bar row
                int tabBarHeight = PanelTabBar::BAR_HEIGHT;
                int y = getHeight() - tabBarHeight + (tabBarHeight - btnSize) / 2;
                return juce::Rectangle<int>(getWidth() - btnSize - 4, y, btnSize, btnSize);
            }
            case PanelLocation::Bottom: {
                // Header area — right side
                int tabBarHeight = PanelTabBar::BAR_HEIGHT;
                int y = (tabBarHeight - btnSize) / 2;
                return juce::Rectangle<int>(getWidth() - btnSize - 4, y, btnSize, btnSize);
            }
        }
    }
    return {};
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
        updateCollapseIcon();

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
        updateCollapseIcon();

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
