#include <juce_audio_devices/juce_audio_devices.h>

#include "magda/daw/audio/midi/ActiveMidiInputs.hpp"
#include "magda/daw/core/Config.hpp"

/// @file Audio Settings' Active MIDI inputs, saved in Config and nowhere else.

namespace {

constexpr auto kPortName = "MAGDA Active Inputs Test";

/** @brief A virtual port, once CoreMIDI or ALSA lists it among the inputs; nothing if it cannot. */
struct VirtualPort {
    VirtualPort() {
        auto listed = false;
        const auto connection = juce::MidiDeviceListConnection::make([&listed] { listed = true; });
        port = juce::MidiOutput::createNewDevice(kPortName);
        if (port == nullptr)
            return;

        // The platform announces the new port asynchronously, through the message loop.
        for (auto tick = 0; tick < 200 && !input.has_value(); ++tick) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            if (!listed)
                continue;
            for (const auto& available : juce::MidiInput::getAvailableDevices())
                if (available.name == kPortName)
                    input = available;
        }
    }

    std::unique_ptr<juce::MidiOutput> port;
    std::optional<juce::MidiDeviceInfo> input;
};

}  // namespace

class ActiveMidiInputsTest final : public juce::UnitTest {
  public:
    ActiveMidiInputsTest() : juce::UnitTest("Active MIDI Inputs", "magda") {}

    void runTest() override {
        auto& config = magda::Config::getInstance();

        beginTest("an input is matched by name, whatever its case");
        {
            config.setInactiveMidiInputs({"Launchkey DAW Out"});
            expect(!config.isMidiInputActive("launchkey daw out"));
            expect(config.isMidiInputActive("Launchkey MIDI Out"));
        }

        beginTest("the inactive inputs survive the config file");
        {
            config.setInactiveMidiInputs({"M4", "Launchkey DAW Out"});
            config.save();
            config.setInactiveMidiInputs({});
            config.load();
            expect(config.getInactiveMidiInputs() ==
                   std::vector<std::string>{"M4", "Launchkey DAW Out"});
        }

        beginTest("a port that will not open is not switched off");
        {
            config.setInactiveMidiInputs({});
            juce::AudioDeviceManager devices;
            juce::Array<juce::MidiDeviceInfo> held;
            held.add(juce::MidiDeviceInfo("Held Elsewhere", "magda-test-no-such-port"));
            magda::ActiveMidiInputs inputs(devices, held);

            expect(!devices.isMidiInputDeviceEnabled("magda-test-no-such-port"));
            expect(!inputs.saveChanges());
            expect(config.getInactiveMidiInputs().empty());
        }

        const VirtualPort virtualPort;
        const auto& input = virtualPort.input;
        if (!input.has_value()) {
            logMessage("No virtual MIDI port on this platform; toggling is not exercised");
            config.setInactiveMidiInputs({});
            config.save();
            return;
        }

        beginTest("unticking saves the input as inactive, and ticking brings it back");
        {
            config.setInactiveMidiInputs({});
            juce::AudioDeviceManager devices;
            magda::ActiveMidiInputs inputs(devices, juce::Array<juce::MidiDeviceInfo>{*input});
            expect(devices.isMidiInputDeviceEnabled(input->identifier));

            devices.setMidiInputDeviceEnabled(input->identifier, false);
            expect(inputs.saveChanges());
            expect(!config.isMidiInputActive(kPortName));

            devices.setMidiInputDeviceEnabled(input->identifier, true);
            expect(inputs.saveChanges());
            expect(config.isMidiInputActive(kPortName));
            expect(!inputs.saveChanges(), "nothing moved since");
        }

        beginTest("an inactive input is shown unticked");
        {
            config.setInactiveMidiInputs({kPortName});
            juce::AudioDeviceManager devices;
            magda::ActiveMidiInputs inputs(devices, juce::Array<juce::MidiDeviceInfo>{*input});
            expect(!devices.isMidiInputDeviceEnabled(input->identifier));
        }

        config.setInactiveMidiInputs({});
        config.save();
    }
};

namespace {
ActiveMidiInputsTest activeMidiInputsTest;
}  // namespace
