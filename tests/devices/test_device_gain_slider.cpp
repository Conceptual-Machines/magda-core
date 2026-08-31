#include "magda/daw/ui/components/chain/layout/NodeHeaderStyles.hpp"

namespace {

class WheelEventSpy final : public juce::Component {
  public:
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {
        ++wheelEventCount;
    }

    int wheelEventCount = 0;
};

juce::MouseEvent wheelEventFor(juce::Component& component, juce::ModifierKeys modifiers = {}) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            {5.0f, 5.0f},
            modifiers,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &component,
            &component,
            now,
            {5.0f, 5.0f},
            now,
            1,
            false};
}

}  // namespace

class DeviceGainSliderTest final : public juce::UnitTest {
  public:
    DeviceGainSliderTest() : juce::UnitTest("Device Gain Slider Tests", "magda") {}

    void runTest() override {
        magda::LevelMeter meter;
        magda::daw::ui::node_header::GainSliderWithMeterTooltip slider(
            juce::Slider::LinearVertical, juce::Slider::NoTextBox, meter);
        WheelEventSpy parent;
        parent.addAndMakeVisible(slider);
        parent.setBounds(0, 0, 200, 200);
        slider.setBounds(0, 0, 20, 120);

        slider.setRange(-60.0, 12.0, 0.1);
        slider.setValue(-6.0, juce::dontSendNotification);
        slider.setSliderSnapsToMousePosition(false);

        juce::MouseWheelDetails wheel;
        wheel.deltaY = 0.5f;

        beginTest("Passive wheel starts disarmed");
        expect(!slider.isScrollWheelEnabled());

        beginTest("Passive wheel passes through to the scrolling parent");
        auto plainEvent = wheelEventFor(slider);
        slider.mouseWheelMove(plainEvent, wheel);

        expectWithinAbsoluteError(slider.getValue(), -6.0, 1.0e-9);
        expectEquals(parent.wheelEventCount, 1);

        beginTest("Grabbing the slider arms wheel adjustment");
        auto grabEvent =
            wheelEventFor(slider, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
        slider.mouseDown(grabEvent);
        slider.mouseUp(grabEvent);
        expect(slider.isScrollWheelEnabled());

        auto armedWheelEvent = wheelEventFor(slider);
        slider.mouseWheelMove(armedWheelEvent, wheel);
        expect(slider.getValue() != -6.0);
        expectEquals(parent.wheelEventCount, 1);

        beginTest("Leaving the slider disarms wheel adjustment");
        auto exitEvent = wheelEventFor(slider);
        slider.mouseExit(exitEvent);
        expect(!slider.isScrollWheelEnabled());

        const auto valueAfterArmedWheel = slider.getValue();
        auto disarmedWheelEvent = wheelEventFor(slider);
        slider.mouseWheelMove(disarmedWheelEvent, wheel);
        expectWithinAbsoluteError(slider.getValue(), valueAfterArmedWheel, 1.0e-9);
        expectEquals(parent.wheelEventCount, 2);
    }
};

static DeviceGainSliderTest deviceGainSliderTest;
