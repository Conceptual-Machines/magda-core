// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/common/PresetSelector.hpp"

using namespace magda::daw::ui;

// The preset box and floppy disk on devices and tracks (#3234).
class PresetSelectorTests : public juce::UnitTest {
  public:
    PresetSelectorTests() : juce::UnitTest("Preset selector", "magda") {}

    void runTest() override {
        constexpr int boxHeight = 20;
        constexpr int saveWidth = 24;
        PresetSelector selector(boxHeight, saveWidth);
        auto& box = selector.getBox();
        auto& save = selector.getSaveButton();

        beginTest("The box names the preset without its folders");
        {
            expectEquals(selector.getLabel(), juce::String("Presets"));
            selector.setPresetName("Bass/Deep/808");
            expectEquals(selector.getLabel(), juce::String("808"));
            selector.setPresetName("Init");
            expectEquals(selector.getLabel(), juce::String("Init"));
            selector.setPresetName({});
            expectEquals(selector.getLabel(), juce::String("Presets"));
        }

        beginTest("The box opens the menu and the floppy disk saves");
        {
            int opened = 0;
            int saved = 0;
            selector.onOpenMenu = [&opened] { ++opened; };
            selector.onSave = [&saved] { ++saved; };
            box.onClick();
            expectEquals(opened, 1);
            expectEquals(saved, 0);
            save.onClick();
            expectEquals(opened, 1);
            expectEquals(saved, 1);
        }

        beginTest("The box stays lit while its menu is open");
        {
            selector.setMenuOpen(true);
            expect(box.getToggleState());
            selector.setMenuOpen(false);
            expect(!box.getToggleState());
        }

        beginTest("The floppy disk keeps its size in a tight header");
        {
            for (const int width : {selector.getPreferredWidth(), 40}) {
                selector.setBounds(0, 0, width, 26);
                expectEquals(save.getWidth(), saveWidth);
                expectEquals(save.getRight(), width);
                expect(box.getRight() <= save.getX());
                expectEquals(box.getHeight(), boxHeight);
                expectEquals(box.getBounds().getCentreY(), selector.getBounds().getCentreY());
            }
        }

        beginTest("A click on neither control falls through to the header");
        {
            bool allowsClick = true;
            bool allowsChildren = false;
            selector.getInterceptsMouseClicks(allowsClick, allowsChildren);
            expect(!allowsClick && allowsChildren);
        }

        beginTest("The box paints at any width");
        {
            selector.setPresetName("A preset with a very long name");
            for (const int width : {selector.getPreferredWidth(), 40, saveWidth}) {
                selector.setBounds(0, 0, width, 26);
                juce::Image image(juce::Image::ARGB, width, 26, true);
                juce::Graphics g(image);
                selector.paintEntireComponent(g, false);
            }
        }
    }
};

static PresetSelectorTests presetSelectorTests;
