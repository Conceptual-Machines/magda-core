// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "InfoLine.hpp"

#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magda {

namespace {
const juce::Identifier kCommandProperty{"magdaCommandId"};
constexpr int kTickHz = 30;
constexpr int kHoldTicks = kTickHz;  // a second at each end before scrolling on
constexpr float kScrollPxPerTick = 1.0f;
constexpr int kPartGap = 10;

juce::Font nameFont() {
    return FontManager::getInstance().getUIFontMedium(11.5f);
}
juce::Font descriptionFont() {
    return FontManager::getInstance().getUIFont(11.5f);
}
juce::Font keyFont() {
    return FontManager::getInstance().getMonoFont(10.5f);
}
int widthOf(const juce::Font& font, const juce::String& text) {
    return juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, text));
}
}  // namespace

InfoLine::InfoLine(juce::ApplicationCommandManager* commands) : commands_(commands) {
    setInterceptsMouseClicks(false, false);
    Config::getInstance().addListener(this);
    applyEnabled();
}

InfoLine::~InfoLine() {
    stopTimer();
    Config::getInstance().removeListener(this);
    if (enabled_)
        juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_);
}

void InfoLine::setCommand(juce::Component& component, juce::CommandID commandId) {
    component.getProperties().set(kCommandProperty, commandId);
}

void InfoLine::configChanged() {
    applyEnabled();
}

void InfoLine::applyEnabled() {
    const bool enabled = Config::getInstance().getShowInfoLine();
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    auto& desktop = juce::Desktop::getInstance();
    if (enabled_) {
        desktop.addGlobalMouseListener(&watcher_);
    } else {
        desktop.removeGlobalMouseListener(&watcher_);
        showHelpFor(nullptr);
    }
    setVisible(enabled_);
}

void InfoLine::HoverWatcher::mouseMove(const juce::MouseEvent&) {
    owner_.showHelpFor(juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse());
}

void InfoLine::HoverWatcher::mouseEnter(const juce::MouseEvent& e) {
    mouseMove(e);
}

void InfoLine::HoverWatcher::mouseExit(const juce::MouseEvent& e) {
    mouseMove(e);
}

void InfoLine::showHelpFor(juce::Component* component) {
    // The floating tooltip lands under the mouse when it pops up; it is not a new hover.
    if (dynamic_cast<juce::TooltipWindow*>(component) != nullptr)
        return;
    if (component == hovered_.getComponent())
        return;
    hovered_ = component;

    // The first component carrying a tooltip or a command speaks for everything inside it.
    juce::String tooltip;
    juce::CommandID commandId = 0;
    for (auto* c = component; c != nullptr; c = c->getParentComponent()) {
        if (auto* client = dynamic_cast<juce::TooltipClient*>(c))
            tooltip = client->getTooltip();
        if (const auto* id = c->getProperties().getVarPointer(kCommandProperty))
            commandId = static_cast<int>(*id);
        if (tooltip.isNotEmpty() || commandId != 0)
            break;
    }

    juce::String name, description, shortcut;
    if (commandId != 0 && commands_ != nullptr) {
        name = commands_->getNameOfCommand(commandId);
        description =
            tooltip.isNotEmpty() ? tooltip : commands_->getDescriptionOfCommand(commandId);
        if (description == name)
            description.clear();
        const auto keys = commands_->getKeyMappings()->getKeyPressesAssignedToCommand(commandId);
        if (!keys.isEmpty())
            shortcut = keys.getReference(0).getTextDescriptionWithIcons();
    } else {
        description = tooltip;
    }

    if (name == name_ && description == description_ && shortcut == shortcut_)
        return;
    name_ = name;
    description_ = description.replaceCharacters("\r\n", "  ");
    shortcut_ = shortcut;
    restartScroll();
    repaint();
}

int InfoLine::contentWidth() const {
    int width = 0;
    auto add = [&](int partWidth) { width += partWidth + (width > 0 ? kPartGap : 0); };
    if (name_.isNotEmpty())
        add(widthOf(nameFont(), name_));
    if (description_.isNotEmpty())
        add(widthOf(descriptionFont(), description_));
    if (shortcut_.isNotEmpty())
        add(widthOf(keyFont(), shortcut_) + 8);
    return width;
}

void InfoLine::restartScroll() {
    scrollOffset_ = 0.0f;
    holdTicks_ = kHoldTicks;
    if (contentWidth() > getWidth())
        startTimerHz(kTickHz);
    else
        stopTimer();
}

void InfoLine::resized() {
    restartScroll();
}

void InfoLine::timerCallback() {
    if (holdTicks_ > 0) {
        --holdTicks_;
        return;
    }
    const float overflow = static_cast<float>(contentWidth() - getWidth());
    if (scrollOffset_ >= overflow) {
        scrollOffset_ = 0.0f;
        holdTicks_ = kHoldTicks;
    } else {
        scrollOffset_ = juce::jmin(overflow, scrollOffset_ + kScrollPxPerTick);
        if (scrollOffset_ >= overflow)
            holdTicks_ = kHoldTicks;
    }
    repaint();
}

void InfoLine::paint(juce::Graphics& g) {
    if (name_.isEmpty() && description_.isEmpty())
        return;

    g.reduceClipRegion(getLocalBounds());
    juce::Rectangle<int> row(-juce::roundToInt(scrollOffset_), 0, contentWidth() + kPartGap,
                             getHeight());
    auto drawPart = [&](const juce::String& text, const juce::Font& font, juce::Colour colour) {
        const int w = widthOf(font, text);
        g.setFont(font);
        g.setColour(colour);
        g.drawText(text, row.removeFromLeft(w + 2), juce::Justification::centredLeft, false);
        row.removeFromLeft(kPartGap - 2);
    };

    if (name_.isNotEmpty())
        drawPart(name_, nameFont(), ActiveTheme::getTextColour());
    if (description_.isNotEmpty())
        drawPart(description_, descriptionFont(), ActiveTheme::getSecondaryTextColour());
    if (shortcut_.isNotEmpty()) {
        const auto font = keyFont();
        const int keyW = widthOf(font, shortcut_) + 8;
        const auto keyBox = row.removeFromLeft(keyW).withSizeKeepingCentre(keyW, 15).toFloat();
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(keyBox.reduced(0.5f), 3.0f, 1.0f);
        g.setFont(font);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText(shortcut_, keyBox, juce::Justification::centred, false);
    }
}

}  // namespace magda
