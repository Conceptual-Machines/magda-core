// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Config.hpp"

namespace magda {

/**
 * @brief Footer line naming the control under the mouse, with its help text and shortcut (#2055)
 *
 * Reads the hovered component's tooltip, walking up to the nearest parent that has one. A
 * control tagged with setCommand() also shows the command's name and its current key binding.
 */
class InfoLine : public juce::Component, private ConfigListener, private juce::Timer {
  public:
    explicit InfoLine(juce::ApplicationCommandManager* commands);
    ~InfoLine() override;

    /** @brief Tags a control with the command it triggers, so the line can show its shortcut. */
    static void setCommand(juce::Component& component, juce::CommandID commandId);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    struct HoverWatcher : juce::MouseListener {
        explicit HoverWatcher(InfoLine& owner) : owner_(owner) {}
        void mouseMove(const juce::MouseEvent&) override;
        void mouseEnter(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        InfoLine& owner_;
    };

    void configChanged() override;
    void timerCallback() override;
    void applyEnabled();
    void showHelpFor(juce::Component* component);
    int contentWidth() const;
    void restartScroll();

    juce::ApplicationCommandManager* commands_;
    HoverWatcher watcher_{*this};
    bool enabled_ = false;
    juce::Component::SafePointer<juce::Component> hovered_;
    juce::String name_, description_, shortcut_;
    float scrollOffset_ = 0.0f;
    int holdTicks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InfoLine)
};

}  // namespace magda
