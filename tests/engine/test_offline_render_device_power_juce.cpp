#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/OfflineRenderHelper.hpp"
#include "magda/engine/io/AudioFileMetadata.hpp"

namespace {

magda::DeviceInfo makeInternalDevice(const juce::String& name, const juce::String& pluginId) {
    magda::DeviceInfo device;
    device.name = name;
    device.format = magda::PluginFormat::Internal;
    device.pluginId = pluginId;
    return device;
}

}  // namespace

/**
 * @brief Powered-off devices must survive an offline render (#1880)
 *
 * The export prep used to enable every plugin on every audio track so the test
 * tone generator would sound offline. That blanket write rendered bypassed
 * devices into the file and left them running afterwards, while the UI power
 * button still read "off" because the model was never touched.
 */
class OfflineRenderDevicePowerTest final : public juce::UnitTest {
  public:
    OfflineRenderDevicePowerTest() : juce::UnitTest("Offline Render Device Power Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testBypassedDeviceStaysBypassed(); });
        magda::test::runWithCleanJuceState([this] { testChainPowerOffStaysOff(); });
        magda::test::runWithCleanJuceState([this] { testTracktionBwavMetadataRestored(); });
        magda::test::runWithCleanJuceState([this] { testTracktionRenderWritesMagdaBwav(); });
    }

