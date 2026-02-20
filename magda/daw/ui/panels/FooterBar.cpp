#include "FooterBar.hpp"

#include <BinaryData.h>

#include "../themes/DarkTheme.hpp"

namespace magda {

FooterBar::FooterBar() {
    setupButtons();
    setupBottomCollapseButton();
    ViewModeController::getInstance().addListener(this);
    updateButtonStates();
}

FooterBar::~FooterBar() {
    ViewModeController::getInstance().removeListener(this);
    // RAII cleanup handled automatically by ManagedChild
}

void FooterBar::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(0.0f, 0.0f, static_cast<float>(getWidth()), 0.0f, 1.0f);
}

void FooterBar::resized() {
    auto bounds = getLocalBounds();

    // Center the view mode buttons horizontally
    int totalButtonsWidth = NUM_MODES * BUTTON_SIZE + (NUM_MODES - 1) * BUTTON_SPACING;
    int startX = (bounds.getWidth() - totalButtonsWidth) / 2;

    int buttonY = (bounds.getHeight() - BUTTON_SIZE) / 2;

    for (int i = 0; i < NUM_MODES; ++i) {
        int buttonX = startX + i * (BUTTON_SIZE + BUTTON_SPACING);
        modeButtons[static_cast<size_t>(i)]->setBounds(buttonX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
    }

    // Bottom panel collapse button on the right side
    if (bottomCollapseButton_) {
        constexpr int collapseSize = 20;
        int cy = (bounds.getHeight() - collapseSize) / 2;
        bottomCollapseButton_->setBounds(bounds.getWidth() - collapseSize - 8, cy, collapseSize,
                                         collapseSize);
    }
}

void FooterBar::viewModeChanged(ViewMode /*mode*/, const AudioEngineProfile& /*profile*/) {
    updateButtonStates();
}

void FooterBar::setupButtons() {
    // Icon data for each mode: Session=Live, Arrangement=Arrange, Mix, Master
    struct IconData {
        const char* data;
        int size;
        ViewMode mode;
        const char* name;
    };

    const std::array<IconData, NUM_MODES> icons = {{
        {BinaryData::Session_svg, BinaryData::Session_svgSize, ViewMode::Live, "Live"},
        {BinaryData::Arrangement_svg, BinaryData::Arrangement_svgSize, ViewMode::Arrange,
         "Arrange"},
        {BinaryData::Mix_svg, BinaryData::Mix_svgSize, ViewMode::Mix, "Mix"},
        {BinaryData::Master_svg, BinaryData::Master_svgSize, ViewMode::Master, "Master"},
    }};

    for (size_t i = 0; i < NUM_MODES; ++i) {
        // Create button using RAII wrapper
        modeButtons[i] = magda::ManagedChild<SvgButton>::create(icons[i].name, icons[i].data,
                                                                static_cast<size_t>(icons[i].size));

        modeButtons[i]->setClickingTogglesState(false);
        modeButtons[i]->onClick = [mode = icons[i].mode]() {
            ViewModeController::getInstance().setViewMode(mode);
        };

        // Set colors for the SvgButton
        modeButtons[i]->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        modeButtons[i]->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        modeButtons[i]->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

        addAndMakeVisible(*modeButtons[i]);  // Safe with ManagedChild
    }
}

void FooterBar::updateButtonStates() {
    auto currentMode = ViewModeController::getInstance().getViewMode();

    const std::array<ViewMode, NUM_MODES> modes = {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix,
                                                   ViewMode::Master};

    for (size_t i = 0; i < NUM_MODES; ++i) {
        bool isActive = (modes[i] == currentMode);
        modeButtons[i]->setActive(isActive);
    }

    repaint();
}

void FooterBar::setupBottomCollapseButton() {
    bottomCollapseButton_ = std::make_unique<SvgButton>(
        "BottomCollapse", BinaryData::collapse_down_svg, BinaryData::collapse_down_svgSize);
    bottomCollapseButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    bottomCollapseButton_->onClick = [this]() {
        if (onBottomPanelCollapseToggle)
            onBottomPanelCollapseToggle();
    };
    addAndMakeVisible(*bottomCollapseButton_);
}

void FooterBar::setBottomPanelCollapsed(bool collapsed) {
    bottomCollapsed_ = collapsed;
    updateBottomCollapseIcon();
}

void FooterBar::updateBottomCollapseIcon() {
    if (!bottomCollapseButton_)
        return;

    if (bottomCollapsed_) {
        bottomCollapseButton_->updateSvgData(BinaryData::collapse_up_svg,
                                             BinaryData::collapse_up_svgSize);
    } else {
        bottomCollapseButton_->updateSvgData(BinaryData::collapse_down_svg,
                                             BinaryData::collapse_down_svgSize);
    }
}

}  // namespace magda
