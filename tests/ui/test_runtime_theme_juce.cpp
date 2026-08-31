#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/common/SvgButton.hpp"
#include "magda/daw/ui/themes/DarkTheme.hpp"
#include "magda/daw/ui/themes/UserTheme.hpp"

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
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::ICON_ON_ACCENT) == 0xFF1E1E1E);
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
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::ICON_BACKGROUND) == 0xFF2E2E33);
        expect(magda::DarkTheme::getColourValue(magda::DarkTheme::ICON_POWER) == 0xFFE6E6E6);

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
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_PRIMARY).getARGB() ==
               0xFF5588AA);

        beginTest("Built-in themes can switch at runtime");
        expect(magda::ThemeManager::isBuiltInTheme(magda::ThemeManager::kDarkThemeId));
        expect(magda::ThemeManager::isBuiltInTheme(magda::ThemeManager::kLightThemeId));
        expect(magda::ThemeManager::isBuiltInTheme(magda::ThemeManager::kHighContrastThemeId));

        expect(magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kLightThemeId));
        expect(magda::ThemeManager::isLightTheme());
        expect(magda::DarkTheme::getBackgroundColour().getARGB() == 0xFFF4F6F8);
        expect(magda::DarkTheme::getTextColour().getARGB() == 0xFF1B242C);
        expect(
            magda::DarkTheme::getColour(magda::DarkTheme::AUTOMATION_LANE_BACKGROUND).getARGB() ==
            0xFFF0F2F4);
        expect(
            magda::DarkTheme::getColour(magda::DarkTheme::PIANO_ROLL_GRID_BACKGROUND).getARGB() ==
            0xFFE7EAED);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ICON_NEUTRAL).getARGB() == 0xFF46535E);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::WAVEFORM_NORMAL).getARGB() ==
               0xFF087A43);
        expect(magda::DarkTheme::getSyntaxColour(magda::SyntaxColourRole::EDITOR_BACKGROUND)
                   .getARGB() == 0xFFF7F8FA);
        expect(magda::DarkTheme::getSyntaxColour(magda::SyntaxColourRole::DSL_TOKEN_COMMENT)
                   .getARGB() == 0xFF4F762F);

        juce::LookAndFeel_V4 lightLookAndFeel;
        magda::DarkTheme::applyToLookAndFeel(lightLookAndFeel);
        expect(lightLookAndFeel.findColour(juce::TextButton::textColourOnId).getARGB() ==
               0xFFFFFFFF);

        const auto storedTrackColour = juce::Colour(0xFFFF0000);
        const auto lightSwatch = magda::deriveTrackSwatch(storedTrackColour);
        expect(storedTrackColour.getARGB() == 0xFFFF0000);
        expect(lightSwatch.getARGB() != storedTrackColour.getARGB());

        expect(
            magda::ThemeManager::setActiveBuiltInTheme(magda::ThemeManager::kHighContrastThemeId));
        expect(!magda::ThemeManager::isLightTheme());
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
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ICON_BACKGROUND).getARGB() ==
               0xFF1B1B1B);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ICON_POWER).getARGB() == 0xFFE0E0E0);
        expect(magda::DarkTheme::getColour(magda::DarkTheme::ICON_ON_ACCENT).getARGB() ==
               0xFF101010);
        expect(!magda::ThemeManager::setActiveBuiltInTheme("missing-theme"));
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

        beginTest("Transport SVG source keys resolve through code-side roles");
        static constexpr char kTransportIconSvg[] = R"svg(
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 1">
              <rect x="0" width="1" height="1" fill="#1A1A1A"/>
              <rect x="1" width="1" height="1" fill="#444444"/>
              <rect x="2" width="1" height="1" fill="#BCBCBC"/>
              <rect x="3" width="1" height="1" fill="#5588AA"/>
              <rect x="4" width="1" height="1" fill="#AA4444"/>
              <rect x="5" width="1" height="1" fill="#FFFFFF"/>
              <rect x="6" width="1" height="1" fill="#2E2E33"/>
              <rect x="7" width="1" height="1" fill="#000000"/>
            </svg>)svg";
        auto transportIcon =
            juce::Drawable::createFromImageData(kTransportIconSvg, sizeof(kTransportIconSvg));
        expect(transportIcon != nullptr);
        if (transportIcon) {
            magda::DarkTheme::applyToSvgIcon(*transportIcon);
            juce::Image image(juce::Image::ARGB, 8, 1, true);
            juce::Graphics graphics(image);
            transportIcon->drawWithin(graphics, {0.0f, 0.0f, 8.0f, 1.0f},
                                      juce::RectanglePlacement::stretchToFit, 1.0f);
            expect(image.getPixelAt(0, 0).getARGB() == 0xFF080808);
            expect(image.getPixelAt(1, 0).getARGB() == 0xFF909090);
            expect(image.getPixelAt(2, 0).getARGB() == 0xFFD8D8D8);
            expect(image.getPixelAt(3, 0).getARGB() == 0xFF4DA3FF);
            expect(image.getPixelAt(4, 0).getARGB() == 0xFFFF6B6B);
            expect(image.getPixelAt(5, 0).getARGB() == 0xFFFFFFFF);
            expect(image.getPixelAt(6, 0).getARGB() == 0xFF1B1B1B);
            expect(image.getPixelAt(7, 0).getARGB() == 0xFFD0D0D0);
        }

        beginTest("Remaining bundled SVG source keys resolve through code-side roles");
        static constexpr char kSharedIconSvg[] = R"svg(
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 14 1">
              <rect x="0" width="1" height="1" fill="#66AAFF"/>
              <rect x="1" width="1" height="1" fill="#88AACC"/>
              <rect x="2" width="1" height="1" fill="#E3E3E3"/>
              <rect x="3" width="1" height="1" fill="#E6E6E6"/>
              <rect x="4" width="1" height="1" fill="#808080"/>
              <rect x="5" width="1" height="1" fill="#979797"/>
              <rect x="6" width="1" height="1" fill="#D9D9D9"/>
              <rect x="7" width="1" height="1" fill="#2A3A4A"/>
              <rect x="8" width="1" height="1" fill="#2A2A35"/>
              <rect x="9" width="1" height="1" fill="#3A3A45"/>
              <rect x="10" width="1" height="1" fill="#1E1E22"/>
              <rect x="11" width="1" height="1" fill="#404050"/>
              <rect x="12" width="1" height="1" fill="#1E1E1E"/>
              <rect x="13" width="1" height="1" fill="#555555"/>
            </svg>)svg";
        auto sharedIcon =
            juce::Drawable::createFromImageData(kSharedIconSvg, sizeof(kSharedIconSvg));
        expect(sharedIcon != nullptr);
        if (sharedIcon) {
            magda::DarkTheme::applyToSvgIcon(*sharedIcon);
            juce::Image image(juce::Image::ARGB, 14, 1, true);
            juce::Graphics graphics(image);
            sharedIcon->drawWithin(graphics, {0.0f, 0.0f, 14.0f, 1.0f},
                                   juce::RectanglePlacement::stretchToFit, 1.0f);
            expect(image.getPixelAt(0, 0).getARGB() == 0xFF54C7FF);
            expect(image.getPixelAt(1, 0).getARGB() == 0xFF9DCCFF);
            expect(image.getPixelAt(2, 0).getARGB() == 0xFFF0F0F0);
            expect(image.getPixelAt(3, 0).getARGB() == 0xFFE0E0E0);
            expect(image.getPixelAt(4, 0).getARGB() == 0xFFB8B8B8);
            expect(image.getPixelAt(5, 0).getARGB() == 0xFFB8B8B8);
            expect(image.getPixelAt(6, 0).getARGB() == 0xFFF0F0F0);
            expect(image.getPixelAt(7, 0).getARGB() == 0xFF2E4C66);
            expect(image.getPixelAt(8, 0).getARGB() == 0xFF1B1B1B);
            expect(image.getPixelAt(9, 0).getARGB() == 0xFF707070);
            expect(image.getPixelAt(10, 0).getARGB() == 0xFF101010);
            expect(image.getPixelAt(11, 0).getARGB() == 0xFF808080);
            expect(image.getPixelAt(12, 0).getARGB() == 0xFF101010);
            expect(image.getPixelAt(13, 0).getARGB() == 0xFF909090);
        }

        beginTest("One SVG geometry renders inactive and active button states from code");
        static constexpr char kStatefulButtonSvg[] = R"svg(
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3 1">
              <rect x="0" width="1" height="1" fill="#1A1A1A"/>
              <rect x="1" width="1" height="1" fill="#BCBCBC"/>
              <rect x="2" width="1" height="1" fill="#444444"/>
            </svg>)svg";
        magda::SvgButton statefulButton("stateful", kStatefulButtonSvg, sizeof(kStatefulButtonSvg));
        statefulButton.setBounds(0, 0, 3, 1);
        statefulButton.setIconPadding(0.0f);
        statefulButton.setStateColourReplacement(juce::Colour(0xFF1A1A1A),
                                                 magda::DarkTheme::PIANO_ROLL_BACKGROUND,
                                                 magda::DarkTheme::ACCENT_PRIMARY);
        statefulButton.setStateColourReplacement(juce::Colour(0xFFBCBCBC),
                                                 magda::DarkTheme::ICON_TRANSPORT,
                                                 magda::DarkTheme::TEXT_BRIGHT);

        juce::Image inactiveImage(juce::Image::ARGB, 3, 1, true);
        juce::Graphics inactiveGraphics(inactiveImage);
        statefulButton.paintButton(inactiveGraphics, false, false);
        expect(inactiveImage.getPixelAt(0, 0).getARGB() == 0xFF080808);
        expect(inactiveImage.getPixelAt(1, 0).getARGB() == 0xFFD8D8D8);
        expect(inactiveImage.getPixelAt(2, 0).getARGB() == 0xFF909090);

        statefulButton.setActive(true);
        juce::Image activeImage(juce::Image::ARGB, 3, 1, true);
        juce::Graphics activeGraphics(activeImage);
        statefulButton.paintButton(activeGraphics, false, false);
        expect(activeImage.getPixelAt(0, 0).getARGB() == 0xFF4DA3FF);
        expect(activeImage.getPixelAt(1, 0).getARGB() == 0xFFFFFFFF);
        expect(activeImage.getPixelAt(2, 0).getARGB() == 0xFF909090);

        // The button keeps semantic roles rather than construction-time
        // colours, so a future light palette can independently invert the
        // SVG background and glyph without replacing the asset.
        auto lightLikePalette = magda::DarkTheme::getActivePalette();
        lightLikePalette[static_cast<std::size_t>(magda::DarkTheme::PIANO_ROLL_BACKGROUND)] =
            0xFFF2F2F2;
        lightLikePalette[static_cast<std::size_t>(magda::DarkTheme::ICON_TRANSPORT)] = 0xFF303030;
        lightLikePalette[static_cast<std::size_t>(magda::DarkTheme::ICON_ON_ACCENT)] = 0xFF202020;
        lightLikePalette[static_cast<std::size_t>(magda::DarkTheme::ACCENT_PRIMARY)] = 0xFF1769CC;
        lightLikePalette[static_cast<std::size_t>(magda::DarkTheme::TEXT_BRIGHT)] = 0xFF101010;
        magda::DarkTheme::setActivePalette(lightLikePalette);

        statefulButton.setActive(false);
        juce::Image lightInactiveImage(juce::Image::ARGB, 3, 1, true);
        juce::Graphics lightInactiveGraphics(lightInactiveImage);
        statefulButton.paintButton(lightInactiveGraphics, false, false);
        expect(lightInactiveImage.getPixelAt(0, 0).getARGB() == 0xFFF2F2F2);
        expect(lightInactiveImage.getPixelAt(1, 0).getARGB() == 0xFF303030);

        statefulButton.setActive(true);
        juce::Image lightActiveImage(juce::Image::ARGB, 3, 1, true);
        juce::Graphics lightActiveGraphics(lightActiveImage);
        statefulButton.paintButton(lightActiveGraphics, false, false);
        expect(lightActiveImage.getPixelAt(0, 0).getARGB() == 0xFF1769CC);
        expect(lightActiveImage.getPixelAt(1, 0).getARGB() == 0xFF101010);

        beginTest("Dual SVG geometry also receives state colours from code");
        static constexpr char kDualOffSvg[] = R"svg(
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1">
              <rect width="1" height="1" fill="#B3B3B3"/>
            </svg>)svg";
        static constexpr char kDualOnSvg[] = R"svg(
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1">
              <rect width="1" height="1" fill="#1E1E1E"/>
            </svg>)svg";
        magda::SvgButton dualButton("dual", kDualOffSvg, sizeof(kDualOffSvg), kDualOnSvg,
                                    sizeof(kDualOnSvg));
        dualButton.setBounds(0, 0, 1, 1);
        dualButton.setStateColourReplacement(juce::Colour(0xFFB3B3B3),
                                             magda::DarkTheme::ICON_NEUTRAL,
                                             magda::DarkTheme::ICON_ON_ACCENT);
        dualButton.setStateColourReplacement(juce::Colour(0xFF1E1E1E),
                                             magda::DarkTheme::ICON_NEUTRAL,
                                             magda::DarkTheme::ICON_ON_ACCENT);
        dualButton.setActive(true);
        juce::Image dualActiveImage(juce::Image::ARGB, 1, 1, true);
        juce::Graphics dualActiveGraphics(dualActiveImage);
        dualButton.paintButton(dualActiveGraphics, false, false);
        expect(dualActiveImage.getPixelAt(0, 0).getARGB() == 0xFF202020);

        beginTest("Factory themes load from the embedded assets");
        expect(magda::factoryThemes().size() == 2);

        const auto neon = magda::loadFactoryTheme("neon-cyberpunk");
        expect(neon.has_value());
        if (neon) {
            expect(neon->name == "Neon Cyberpunk");
            expect(neon->warnings.empty());
            magda::DarkTheme::setActivePalette(neon->palette);
            expect(magda::DarkTheme::getBackgroundColour().getARGB() == 0xFF0A0A14);
            expect(magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_PRIMARY).getARGB() ==
                   0xFFFF2D9E);
        }

        const auto concrete = magda::loadFactoryTheme("concrete-warehouse");
        expect(concrete.has_value());
        if (concrete) {
            expect(concrete->name == "Concrete Warehouse");
            expect(concrete->warnings.empty());
        }

        expect(!magda::loadFactoryTheme("missing-factory-theme").has_value());

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
