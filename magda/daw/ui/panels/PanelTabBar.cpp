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
            // Use plug icon as placeholder for chain (could add dedicated icon later)
            return {BinaryData::plug_svg, BinaryData::plug_svgSize};
        case PanelContentType::PianoRoll:
            // Use script icon as placeholder for piano roll (could add dedicated icon later)
            return {BinaryData::script_svg, BinaryData::script_svgSize};
        case PanelContentType::WaveformEditor:
            return {BinaryData::sinewave_svg, BinaryData::sinewave_svgSize};
        case PanelContentType::Empty:
            break;
    }
    return {};
}
}  // namespace

PanelTabBar::PanelTabBar() {
    setName("Panel Tab Bar");
}

void PanelTabBar::paint(juce::Graphics& g) {
    // Draw background
    g.fillAll(DarkTheme::getPanelBackgroundColour().darker(0.1f));

    // Draw top border
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, 0, getWidth(), 1);
}

void PanelTabBar::resized() {
    if (currentTabs_.empty())
        return;

    auto numTabs = static_cast<int>(currentTabs_.size());
    int totalWidth = numTabs * BUTTON_SIZE + (numTabs - 1) * BUTTON_SPACING;
    int startX = (getWidth() - totalWidth) / 2;
    int buttonY = (getHeight() - BUTTON_SIZE) / 2;

    for (size_t i = 0; i < currentTabs_.size(); ++i) {
        if (tabButtons_[i]) {
            int buttonX = startX + static_cast<int>(i) * (BUTTON_SIZE + BUTTON_SPACING);
            tabButtons_[i]->setBounds(buttonX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
        }
    }
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
    btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

    // Click handler
    btn->onClick = [this, index]() {
        if (onTabClicked) {
            onTabClicked(static_cast<int>(index));
        }
    };

    addAndMakeVisible(*btn);
}

void PanelTabBar::updateButtonStates() {
    for (size_t i = 0; i < currentTabs_.size(); ++i) {
        if (tabButtons_[i]) {
            tabButtons_[i]->setActive(static_cast<int>(i) == activeTabIndex_);
        }
    }
    repaint();
}

}  // namespace magda::daw::ui
