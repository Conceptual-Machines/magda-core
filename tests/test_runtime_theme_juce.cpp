#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/themes/DarkTheme.hpp"

class RuntimeThemeTest final : public juce::UnitTest {
  public:
    RuntimeThemeTest() : juce::UnitTest("Runtime Theme Tests", "magda") {}

    void runTest() override {
        beginTest("Dark palette is the default");
        DarkThemeReset reset;
        magda::DarkTheme::resetToDarkPalette();
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::BACKGROUND) == 0xFF0C0F14);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::TEXT_PRIMARY) == 0xFFE8EDF1);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::AUTOMATION_LANE_BACKGROUND) ==
               0xFF1E1E1E);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::PIANO_ROLL_GRID_BACKGROUND) ==
               0xFF3A3A3A);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::ICON_NEUTRAL) == 0xFFB3B3B3);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::ICON_TRANSPORT) == 0xFFBCBCBC);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::PRESET_INDIGO) == 0xFF5577CC);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::MIDI_LEARN) == 0xFFFF6B35);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::STEP_RECORD) == 0xFFCC3333);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::CURVE_BACKGROUND) == 0xFF1A1A1A);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::CURVE_POINT) == 0xFFFF8A2A);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::GATE_CURVE) == 0xFF00D4FF);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::MULTIBAND_LOW) == 0xFF43A0FF);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::SAMPLER_START_MARKER) ==
               0xFFFF9800);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::INSTRUMENT_BACKGROUND) ==
               0xFF0D0D0F);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::TEXT_SLIDER_THUMB) == 0xFFBCD4E8);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::TOAST_BACKGROUND) == 0xFF222233);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::EQ_BAND_LOW) == 0xFFE06C75);

        const auto neutralIconRole =
            magda::DarkTheme::findDarkPaletteRole(juce::Colour(0x80B3B3B3));
        expect(neutralIconRole.has_value());
        expect(*neutralIconRole == magda::ColourRole::ICON_NEUTRAL);

        beginTest("Active palette changes existing DarkTheme lookups at runtime");
        auto testPalette = magda::DarkTheme::getDarkPalette();
        testPalette[static_cast<std::size_t>(magda::ColourRole::BACKGROUND)] = 0xFFF2F2F2;
        testPalette[static_cast<std::size_t>(magda::ColourRole::TEXT_PRIMARY)] = 0xFF121212;
        magda::DarkTheme::setActivePalette(testPalette);

        expect(magda::DarkTheme::getBackgroundColour().getARGB() == 0xFFF2F2F2);
        expect(magda::DarkTheme::getTextColour().getARGB() == 0xFF121212);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_BLUE).getARGB() == 0xFF5588AA);

        beginTest("Built-in themes can switch at runtime");
        expect(magda::DarkTheme::isBuiltInTheme(magda::DarkTheme::kDarkThemeId));
        expect(magda::DarkTheme::isBuiltInTheme(magda::DarkTheme::kHighContrastThemeId));
        expect(magda::DarkTheme::setActiveBuiltInTheme(magda::DarkTheme::kHighContrastThemeId));
        expect(magda::DarkTheme::getBackgroundColour().getARGB() == 0xFF000000);
        expect(magda::DarkTheme::getTextColour().getARGB() == 0xFFFFFFFF);
        expect(
            magda::DarkTheme::getColour(magda::DarkTheme::AUTOMATION_LANE_BACKGROUND).getARGB() ==
            0xFF101010);
        expect(
            magda::DarkTheme::getColour(magda::DarkTheme::PIANO_ROLL_GRID_BACKGROUND).getARGB() ==
            0xFF202020);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ICON_NEUTRAL).getARGB() == 0xFFD0D0D0);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::PRESET_INDIGO).getARGB() ==
               0xFF88B7FF);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::MIDI_LEARN).getARGB() == 0xFFFF9A73);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::CURVE_POINT).getARGB() == 0xFFFFA24A);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::GATE_CURVE).getARGB() == 0xFF54DFFF);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::SAMPLER_END_MARKER).getARGB() ==
               0xFFFF7171);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::INSTRUMENT_TEXT).getARGB() ==
               0xFFF3F3F8);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::TEXT_SLIDER_THUMB).getARGB() ==
               0xFFE5F1FF);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::TOAST_BACKGROUND).getARGB() ==
               0xFF181818);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::EQ_BAND_LOW).getARGB() == 0xFFFF8088);
        expect(!magda::DarkTheme::setActiveBuiltInTheme("missing-theme"));
        expect(magda::DarkTheme::getTextColour().getARGB() == 0xFFFFFFFF);

        beginTest("Bundled SVG source colours resolve through the active palette");
        static constexpr char kNeutralIconSvg[] =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect width="10" height="10" fill="#B3B3B3"/></svg>)svg";
        auto icon = juce::Drawable::createFromImageData(kNeutralIconSvg, sizeof(kNeutralIconSvg));
        expect(icon != nullptr);
        if (icon) {
            magda::DarkTheme::applyToSvgIcon(*icon);
            juce::Image image(juce::Image::ARGB, 10, 10, true);
            juce::Graphics graphics(image);
            icon->drawWithin(graphics, {0.0f, 0.0f, 10.0f, 10.0f},
                             juce::RectanglePlacement::stretchToFit, 1.0f);
            expect(image.getPixelAt(5, 5).getARGB() == 0xFFD0D0D0);
        }

        beginTest("Reset restores the built-in dark palette");
        magda::DarkTheme::resetToDarkPalette();
        expect(magda::DarkTheme::getActivePalette() == magda::DarkTheme::getDarkPalette());
    }

  private:
    struct DarkThemeReset {
        ~DarkThemeReset() {
            magda::DarkTheme::resetToDarkPalette();
        }
    };
};

static RuntimeThemeTest runtimeThemeTest;
