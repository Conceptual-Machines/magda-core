#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/audio/plugins/compiled/MagdaMultibandCompiledPlugin.hpp"
#include "magda/daw/ui/components/chain/compiled/CompiledMultibandCurveView.hpp"

namespace {

using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
using View = magda::daw::ui::CompiledMultibandCurveView;
using HandleKind = View::MagdaTestHandleKind;

void addParameter(magda::DeviceInfo& device, int slot, float value) {
    magda::ParameterInfo param;
    param.paramIndex = slot;
    param.currentValue = value;
    device.parameters.push_back(param);
}

magda::DeviceInfo makeMultibandDevice(float lowXoHz) {
    magda::DeviceInfo device;
    addParameter(device, Mb::kLowXoSlot, lowXoHz);
    addParameter(device, Mb::kHighXoSlot, 2500.0f);

    addParameter(device, Mb::kLowLowerThresholdSlot, -48.0f);
    addParameter(device, Mb::kLowUpperThresholdSlot, -24.0f);
    addParameter(device, Mb::kMidLowerThresholdSlot, -60.0f);
    addParameter(device, Mb::kMidUpperThresholdSlot, -20.0f);
    addParameter(device, Mb::kHighLowerThresholdSlot, -48.0f);
    addParameter(device, Mb::kHighUpperThresholdSlot, -24.0f);
    return device;
}

void paintOnce(View& view) {
    view.setSize(360, view.getPreferredHeight());
    juce::Image image{juce::Image::ARGB, view.getWidth(), view.getHeight(), true};
    juce::Graphics graphics{image};
    view.paint(graphics);
}

}  // namespace

class MultibandCurveViewTest final : public juce::UnitTest {
  public:
    MultibandCurveViewTest() : juce::UnitTest("Multiband Curve View Tests", "magda") {}

    void runTest() override {
        beginTest("Threshold drag wins over overlapping ratio pill");
        {
            View view{Mb::xmlTypeName};
            view.updateFromDevice(makeMultibandDevice(120.0f));
            paintOnce(view);

            const float lowBandMidX =
                (view.magdaTestXForFrequency(20.0f) + view.magdaTestXForFrequency(120.0f)) * 0.5f;
            const float lowerThresholdY = view.magdaTestYForDb(-48.0f);

            const auto picked = view.magdaTestPickHandle(lowBandMidX, lowerThresholdY + 5.0f);
            expectEquals(picked.band, 0);
            expect(picked.kind == HandleKind::LowerThreshold,
                   "A low-band threshold grab must not become a below-ratio grab");
        }

        beginTest("Narrow low-band ratio hit area stays inside the band");
        {
            View view{Mb::xmlTypeName};
            view.updateFromDevice(makeMultibandDevice(40.0f));
            paintOnce(view);

            const float lowXoX = view.magdaTestXForFrequency(40.0f);
            const float lowerThresholdY = view.magdaTestYForDb(-48.0f);

            const auto picked = view.magdaTestPickHandle(lowXoX + 12.0f, lowerThresholdY + 8.0f);
            expect(!(picked.band == 0 && picked.kind == HandleKind::BelowRatio),
                   "A low-band ratio pill must not steal hit-tests in the mid band");
        }
    }
};

static MultibandCurveViewTest multibandCurveViewTest;
