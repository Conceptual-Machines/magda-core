#include "PanelTabBar.hpp"

#include "../themes/DarkTheme.hpp"
#include "BinaryData.h"

namespace magda::daw::ui {

namespace {
// Helper to get SVG data for content type
struct SvgIconData {
    const char* data = nullptr;
    int size = 0;
};

SvgIconData getSvgForContentType(PanelContentType type) {
    switch (type) {
        case PanelContentType::PluginBrowser:
            return {BinaryData::plug_svg, BinaryData::plug_svgSize};
        case PanelContentType::MediaExplorer:
            return {BinaryData::browser_svg, BinaryData::browser_svgSize};
        case PanelContentType::PresetBrowser:
            return {BinaryData::preset_svg, BinaryData::preset_svgSize};
        case PanelContentType::Inspector:
            return {BinaryData::info_svg, BinaryData::info_svgSize};
        case PanelContentType::AIChatConsole:
            return {BinaryData::console_svg, BinaryData::console_svgSize};
        case PanelContentType::ScriptingConsole:
            return {BinaryData::script_svg, BinaryData::script_svgSize};
        case PanelContentType::TrackChain:
            return {BinaryData::plug_svg, BinaryData::plug_svgSize};
        case PanelContentType::PianoRoll:
            return {BinaryData::script_svg, BinaryData::script_svgSize};
        case PanelContentType::WaveformEditor:
            return {BinaryData::sinewave_svg, BinaryData::sinewave_svgSize};
        case PanelContentType::DrumGridClipView:
            return {BinaryData::script_svg, BinaryData::script_svgSize};
        case PanelContentType::AudioClipProperties:
            return {BinaryData::knob_svg, BinaryData::knob_svgSize};
        case PanelContentType::Empty:
            break;
    }
    return {};
}
}  // namespace

PanelTabBar::PanelTabBar(PanelLocation location) : location_(location) {
    setName("Panel Tab Bar");
    setupCollapseButton();
}

void PanelTabBar::paint(juce::Graphics& g) {
    // Draw background
    g.fillAll(DarkTheme::getPanelBackgroundColour().darker(0.1f));

    // Draw top border
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, 0, getWidth(), 1);
}

void PanelTabBar::resized() {
    // Reserve space for collapse button at the appropriate edge
    auto availableBounds = getLocalBounds();
    constexpr int collapseMargin = COLLAPSE_BUTTON_SIZE + 8;

    if (collapseButton_) {
        int btnY = (getHeight() - COLLAPSE_BUTTON_SIZE) / 2;

        if (location_ == PanelLocation::Left) {
            // PanelLocation::Left is used by RightPanel: collapse button on the LEFT edge
            collapseButton_->setBounds(4, btnY, COLLAPSE_BUTTON_SIZE, COLLAPSE_BUTTON_SIZE);
            availableBounds.removeFromLeft(collapseMargin);
        } else {
            // PanelLocation::Right is used by LeftPanel: collapse button on the RIGHT edge
            collapseButton_->setBounds(getWidth() - COLLAPSE_BUTTON_SIZE - 4, btnY,
                                       COLLAPSE_BUTTON_SIZE, COLLAPSE_BUTTON_SIZE);
            availableBounds.removeFromRight(collapseMargin);
        }
    }

    // Center the tab buttons in the remaining space
    if (currentTabs_.empty())
        return;

    auto numTabs = juce::jmin(static_cast<int>(currentTabs_.size()), MAX_TABS);
    int totalWidth = numTabs * BUTTON_SIZE + (numTabs - 1) * BUTTON_SPACING;
    int startX = availableBounds.getX() + (availableBounds.getWidth() - totalWidth) / 2;
    int buttonY = (getHeight() - BUTTON_SIZE) / 2;

    for (size_t i = 0; i < currentTabs_.size() && i < MAX_TABS; ++i) {
        if (tabButtons_[i]) {
            int buttonX = startX + static_cast<int>(i) * (BUTTON_SIZE + BUTTON_SPACING);
            tabButtons_[i]->setBounds(buttonX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
        }
    }
}

void PanelTabBar::setupCollapseButton() {
    // Bottom panel collapse is handled by FooterBar
    if (location_ == PanelLocation::Bottom)
        return;

    // Initial state is expanded, so show "close" icon
    const char* svgData = (location_ == PanelLocation::Right) ? BinaryData::left_close_svg
                                                              : BinaryData::right_close_svg;
    size_t svgSize = (location_ == PanelLocation::Right) ? BinaryData::left_close_svgSize
                                                         : BinaryData::right_close_svgSize;

    collapseButton_ = std::make_unique<magda::SvgButton>("Collapse", svgData, svgSize);
    collapseButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    collapseButton_->onClick = [this]() {
        if (onCollapseClicked)
            onCollapseClicked();
    };
    addAndMakeVisible(*collapseButton_);
}

void PanelTabBar::setCollapseState(bool collapsed) {
    collapsed_ = collapsed;
    updateCollapseIcon();
}

void PanelTabBar::updateCollapseIcon() {
    if (!collapseButton_)
        return;

    const char* svgData = nullptr;
    size_t svgSize = 0;

    if (location_ == PanelLocation::Right) {
        // LeftPanel: collapsed → "open", expanded → "close"
        svgData = collapsed_ ? BinaryData::left_open_svg : BinaryData::left_close_svg;
        svgSize = collapsed_ ? BinaryData::left_open_svgSize : BinaryData::left_close_svgSize;
    } else if (location_ == PanelLocation::Left) {
        // RightPanel: collapsed → "open", expanded → "close"
        svgData = collapsed_ ? BinaryData::right_open_svg : BinaryData::right_close_svg;
        svgSize = collapsed_ ? BinaryData::right_open_svgSize : BinaryData::right_close_svgSize;
    }

    if (svgData)
        collapseButton_->updateSvgData(svgData, svgSize);
}

void PanelTabBar::setTabs(const std::vector<PanelContentType>& tabs) {
    // Remove old buttons
    for (auto& btn : tabButtons_) {
        if (btn) {
            removeChildComponent(btn.get());
            btn.reset();
        }
    }

    currentTabs_ = tabs;

    // Create new buttons
    for (size_t i = 0; i < tabs.size() && i < MAX_TABS; ++i) {
        setupButton(i, tabs[i]);
    }

    updateButtonStates();
    resized();
}

void PanelTabBar::setActiveTab(int index) {
    if (index != activeTabIndex_ && index >= 0 && index < static_cast<int>(currentTabs_.size())) {
        activeTabIndex_ = index;
        updateButtonStates();
    }
}

void PanelTabBar::setupButton(size_t index, PanelContentType type) {
    auto svgData = getSvgForContentType(type);
    if (svgData.data == nullptr || svgData.size <= 0)
        return;

    auto name = getContentTypeName(type);

    tabButtons_[index] =
        std::make_unique<SvgButton>(name, svgData.data, static_cast<size_t>(svgData.size));

    auto* btn = tabButtons_[index].get();
    btn->setClickingTogglesState(false);

    // Set colors
    btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
    btn->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.35f));

    // Click handler
    btn->onClick = [this, index]() {
        if (onTabClicked) {
            onTabClicked(static_cast<int>(index));
        }
    };

    addAndMakeVisible(*btn);
}

void PanelTabBar::updateButtonStates() {
    for (size_t i = 0; i < currentTabs_.size() && i < MAX_TABS; ++i) {
        if (tabButtons_[i]) {
            tabButtons_[i]->setActive(static_cast<int>(i) == activeTabIndex_);
        }
    }
    repaint();
}

}  // namespace magda::daw::ui
