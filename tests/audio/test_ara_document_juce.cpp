#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "magda/daw/audio/ara/AraDocument.hpp"
#include "magda/daw/engine/host/EngineRuntimeFactory.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_ara_document_juce.cpp
 * @brief A host-side ARA document over a real ARA plugin (#1579).
 *
 * The fixture plugin renders its regions' source unchanged, reading it through the host, so a
 * block it renders is compared sample for sample against the file the document named.
 */

namespace {

namespace ara = magda::daw::audio::ara;

constexpr double kRate = 48000.0;
constexpr int kFileSamples = 96000;
constexpr int kBlock = 512;

/** @brief A sample nobody would render by accident: a ramp, negated on the right. */
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

/** @brief A transport parked at one sample, playing. */
struct ParkedPlayHead final : juce::AudioPlayHead {
    juce::int64 position = 0;

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setTimeInSamples(position);
        info.setTimeInSeconds(static_cast<double>(position) / kRate);
        info.setIsPlaying(true);
        return info;
    }
};

class AraDocumentTest final : public juce::UnitTest {
  public:
    AraDocumentTest() : juce::UnitTest("ARA Host Document", "magda") {}

    void runTest() override {
        const auto file = writeRampFile(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                            .getChildFile("magda_ara_ramp.wav"));

        beginTest("the fixture scans as an ARA plugin");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile(found, MAGDA_ARA_IDENTITY_PLUGIN);
        expectEquals(found.size(), 1);
        if (found.isEmpty())
            return;
        expect(found[0]->hasARAExtension);

        juce::String error;
        auto instance = format.createInstanceFromDescription(*found[0], kRate, kBlock, error);
        expect(instance != nullptr, error);
        if (instance == nullptr)
            return;

        beginTest("a document is created from the plugin's factory");
        magda::daw::engine_host::EngineFileReaders files;
        // The render instance is destroyed mid-test; the anchor is what keeps the module loaded.
        auto anchor = format.createInstanceFromDescription(*found[0], kRate, kBlock, error);
        auto document =
            ara::AraDocument::create(factoryOf(*instance), std::move(anchor), "Test", files);
        expect(document != nullptr);
        if (document == nullptr)
            return;
        document->setMusicalContext(magda::engine::TempoMap());

        testRendersTheSourceThroughTheHost(*document, *instance, file);
        instance.reset();

        testSyncFollowsTheModel(*document, file);
        file.deleteFile();
    }

  private:
    juce::ARAFactoryWrapper factoryOf(juce::AudioPluginInstance& instance) {
        std::optional<juce::ARAFactoryWrapper> factory;
        juce::createARAFactoryAsync(instance,
                                    [&](juce::ARAFactoryWrapper f) { factory = std::move(f); });
        for (int i = 0; i < 200 && !factory.has_value(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        expect(factory.has_value() && factory->get() != nullptr);
        return factory.value_or(juce::ARAFactoryWrapper{});
    }

    static ara::AraRegionSpec spec(const juce::File& file, ara::AraRegionTiming timing) {
        return {.clipId = 1,
                .eventId = 1,
                .trackId = 1,
                .sourceId = 1,
                .sourcePath = file.getFullPathName().toStdString(),
                .timing = timing,
                .name = "Clip"};
    }

    void testRendersTheSourceThroughTheHost(ara::AraDocument& document,
                                            juce::AudioPluginInstance& instance,
                                            const juce::File& file) {
        beginTest("a region renders its source stretch at its playback position");

        // Half a second into the file, one second long, from two seconds on the timeline.
        const auto refused = document.syncRegions({spec(file, {.startInModification = 0.5,
                                                               .durationInModification = 1.0,
                                                               .startInPlayback = 2.0,
                                                               .durationInPlayback = 1.0})});
        expect(refused.empty());
        expectEquals(static_cast<int>(document.sourceCount()), 1);
        expectEquals(static_cast<int>(document.regionCount()), 1);

        auto extension = document.bind(instance, ARA::kARAPlaybackRendererRole);
        expect(extension.isValid());
        auto renderer = extension.getPlaybackRendererInterface();
        expect(renderer.isValid());
        renderer.add(*document.region(1, 1));

        ParkedPlayHead head;
        head.position = static_cast<juce::int64>(2.0 * kRate) - kBlock / 2;
        instance.setPlayHead(&head);
        instance.setNonRealtime(true);
        instance.prepareToPlay(kRate, kBlock);

        juce::AudioBuffer<float> block(2, kBlock);
        block.clear();
        juce::MidiBuffer midi;
        instance.processBlock(block, midi);

        // Silence before the region; then the file from half a second in.
        const int sourceStart = static_cast<int>(0.5 * kRate);
        int mismatches = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i) {
                const float expected =
                    i < kBlock / 2 ? 0.0f : sampleAt(c, sourceStart + i - kBlock / 2);
                if (std::abs(block.getSample(c, i) - expected) > 1.0e-6f)
                    ++mismatches;
            }
        expectEquals(mismatches, 0);

        instance.releaseResources();
        instance.setPlayHead(nullptr);

        // JUCE 9.0.3's ~Registry erases from the map it is iterating, so a registry must be
        // emptied before it dies.
        renderer.remove(*document.region(1, 1));
    }

    void testSyncFollowsTheModel(ara::AraDocument& document, const juce::File& file) {
        beginTest("a region whose event is gone is removed with its source");
        document.syncRegions({});
        expectEquals(static_cast<int>(document.regionCount()), 0);
        expectEquals(static_cast<int>(document.sourceCount()), 0);

        beginTest("a source that cannot be opened is refused, not placed");
        const auto refused = document.syncRegions(
            {spec(file.getSiblingFile("magda_ara_missing.wav"), {.durationInModification = 1.0})});
        expectEquals(static_cast<int>(refused.size()), 1);
        expectEquals(static_cast<int>(document.regionCount()), 0);

        beginTest("a region that moves is updated in place");
        document.syncRegions(
            {spec(file, {.durationInModification = 1.0, .durationInPlayback = 1.0})});
        const auto* before = document.region(1, 1);
        document.syncRegions({spec(
            file,
            {.durationInModification = 1.0, .startInPlayback = 4.0, .durationInPlayback = 1.0})});
        expect(document.region(1, 1) == before);
        document.syncRegions({});
    }
};

AraDocumentTest araDocumentTest;

}  // namespace
