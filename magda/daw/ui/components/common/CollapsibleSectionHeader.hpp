// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief An inspector section title with a caret that folds the section.
 *
 * Collapse state is kept per @p stateKey for the session, so a section stays folded across
 * selection changes and across panels that share the key. An optional caption and accessory
 * (a switch, say) sit at the right end; clicks on the accessory do not fold.
 */
class CollapsibleSectionHeader : public juce::Component {
  public:
    static constexpr int kHeight = 26;

    CollapsibleSectionHeader(const juce::String& text, juce::String stateKey)
        : text_(text.toUpperCase()), stateKey_(std::move(stateKey)) {}

    void setText(const juce::String& text) {
        text_ = text.toUpperCase();
        repaint();
    }

    void setCaption(const juce::String& caption) {
        caption_ = caption;
        repaint();
    }

    void setAccessory(juce::Component* accessory, int width) {
        accessory_ = accessory;
        accessoryWidth_ = width;
        if (accessory_ != nullptr)
            addAndMakeVisible(*accessory_);
        resized();
    }

    bool isCollapsed() const {
        return collapsedStates()[stateKey_];
    }

    /// Fold or unfold from code; onCollapsedChanged is left to the caller.
    void setCollapsed(bool collapsed) {
        collapsedStates()[stateKey_] = collapsed;
        repaint();
    }

    std::function<void()> onCollapsedChanged;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        const float cy = static_cast<float>(bounds.getCentreY());

        juce::Path caret;
        if (isCollapsed())
            caret.addTriangle(1.0f, cy - 4.0f, 1.0f, cy + 4.0f, 6.0f, cy);
        else
            caret.addTriangle(0.0f, cy - 2.5f, 8.0f, cy - 2.5f, 4.0f, cy + 2.5f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.fillPath(caret);
        bounds.removeFromLeft(14);

        const auto font =
            FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
        g.setFont(font);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.drawText(text_, bounds, juce::Justification::centredLeft, false);

        if (caption_.isNotEmpty()) {
            if (accessory_ != nullptr)
                bounds.removeFromRight(accessoryWidth_ + 8);
            g.setFont(FontManager::getInstance().getMonoFont(10.0f));
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            g.drawText(caption_, bounds, juce::Justification::centredRight, false);
        }
    }

    void resized() override {
        if (accessory_ != nullptr)
            accessory_->setBounds(getLocalBounds().removeFromRight(accessoryWidth_));
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!getLocalBounds().contains(e.getPosition()))
            return;
        auto& collapsed = collapsedStates()[stateKey_];
        collapsed = !collapsed;
        repaint();
        if (onCollapsedChanged)
            onCollapsedChanged();
    }

  private:
    static std::map<juce::String, bool>& collapsedStates() {
        static std::map<juce::String, bool> states;
        return states;
    }

    juce::String text_;
    juce::String stateKey_;
    juce::String caption_;
    juce::Component* accessory_ = nullptr;
    int accessoryWidth_ = 0;
};

}  // namespace magda::daw::ui
