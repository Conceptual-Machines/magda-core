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
                   juce::StringArray outputs)
        : juce::AudioIODeviceType(typeName),
          inputs_(std::move(inputs)),
          outputs_(std::move(outputs)) {}

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
        return true;
    }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override {
        return nullptr;
    }

  private:
    juce::StringArray inputs_, outputs_;
};

/** AudioDeviceManager seeded with two fake driver types: "FakeA" then "FakeB". */
class TwoTypeDeviceManager final : public juce::AudioDeviceManager {
  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& list) override {
        list.add(new FakeDeviceType("FakeA", {"A-in"}, {"A-out"}));
        list.add(new FakeDeviceType("FakeB", {"B-in"}, {"B-out"}));
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
            expectEquals(chosen->getDeviceNames(true)[0], juce::String("B-in"));
            expectEquals(chosen->getDeviceNames(false)[0], juce::String("B-out"));
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
    }
};

static AudioDriverTypeSelectionTest audioDriverTypeSelectionTest;
