#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/AraTracks.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/PluginService.hpp"
#include "magda/daw/engine/host/AraTrackEditing.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_ara_track_editing_juce.cpp
 * @brief An ARA plugin on a track playing that track's clips live (#1579).
 *
 * The fixture renders its regions' source in place of its input, so a block processed at a
 * clip's timeline position must come out as the clip's file, whatever went in.
 */

namespace {

using magda::daw::audio::engine_adapter::EngineExternalDevice;
using magda::daw::engine_host::AraTrackEditing;

constexpr double kRate = 48000.0;
constexpr int kFileSamples = 96000;
constexpr int kBlock = 512;
constexpr magda::TrackId kTrack = 1;

float sampleAt(int channel, int index) {
    const float ramp = static_cast<float>(index) / static_cast<float>(kFileSamples);
    return channel == 0 ? ramp : -ramp;
}

juce::File writeRampFile(const juce::File& file) {
    juce::AudioBuffer<float> buffer(2, kFileSamples);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kFileSamples; ++i)
            buffer.setSample(c, i, sampleAt(c, i));

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream().release());
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(kRate).withNumChannels(2).withBitsPerSample(
            32));
    writer->writeFromAudioSampleBuffer(buffer, 0, kFileSamples);
    return file;
}

class AraTrackEditingTest final : public juce::UnitTest {
  public:
    AraTrackEditingTest() : juce::UnitTest("ARA Track Editing", "magda") {}

    void runTest() override {
        beginTest("The fixture is found as an ARA plugin");
        magda::test::resetJuceProjectState();
        const auto folder = juce::File::createTempFile("magda_ara_track_editing");
        folder.createDirectory();
        const auto file = writeRampFile(folder.getChildFile("vocal.wav"));

        juce::addDefaultFormatsToManager(formats_);
        juce::OwnedArray<juce::PluginDescription> found;
        juce::VST3PluginFormat().findAllTypesForFile(found, MAGDA_ARA_IDENTITY_PLUGIN);
        expectEquals(found.size(), 1);
        if (found.isEmpty())
            return;
        plugin_ = *found[0];

        // Beat 4 at 120 bpm: the clip starts two seconds in.
        clip_ = magda::ClipManager::getInstance().createAudioClipBeats(kTrack, 4.0, 4.0,
                                                                       file.getFullPathName());

        {
            AraTrackEditing editing(formats_, tempo);
            editing.notify = [this](const juce::String& message) { messages_.add(message); };
            auto device = load();
            if (device == nullptr)
                return;
            editing.attach(kTrack, *device);
            device->prepare({.sampleRate = kRate, .maxBlockSize = kBlock, .numChannels = 2});

            testPlaysTheClipAtItsPosition(*device);
            testFollowsAMovedClip(*device);
            testPlaysAReversedClipRaw(*device);
            testPlaysALoopingClipOnce(*device);
            testArchives(editing);

            // Devices first, as the host lets them go, then the documents.
            device.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            editing.forgetProject();
        }

        testRestoresFromTheArchive();
        testFindsTheAraPluginOnATrack();

        magda::test::resetJuceProjectState();
        folder.deleteRecursively();
    }

  private:
    static const magda::engine::TempoMap& tempo() {
        static const magda::engine::TempoMap map;
        return map;
    }

    std::unique_ptr<EngineExternalDevice> load() {
        juce::String error;
        auto instance = formats_.createPluginInstance(plugin_, kRate, kBlock, error);
        expect(instance != nullptr, error);
        if (instance == nullptr)
            return nullptr;
        magda::DeviceInfo info;
        info.name = plugin_.name;
        info.format = magda::PluginFormat::VST3;
        return std::make_unique<EngineExternalDevice>(std::move(instance), info, false);
    }

    /** @brief Process one block at @p seconds with the input full of 0.5, and return it. */
    juce::AudioBuffer<float> blockAt(EngineExternalDevice& device, double seconds) {
        juce::AudioBuffer<float> audio(2, kBlock);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill(audio.getWritePointer(c), 0.5f, kBlock);

        magda::engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(audio);
        block.block.numSamples = kBlock;
        block.block.playing = true;
        block.block.seconds = {seconds, seconds + kBlock / kRate};
        block.block.beats = {seconds * 2.0, (seconds + kBlock / kRate) * 2.0};
        device.process(block);
        return audio;
    }

