// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/core/DefaultColourPalette.hpp"
#include "magda/daw/ui/themes/ActiveTheme.hpp"

/** @brief Light track colours show as picked under dark ink; the rest stay levelled (#3307). */
class TrackSwatchTest final : public juce::UnitTest {
  public:
    TrackSwatchTest() : juce::UnitTest("Track Swatch Tests", "magda") {}

    void runTest() override {
        ThemeReset reset;
        for (const auto* themeId :
             {magda::ThemeManager::kDarkThemeId, magda::ThemeManager::kLightThemeId}) {
            magda::ThemeManager::setActiveBuiltInTheme(themeId);

            beginTest(juce::String("Default colours stay levelled, under white ink: ") + themeId);
            for (const auto& entry : magda::kDefaultColourPalette) {
                const juce::Colour stored(entry.colour);
                expect(!magda::isLightTrackColour(stored), entry.name);
                expect(magda::deriveTrackSwatch(stored) == magda::deriveLevelledSwatch(stored),
                       entry.name);
                expect(magda::deriveSwatchInk(stored) == juce::Colours::white, entry.name);
            }

            beginTest(juce::String("A bright yellow shows as picked, under dark ink: ") + themeId);
            const juce::Colour yellow(0xFFFFEF08);
            expect(magda::isLightTrackColour(yellow));
            expect(magda::deriveTrackSwatch(yellow) == yellow);
            expect(magda::deriveTrackSwatch(yellow, 0.5f) == yellow.withAlpha(0.5f));
            expect(magda::deriveSwatchInk(yellow).getPerceivedBrightness() < 0.2f);

            beginTest(juce::String("The no-colour sentinel is unchanged: ") + themeId);
            const juce::Colour none(0xFF444444);
            expect(!magda::isLightTrackColour(none));
            expect(magda::deriveTrackSwatch(none) == none);
            expect(magda::deriveSwatchInk(none) == juce::Colours::white);
        }
    }

  private:
    struct ThemeReset {
        ~ThemeReset() {
            magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kDarkThemeId);
        }
    };
};

static TrackSwatchTest trackSwatchTest;
