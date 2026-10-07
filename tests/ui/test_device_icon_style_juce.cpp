// The v1 device icon buttons draw their state in the glyph alone: grey at rest,
// green for power when on, red for close on hover, and never a filled backdrop.
// Set MAGDA_ICON_SNAPSHOT_DIR to also write each rendering as a PNG.

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"
#include "magda/daw/ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "magda/daw/ui/themes/ActiveTheme.hpp"

namespace {

using magda::daw::ui::node_header::applyDeviceIconStyle;
using magda::daw::ui::node_header::DeviceIcon;

juce::Image render(magda::SvgButton& button, const juce::String& name) {
    button.setBounds(0, 0, 30, 26);
    auto image = button.createComponentSnapshot(button.getLocalBounds(), true, 4.0f);
    if (const char* dir = std::getenv("MAGDA_ICON_SNAPSHOT_DIR")) {
        juce::FileOutputStream out(juce::File(dir).getChildFile(name + ".png"));
        if (out.openedOk()) {
            out.setPosition(0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
    }
    return image;
}

// Every opaque-ish pixel, by how close it is to @p colour.
int pixelsNear(const juce::Image& image, juce::Colour colour, int tolerance = 40) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const auto p = image.getPixelAt(x, y);
            if (p.getAlpha() < 200)
                continue;
            if (std::abs(p.getRed() - colour.getRed()) +
                    std::abs(p.getGreen() - colour.getGreen()) +
                    std::abs(p.getBlue() - colour.getBlue()) <
                tolerance)
                ++count;
        }
    return count;
}

int opaquePixels(const juce::Image& image) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y).getAlpha() >= 200)
                ++count;
    return count;
}

}  // namespace

class DeviceIconStyleTest final : public juce::UnitTest {
  public:
    DeviceIconStyleTest() : juce::UnitTest("Device Icon Style", "magda") {}

    void runTest() override {
        using magda::ActiveTheme;
        const auto grey = ActiveTheme::getColour(ActiveTheme::DEVICE_ICON);
        const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
        const auto white = juce::Colours::white;

        beginTest("Close draws a grey cross, not a white one");
        {
            magda::SvgButton close("Close", BinaryData::close_svg, BinaryData::close_svgSize);
            applyDeviceIconStyle(close, DeviceIcon::Close);
            const auto image = render(close, "close");
            logMessage("close: opaque " + juce::String(opaquePixels(image)) + ", grey " +
                       juce::String(pixelsNear(image, grey)) + ", white " +
                       juce::String(pixelsNear(image, white)));
            expect(pixelsNear(image, grey) > 0, "no grey glyph pixels");
            expectEquals(pixelsNear(image, white), 0, "white pixels in the cross");
        }

        beginTest("Power on is a green glyph with no green backdrop");
        {
            magda::SvgButton power("Power", BinaryData::power_svg, BinaryData::power_svgSize);
            applyDeviceIconStyle(power, DeviceIcon::Power, juce::Colour(0xFFE6E6E6));
            power.setToggleState(true, juce::dontSendNotification);
            power.setActive(true);
            const auto image = render(power, "power_on");
            const int greenPixels = pixelsNear(image, green);
            const int opaque = opaquePixels(image);
            logMessage("power: opaque " + juce::String(opaque) + ", green " +
                       juce::String(greenPixels));
            expect(greenPixels > 0, "no green glyph pixels");
            // A glyph is a thin ring; a filled backdrop would cover most of the button.
            expect(opaque < (30 * 4) * (26 * 4) / 3, "most of the button is painted");
        }
    }
};

static DeviceIconStyleTest deviceIconStyleTest;