    /** @brief How many samples of @p audio miss the file from @p fileStart on. */
    static int mismatches(const juce::AudioBuffer<float>& audio, int fileStart) {
        int missed = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i) {
                const auto expected = fileStart + i >= 0 && fileStart + i < kFileSamples
                                          ? sampleAt(c, fileStart + i)
                                          : 0.0f;
                if (std::abs(audio.getSample(c, i) - expected) > 1.0e-6f)
                    ++missed;
            }
        return missed;
    }

    void testPlaysTheClipAtItsPosition(EngineExternalDevice& device) {
        beginTest("The track plays its clip through the plugin, in place of its input");
        expectEquals(mismatches(blockAt(device, 2.0), 0), 0, "at the clip's start");
        expectEquals(mismatches(blockAt(device, 2.5), static_cast<int>(0.5 * kRate)), 0,
                     "half a second in");
        expectEquals(mismatches(blockAt(device, 1.0), -kFileSamples), 0, "before it: silence");
    }

    void testFollowsAMovedClip(EngineExternalDevice& device) {
        beginTest("Moving the clip moves what the plugin plays");
        auto& clips = magda::ClipManager::getInstance();
        clips.getClip(clip_)->setPlacementBeats(8.0, 4.0);
        clips.forceNotifyClipPropertyChanged(clip_);

        expectEquals(mismatches(blockAt(device, 4.0), 0), 0, "at the new start");
        expectEquals(mismatches(blockAt(device, 2.0), -kFileSamples), 0, "at the old: silence");
    }

    void testPlaysAReversedClipRaw(EngineExternalDevice& device) {
        beginTest("A reversed clip plays raw through the plugin: reverse stands down there");
        auto& clips = magda::ClipManager::getInstance();
        clips.getClip(clip_)->primaryEvent()->reversed = true;
        clips.forceNotifyClipPropertyChanged(clip_);

        expectEquals(mismatches(blockAt(device, 4.0), 0), 0, "forwards, from the file's start");

        clips.getClip(clip_)->primaryEvent()->reversed = false;
        clips.forceNotifyClipPropertyChanged(clip_);
    }

    void testPlaysALoopingClipOnce(EngineExternalDevice& device) {
        beginTest("A looping clip plays once, raw, through the plugin: loop stands down there");
        auto& clips = magda::ClipManager::getInstance();
        auto* clip = clips.getClip(clip_);
        clip->loopEnabled = true;
        clip->primaryEvent()->loopStartSamples = 0;
        clip->primaryEvent()->loopLengthSamples = static_cast<std::int64_t>(0.5 * kRate);
        clips.forceNotifyClipPropertyChanged(clip_);

        // Three quarters of a second in is past the loop's end, and still the file, not a wrap.
        expectEquals(mismatches(blockAt(device, 4.75), static_cast<int>(0.75 * kRate)), 0);

        clip = clips.getClip(clip_);
        clip->loopEnabled = false;
        clips.forceNotifyClipPropertyChanged(clip_);
    }

    void testArchives(AraTrackEditing& editing) {
        beginTest("Saving archives the plugin's document beside the project");
        const auto media = magda::ProjectManager::getInstance().getMediaDirectory();
        editing.saveArchives(media);

        juce::MemoryBlock data;
        AraTrackEditing::archiveFile(media, plugin_.createIdentifierString()).loadFileAsData(data);
        const std::string bytes(static_cast<const char*>(data.getData()), data.getSize());
        expect(bytes.starts_with("MAGDA-ARA 1"));
        expect(bytes.find("clip:" + std::to_string(clip_) + "/event:") != std::string::npos);
    }

    void testRestoresFromTheArchive() {
        beginTest("A plugin attached in a reopened project restores the archive");
        messages_.clear();
        {
            AraTrackEditing editing(formats_, tempo);
            editing.notify = [this](const juce::String& message) { messages_.add(message); };
            auto device = load();
            if (device == nullptr)
                return;
            editing.attach(kTrack, *device);
            device->prepare({.sampleRate = kRate, .maxBlockSize = kBlock, .numChannels = 2});
            expect(messages_.isEmpty(), messages_.joinIntoString("\n"));
            expectEquals(mismatches(blockAt(*device, 4.0), 0), 0, "and still plays the clip");

            device.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            editing.forgetProject();
        }
        AraTrackEditing::archiveFile(magda::ProjectManager::getInstance().getMediaDirectory(),
                                     plugin_.createIdentifierString())
            .deleteFile();
    }

    void testFindsTheAraPluginOnATrack() {
        beginTest(
            "A track with an ARA plugin is found, so its clips' stretch and pitch stand down");
        auto& service = magda::PluginService::getInstance();
        if (service.knownList() == nullptr)
            service.useOwnList();
        service.knownList()->addType(plugin_);

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Vocal");
        const auto clip = magda::ClipManager::getInstance().createAudioClipBeats(
            track, 0.0, 4.0, "/tmp/magda_ara_track_editing.wav");
        expect(!magda::araPluginForClip(clip).has_value(), "no plugin yet");

        magda::DeviceInfo device;
        device.name = plugin_.name;
        device.format = magda::PluginFormat::VST3;
        device.uniqueId = plugin_.createIdentifierString();
        const auto id = tracks.addDeviceToTrack(track, device);
        expect(magda::araPluginForClip(clip) == plugin_.name);

        tracks.removeDeviceFromTrack(track, id);
        expect(!magda::araPluginOnTrack(track).has_value(), "and gone with it");
        service.knownList()->removeType(plugin_);
    }

    juce::AudioPluginFormatManager formats_;
    juce::PluginDescription plugin_;
    magda::ClipId clip_ = magda::INVALID_CLIP_ID;
    juce::StringArray messages_;
};

AraTrackEditingTest araTrackEditingTest;

}  // namespace
