#include <juce_audio_devices/juce_audio_devices.h>

#include "magda/daw/audio/AudioDriverUtils.hpp"

namespace {

/**
 * Minimal AudioIODeviceType that reports a fixed set of device names. No real
 * hardware is touched: createDevice returns nullptr (the tests never open a
 * device), they only ask which type / which names get listed.
 */
class FakeDeviceType final : public juce::AudioIODeviceType {
  public:
    FakeDeviceType(const juce::String& typeName, juce::StringArray inputs,
                   juce::StringArray outputs, bool separateInputsAndOutputs = true)
        : juce::AudioIODeviceType(typeName),
          inputs_(std::move(inputs)),
          outputs_(std::move(outputs)),
          separateInputsAndOutputs_(separateInputsAndOutputs) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool wantInputNames = false) const override {
        return wantInputNames ? inputs_ : outputs_;
    }
    int getDefaultDeviceIndex(bool) const override {
        return 0;
    }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override {
        return -1;
    }
    bool hasSeparateInputsAndOutputs() const override {
        return separateInputsAndOutputs_;
    }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override {
        return nullptr;
    }

  private:
    juce::StringArray inputs_, outputs_;
    bool separateInputsAndOutputs_;
};

/** AudioDeviceManager seeded with two fake driver types: "FakeA" then "FakeB". */
class TwoTypeDeviceManager final : public juce::AudioDeviceManager {
  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& list) override {
        list.add(new FakeDeviceType("FakeA", {"A-in"}, {"A-out"}));
        list.add(new FakeDeviceType("FakeB", {"B-in"}, {"B-out"}));
    }
};

class SingleDeviceTypeManager final : public juce::AudioDeviceManager {
  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& list) override {
        list.add(new FakeDeviceType("FakeASIO", {"Unexpected input list"}, {"Full-duplex device"},
                                    false));
    }
};

}  // namespace

class AudioDriverTypeSelectionTest final : public juce::UnitTest {
  public:
    AudioDriverTypeSelectionTest() : juce::UnitTest("Audio Driver Type Selection", "magda") {}

    void runTest() override {
        beginTest("lists the ACTIVE driver, not the first available one");
        {
            // Regression guard for the "No such device" bug: the device list was
            // taken from getAvailableDeviceTypes()[0] (FakeA) regardless of which
            // driver was actually active.
            TwoTypeDeviceManager dm;
            auto& types = dm.getAvailableDeviceTypes();
            expectEquals(types.size(), 2);

            dm.setCurrentAudioDeviceType("FakeB", true);

            auto* chosen = magda::activeDeviceTypeFor(dm);
            expect(chosen != nullptr, "expected a resolved device type");
            expectEquals(chosen->getTypeName(), juce::String("FakeB"));
            expect(!magda::isSingleDeviceDriver(dm), "FakeB should keep separate selectors");
            expectEquals(chosen->getDeviceNames(true)[0], juce::String("B-in"));
            expectEquals(chosen->getDeviceNames(false)[0], juce::String("B-out"));

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.inputDeviceName = "Old input";
            setup.outputDeviceName = "Old output";
            magda::applySelectedAudioDeviceName(setup, "B-in", true,
                                                magda::isSingleDeviceDriver(dm));
            expectEquals(setup.inputDeviceName, juce::String("B-in"));
            expectEquals(setup.outputDeviceName, juce::String("Old output"));
        }

        beginTest("falls back to the first type when none is active");
        {
            // getCurrentDeviceTypeObject() returns null before any driver is
            // chosen; the helper should then pick the first available type.
            TwoTypeDeviceManager dm;
            expectEquals(dm.getAvailableDeviceTypes().size(), 2);

            auto* chosen = magda::activeDeviceTypeFor(dm);
            expect(chosen != nullptr, "expected a fallback device type");
            expectEquals(chosen->getTypeName(), juce::String("FakeA"));
        }

        beginTest("single-device drivers collapse to one full-duplex device");
        {
            SingleDeviceTypeManager dm;
            auto* chosen = magda::activeDeviceTypeFor(dm);
            expect(chosen != nullptr, "expected a resolved device type");
            expectEquals(chosen->getTypeName(), juce::String("FakeASIO"));
            expect(magda::isSingleDeviceDriver(dm), "FakeASIO should use one selector");

            auto dialogDeviceNames = chosen->getDeviceNames();
            expectEquals(dialogDeviceNames.size(), 1);
            expectEquals(dialogDeviceNames[0], juce::String("Full-duplex device"));

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.inputDeviceName = "Unexpected input list";
            setup.outputDeviceName = "Old output";
            magda::applySelectedAudioDeviceName(setup, dialogDeviceNames[0], true,
                                                magda::isSingleDeviceDriver(dm));
            expectEquals(setup.inputDeviceName, juce::String("Full-duplex device"));
            expectEquals(setup.outputDeviceName, juce::String("Full-duplex device"));
        }
    }
};

static AudioDriverTypeSelectionTest audioDriverTypeSelectionTest;