  private:
    void testTracktionRenderWritesMagdaBwav() {
        beginTest("Tracktion offline render keeps MAGDA BWF origin metadata");

        auto& wrapper = magda::test::getSharedEngine();
        auto& tracks = magda::TrackManager::getInstance();
        tracks.clearAllTracks();
        tracks.setAudioEngine(&wrapper);

        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const auto source = directory.getChildFile("magda_tracktion_metadata_source.wav");
        const auto output = directory.getChildFile("magda_tracktion_metadata_export.wav");
        source.deleteFile();
        output.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
            auto writer = wav.createWriterFor(
                stream,
                juce::AudioFormatWriterOptions()
                    .withSampleRate(48000.0)
                    .withNumChannels(1)
                    .withBitsPerSample(32)
                    .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect(writer != nullptr);
            if (writer == nullptr)
                return;
            juce::AudioBuffer<float> silence(1, 96000);
            silence.clear();
            expect(writer->writeFromAudioSampleBuffer(silence, 0, silence.getNumSamples()));
        }

        const auto trackId = tracks.createTrack("Metadata export");
        magda::ClipManager::getInstance().createAudioClipBeats(trackId, 0.0, 4.0,
                                                               source.getFullPathName());
        magda::OfflineRenderRequest request;
        request.destination = output;
        request.range = {{0.0}, {4.0}};
        request.sampleRate = 48000.0;
        request.bitDepth = 32;
        request.oneShot = true;
        auto session = wrapper.createOfflineRenderSession(false);
        auto task = session ? session->createTask(request) : nullptr;
        expect(task != nullptr);
        if (task != nullptr)
            expect(task->run().success);

        juce::WavAudioFormat wav;
        auto input = output.createInputStream();
        std::unique_ptr<juce::AudioFormatReader> reader(
            input != nullptr ? wav.createReaderFor(input.release(), true) : nullptr);
        expect(reader != nullptr);
        if (reader != nullptr) {
            expectEquals(reader->metadataValues[juce::WavAudioFormat::bwavDescription],
                         juce::String("MAGDA offline render"));
            expect(
                reader->metadataValues[juce::WavAudioFormat::bwavOriginator].startsWith("MAGDA "));
            expectEquals(
                reader->metadataValues[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue(),
                static_cast<juce::int64>(0));
        }
        reader.reset();
        task.reset();
        session.reset();
        tracks.clearAllTracks();
        source.deleteFile();
        output.deleteFile();
    }

    void testTracktionBwavMetadataRestored() {
        beginTest("Tracktion BWF fields are restored without changing the audio");

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("magda_tracktion_bwf_metadata_test.wav");
        file.deleteFile();
        magda::engine::AudioFileMetadata facts;
        facts.tempo = 100.0;
        facts.description = "MAGDA offline render";
        facts.originator = "MAGDA test";
        auto intendedMap = magda::engine::wavMetadataFor(facts);
        intendedMap[juce::WavAudioFormat::bwavTimeReference] = "22050";
        auto overwritten = intendedMap;
        overwritten[juce::WavAudioFormat::bwavDescription] = {};
        overwritten[juce::WavAudioFormat::bwavOriginator] = "tracktion";
        overwritten[juce::WavAudioFormat::bwavTimeReference] = "0";

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
        expect(output != nullptr);
        if (output == nullptr)
            return;
        auto writer = wav.createWriterFor(
            output,
            juce::AudioFormatWriterOptions()
                .withSampleRate(44100.0)
                .withNumChannels(1)
                .withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)
                .withMetadataValues(overwritten));
        expect(writer != nullptr);
        if (writer == nullptr)
            return;
        juce::AudioBuffer<float> audio(1, 128);
        audio.clear();
        audio.setSample(0, 0, 0.25f);
        expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()));
        writer.reset();

        juce::StringPairArray intended;
        for (const auto& [key, value] : intendedMap)
            intended.set(key, value);
        expect(magda::restoreTracktionWavMetadata(file, intended));

        std::unique_ptr<juce::AudioFormatReader> reader(
            wav.createReaderFor(file.createInputStream().release(), true));
        expect(reader != nullptr);
        if (reader != nullptr) {
            expectEquals(reader->metadataValues[juce::WavAudioFormat::bwavDescription],
                         juce::String("MAGDA offline render"));
            expectEquals(reader->metadataValues[juce::WavAudioFormat::bwavOriginator],
                         juce::String("MAGDA test"));
            expectEquals(
                reader->metadataValues[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue(),
                static_cast<juce::int64>(22050));
            expectEquals(reader->metadataValues[juce::WavAudioFormat::acidTempo].getIntValue(),
                         100);
            juce::AudioBuffer<float> stored(1, 128);
            expect(reader->read(&stored, 0, 128, 0, true, false));
            expectEquals(stored.getSample(0, 0), 0.25f);
        }
        reader.reset();
        file.deleteFile();
    }

    void testBypassedDeviceStaysBypassed() {
        beginTest("Per-device power off survives render prep");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr, "AudioBridge and Edit must exist");
        if (!bridge || !edit)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Render power");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);

        auto plugin = bridge->getPlugin(fxPath);
        expect(plugin != nullptr, "Internal FX should have a TE plugin");
        if (plugin == nullptr) {
            trackManager.clearAllTracks();
            return;
        }

        expect(plugin->isEnabled(), "Freshly added device should be enabled");

        // The power button in the device header / mixer mini chain goes through
        // this setter, which is what pushes enablement into the engine.
        trackManager.setDeviceInChainBypassedByPath(fxPath, true);
        expect(!plugin->isEnabled(), "Powering the device off should disable the TE plugin");

        magda::prepareEditForOfflineRender(*edit);

        expect(!plugin->isEnabled(),
               "Render prep must leave a powered-off device disabled (#1880)");

        auto* device = trackManager.getDeviceInChainByPath(fxPath);
        expect(device != nullptr && device->bypassed,
               "Render prep must not touch the model's bypassed flag");

        trackManager.clearAllTracks();
    }

    void testChainPowerOffStaysOff() {
        beginTest("Track chain power off survives render prep");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr, "AudioBridge and Edit must exist");
        if (!bridge || !edit)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Chain power render");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);

        auto plugin = bridge->getPlugin(fxPath);
        expect(plugin != nullptr, "Internal FX should have a TE plugin");
        if (plugin == nullptr) {
            trackManager.clearAllTracks();
            return;
        }

        trackManager.setChainEnabled(trackId, false);
        expect(!plugin->isEnabled(), "Chain power off should disable the TE plugin");

        magda::prepareEditForOfflineRender(*edit);

        expect(!plugin->isEnabled(), "Render prep must leave a powered-off chain disabled (#1880)");

        auto* device = trackManager.getDeviceInChainByPath(fxPath);
        expect(device != nullptr && !device->bypassed,
               "Chain power must not write the device's own bypassed flag");

        trackManager.clearAllTracks();
    }
};

static OfflineRenderDevicePowerTest offlineRenderDevicePowerTest;
