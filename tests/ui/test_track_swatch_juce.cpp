// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/core/DefaultColourPalette.hpp"
#include "magda/daw/ui/themes/ActiveTheme.hpp"

/** @brief A full-bar head holds a tint behind the name, then fades into the colour as picked. */
class TrackSwatchTest final : public juce::UnitTest {
  public:
    TrackSwatchTest() : juce::UnitTest("Track Swatch Tests", "magda") {}

    void runTest() override {
        ThemeReset reset;
        const juce::Rectangle<float> head(10.0f, 0.0f, 200.0f, 24.0f);
        const juce::Colour brightYellow(0xFFFFEF08);
        const juce::Colour navy(0xFF000080);

        for (const auto* themeId :
             {magda::ThemeManager::kDarkThemeId, magda::ThemeManager::kLightThemeId}) {
            magda::ThemeManager::setActiveBuiltInTheme(themeId);
            const juce::String theme(themeId);

            beginTest("The tint holds behind the name, then fades into the colour: " + theme);
            for (const auto stored : {brightYellow, navy, juce::Colour(0xFFAAAA55)}) {
                const auto gradient = magda::deriveTrackHeadGradient(stored, 0.0f, head);
                const auto tint = magda::deriveHeadTint(stored);
                expect(gradient.getColourAtPosition(0.0) == tint);
                expect(gradient.getColourAtPosition(magda::kHeadTintSpan) == tint);
                expect(gradient.getColourAtPosition(1.0) == stored);
            }

            beginTest("A shade darkens the colour end only: " + theme);
            const auto shaded = magda::deriveTrackHeadGradient(brightYellow, 0.2f, head);
            expect(shaded.getColourAtPosition(0.0) == magda::deriveHeadTint(brightYellow));
            expect(shaded.getColourAtPosition(1.0) == brightYellow.darker(0.2f));
        }

        beginTest("On the dark theme, names are white on the clip-body tint");
        magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kDarkThemeId);
        expect(magda::trackHeadNameColour() == juce::Colours::white);
        for (const auto& entry : magda::kDefaultColourPalette) {
            const juce::Colour stored(entry.colour);
            expect(magda::deriveHeadTint(stored) == magda::deriveClipTint(stored), entry.name);
        }

        beginTest("A colour darker than the tint gets a darker tint, so the fade runs toward it");
        expect(magda::sRgbToOklab(magda::deriveHeadTint(navy)).L < magda::sRgbToOklab(navy).L);

        beginTest("On the light theme, names take the theme's text on a pale tint");
        magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kLightThemeId);
        expect(magda::trackHeadNameColour() ==
               magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
        expect(magda::deriveHeadTint(brightYellow) == magda::deriveClipTint(brightYellow));
    }

  private:
    struct ThemeReset {
        ~ThemeReset() {
            magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kDarkThemeId);
        }
    };
};

static TrackSwatchTest trackSwatchTest;
