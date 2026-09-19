#include <juce_core/juce_core.h>

#include "magda/daw/engine/MagdaEngineBehaviour.hpp"
#include "magda/daw/engine/MagdaPropertyStorage.hpp"

/// @file Tracktion under the native engine: MIDI, no audio interface, its own left alone (#2747).

namespace {

constexpr auto kSavedSetup = R"(<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="audio_device_setup">
    <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="M4" audioInputDeviceName="M4"
                 audioDeviceRate="48000.0" audioDeviceBufferSize="256"/>
  </VALUE>
</PROPERTIES>
)";

/** @brief A Settings.xml of its own, holding the interface Tracktion last opened. */
struct ScratchSettings {
    ScratchSettings() {
        folder().createDirectory();
        file().replaceWithText(kSavedSetup);
    }

    ~ScratchSettings() {
        folder().deleteRecursively();
    }

    juce::File folder() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(appName);
    }

    juce::File file() const {
        return folder().getChildFile("Settings.xml");
    }

    /** @brief The saved interface's output name, as Tracktion would read it back. */
    juce::String savedOutput() const {
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile settings(file(), options);
        const auto setup = settings.getXmlValue("audio_device_setup");
        return setup != nullptr ? setup->getStringAttribute("audioOutputDeviceName")
                                : juce::String();
    }

    juce::String appName = "MAGDA-test-" + juce::Uuid().toDashedString();
};

}  // namespace

class TracktionWithoutAudioTest final : public juce::UnitTest {
  public:
    TracktionWithoutAudioTest() : juce::UnitTest("Tracktion Without Audio", "magda") {}

    void runTest() override {
        beginTest("Tracktion opens no interface and keeps the one it saved");
        {
            ScratchSettings scratch;
            {
                tracktion::Engine engine(
                    std::make_unique<magda::MagdaPropertyStorage>(scratch.appName, false), nullptr,
                    std::make_unique<magda::MagdaEngineBehaviour>(false));
                auto& devices = engine.getDeviceManager();
                initialiseAndSave(devices);

                expect(devices.deviceManager.getAvailableDeviceTypes().isEmpty());
                expect(devices.deviceManager.getCurrentAudioDevice() == nullptr);
                expectEquals(devices.getNumWaveOutDevices(), 0);
            }
            expectEquals(scratch.savedOutput(), juce::String("M4"));
        }

        beginTest("without the guard, Tracktion saves an interface with no name over it");
        {
            ScratchSettings scratch;
            {
                tracktion::Engine engine(
                    std::make_unique<magda::MagdaPropertyStorage>(scratch.appName, true), nullptr,
                    std::make_unique<magda::MagdaEngineBehaviour>(false));
                initialiseAndSave(engine.getDeviceManager());
            }
            expectEquals(scratch.savedOutput(), juce::String());
        }
    }

  private:
    /** @brief Bring @p devices up as the wrapper does, and run the saves it queues. */
    static void initialiseAndSave(tracktion::DeviceManager& devices) {
        devices.initialise(0, 0);
        devices.deviceManager.dispatchPendingMessages();
        devices.dispatchPendingUpdates();
    }
};

namespace {
TracktionWithoutAudioTest tracktionWithoutAudioTest;
}  // namespace
