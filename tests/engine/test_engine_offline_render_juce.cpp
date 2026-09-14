#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/AudioEngine.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"
#include "magda/daw/engine/host/EngineOfflineRender.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/TrackFreeze.hpp"

/**
 * @file test_engine_offline_render_juce.cpp
 * @brief A render on magda::engine, read back off disk (#2555).
 *
 * Here rather than in magda_tests because the render reads the project out of
 * TrackManager and ClipManager, and no live session runs: the host is never put
 * on a device, so every device is built for the render.
 */

namespace {

namespace host = magda::daw::engine_host;

constexpr double kSampleRate = 44100.0;

/// Four beats at the host's 120 bpm.
constexpr int kRangeSamples = 88200;

constexpr int kFftOrder = 13;
constexpr int kFftSize = 1 << kFftOrder;
constexpr double kToneHz = 1000.0;

/// -90 dBFS, a hair over one 16-bit LSB: where rounding without dither is audible.
constexpr float kQuietAmplitude = 3.16e-5f;

using Signal = std::function<float(int sample)>;

Signal sine(float amplitude, double hz) {
    return [amplitude, hz](int sample) {
        return amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz *
                                                       static_cast<double>(sample) / kSampleRate));
    };
}

juce::File scratchDirectory() {
    auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("magda_engine_offline_render_test");
    directory.createDirectory();
    return directory;
}

/** @brief @p signal on both channels of a 32-bit float WAV, the length of the render range. */
juce::File writeSource(const juce::String& name, const Signal& signal) {
    const auto file = scratchDirectory().getChildFile(name + ".wav");
    file.deleteFile();

    juce::AudioBuffer<float> buffer(2, kRangeSamples);
    for (auto sample = 0; sample < kRangeSamples; ++sample) {
        buffer.setSample(0, sample, signal(sample));
        buffer.setSample(1, sample, signal(sample));
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate(kSampleRate)
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(32));
    writer->writeFromAudioSampleBuffer(buffer, 0, kRangeSamples);
    return file;
}

/** @brief A track whose one clip plays @p source from beat zero for the whole range. */
void trackPlaying(const juce::File& source) {
    const auto trackId = magda::TrackManager::getInstance().createTrack("Audio");
    magda::ClipManager::getInstance().createAudioClipBeats(trackId, 0.0, 4.0,
                                                           source.getFullPathName());
}

juce::AudioBuffer<float> readBack(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

magda::OfflineRenderRequest requestFor(const juce::File& destination, int bitDepth,
                                       magda::OfflineRenderDither dither) {
    magda::OfflineRenderRequest request;
    request.destination = destination;
    request.bitDepth = bitDepth;
    request.dither = dither;
    request.sampleRate = kSampleRate;
    request.range = {{0.0}, {4.0}};
    return request;
}

magda::OfflineRenderResult render(const magda::OfflineRenderRequest& request) {
    host::EngineHost engineHost;
    auto session = engineHost.createOfflineRenderSession(false);
    auto task = session->createTask(request);
    if (task == nullptr)
        return {false, "no task"};

    return task->run();
}

/// The magnitude spectrum of @p buffer's first channel from @p offset, Hann-windowed.
std::vector<float> spectrumOf(const juce::AudioBuffer<float>& buffer, int offset) {
    std::vector<float> data(static_cast<std::size_t>(kFftSize) * 2, 0.0f);

    for (auto at = 0; at < kFftSize; ++at) {
        const auto window =
            0.5 - (0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(at) / kFftSize));
        data[static_cast<std::size_t>(at)] =
            buffer.getSample(0, offset + at) * static_cast<float>(window);
    }

    juce::dsp::FFT fft(kFftOrder);
    fft.performFrequencyOnlyForwardTransform(data.data());
    data.resize(static_cast<std::size_t>(kFftSize) / 2);
    return data;
}

int binFor(double hz) {
    return static_cast<int>(std::lround(hz * kFftSize / kSampleRate));
}

float energyAt(const std::vector<float>& spectrum, double hz) {
    auto sum = 0.0f;
    for (auto bin = binFor(hz) - 2; bin <= binFor(hz) + 2; ++bin)
        sum += spectrum[static_cast<std::size_t>(bin)] * spectrum[static_cast<std::size_t>(bin)];
    return sum;
}

/// The odd harmonics a three-level staircase is made of.
float harmonicEnergy(const std::vector<float>& spectrum) {
    return energyAt(spectrum, kToneHz * 3.0) + energyAt(spectrum, kToneHz * 5.0) +
           energyAt(spectrum, kToneHz * 7.0);
}

/// Everything between 200 Hz and 20 kHz but the tone's own bins.
float floorEnergy(const std::vector<float>& spectrum) {
    auto sum = 0.0f;
    for (auto bin = binFor(200.0); bin <= binFor(20000.0); ++bin)
        if (std::abs(bin - binFor(kToneHz)) > 3)
            sum +=
                spectrum[static_cast<std::size_t>(bin)] * spectrum[static_cast<std::size_t>(bin)];
    return sum;
}

/** @brief Rings on the first sample of every block once it has heard one, until reset. */
class RingingDevice final : public magda::engine::EngineDevice {
  public:
    void reset() override {
        ringing = false;
    }

    double tailSeconds() const override {
        return tail;
    }

    void process(magda::engine::DeviceBlock& block) override {
        if (ringing)
            for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel)
                block.audio.setSample(static_cast<int>(channel), 0, 1.0f);

        ringing = true;
    }

    bool ringing = false;
    double tail = 0.0;
};

/** @brief A host whose live session holds one device, at @p key. */
class LendingHost final : public host::OfflineRenderHost {
  public:
    LendingHost(magda::engine::DeviceKey key, std::shared_ptr<RingingDevice> device)
        : key_(key), device_(std::move(device)) {}

    void beginOfflineRender() override {}
    void endOfflineRender(bool) override {}

    std::shared_ptr<magda::engine::EngineDevice> liveDevice(
        magda::engine::DeviceKey key) const override {
        return key == key_ ? device_ : nullptr;
    }

    std::optional<magda::engine::RenderContext> liveContext() const override {
        return magda::engine::RenderContext{.sampleRate = kSampleRate, .maxBlockSize = 512};
    }

    magda::engine::TempoMap renderTempo() const override {
        return host::tempoMapAt(120.0, 4, 4);
    }

    magda::daw::audio::engine_adapter::ExternalPluginServices pluginServices() const override {
        return {};
    }

  private:
    magda::engine::DeviceKey key_;
    std::shared_ptr<RingingDevice> device_;
};

/// A compiled effect for the chain slot the host lends its device at.
magda::DeviceInfo chorus(magda::DeviceId id) {
    magda::DeviceInfo device;
    device.id = id;
    device.name = "Chorus";
    device.pluginId = magda::daw::audio::compiled::MagdaChorusCompiledPlugin::xmlTypeName;
    device.deviceType = magda::DeviceType::Effect;
    device.format = magda::PluginFormat::Internal;
    device.audioInputChannels = 2;
    device.audioOutputChannels = 2;
    return device;
}

class EngineOfflineRenderTest final : public juce::UnitTest {
  public:
    EngineOfflineRenderTest() : juce::UnitTest("Engine Offline Render Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testKnownSignalAtEachDepth(); });
        magda::test::runWithCleanJuceState([this] { testDitherOnTheFile(); });
        magda::test::runWithCleanJuceState([this] { testNormaliseAndLeadIn(); });
        magda::test::runWithCleanJuceState([this] { testBorrowedDeviceStartsClean(); });
        magda::test::runWithCleanJuceState([this] { testDeclaredTail(); });
        magda::test::runWithCleanJuceState([this] { testFreezeRendersUpToTheFader(); });
    }

  private:
    void testKnownSignalAtEachDepth() {
        beginTest("A render of a known signal comes back off disk as the samples that went in");

        const auto signal = sine(0.5f, 441.0);
        trackPlaying(writeSource("known", signal));

        for (const auto bitDepth : {32, 24, 16}) {
            const auto file =
                scratchDirectory().getChildFile("known_" + juce::String(bitDepth) + ".wav");
            const auto result =
                render(requestFor(file, bitDepth, magda::OfflineRenderDither::None));
            expect(result.success,
                   "The render at " + juce::String(bitDepth) + " bit succeeds: " + result.error);

            const auto stored = readBack(file);
            expectEquals(stored.getNumSamples(), kRangeSamples, "The file is the range");
            if (stored.getNumSamples() != kRangeSamples)
                continue;

            // Half a step of the target's grid, plus room for the fader's own curve at unity.
            const auto tolerance =
                (bitDepth == 32 ? 0.0f : 1.0f / static_cast<float>(1 << bitDepth)) + 1.0e-4f;

            auto worst = 0.0f;
            for (auto sample = 0; sample < kRangeSamples; ++sample)
                worst = std::max(worst, std::abs(stored.getSample(1, sample) - signal(sample)));

            expect(worst <= tolerance,
                   juce::String(bitDepth) + " bit is off by " + juce::String(worst) + " at worst");
        }
    }

    void testDitherOnTheFile() {
        beginTest("At 16 bit with dither the file's harmonics are gone under a floor");

        trackPlaying(writeSource("quiet", sine(kQuietAmplitude, kToneHz)));

        const auto plainFile = scratchDirectory().getChildFile("quiet_plain.wav");
        const auto ditheredFile = scratchDirectory().getChildFile("quiet_tpdf.wav");
        expect(render(requestFor(plainFile, 16, magda::OfflineRenderDither::None)).success,
               "The undithered render succeeds");
        expect(render(requestFor(ditheredFile, 16, magda::OfflineRenderDither::Tpdf)).success,
               "The dithered render succeeds");

        const auto plainStored = readBack(plainFile);
        const auto ditheredStored = readBack(ditheredFile);
        if (plainStored.getNumSamples() < 2 * kFftSize ||
            ditheredStored.getNumSamples() < 2 * kFftSize) {
            expect(false, "Both files hold the range");
            return;
        }

        const auto plain = spectrumOf(plainStored, kFftSize);
        const auto dithered = spectrumOf(ditheredStored, kFftSize);

        expect(harmonicEnergy(dithered) < harmonicEnergy(plain) * 0.1f,
               "Dither takes the staircase's harmonics away");
        expect(floorEnergy(dithered) > floorEnergy(plain), "And leaves a floor in their place");
        expect(energyAt(dithered, kToneHz) > energyAt(dithered, kToneHz * 3.0) * 10.0f,
               "The tone survives under it");
    }

    void testNormaliseAndLeadIn() {
        beginTest("A normalised render peaks where it was asked to, after silent lead-in");

        trackPlaying(writeSource("loud", sine(0.25f, 441.0)));

        const auto file = scratchDirectory().getChildFile("normalised.wav");
        auto request = requestFor(file, 32, magda::OfflineRenderDither::None);
        request.shouldNormalise = true;
        request.normaliseToLevelDb = -1.0f;
        request.leadInSeconds = 0.5;

        const auto result = render(request);
        expect(result.success, "The render succeeds: " + result.error);

        const auto leadIn = static_cast<int>(kSampleRate / 2.0);
        const auto stored = readBack(file);
        expectEquals(stored.getNumSamples(), leadIn + kRangeSamples,
                     "The file is the lead-in and the range");
        if (stored.getNumSamples() != leadIn + kRangeSamples)
            return;

        expectEquals(stored.getMagnitude(0, 0, leadIn), 0.0f, "The lead-in is silent");
        expectWithinAbsoluteError(stored.getMagnitude(0, leadIn, kRangeSamples),
                                  juce::Decibels::decibelsToGain(-1.0f), 1.0e-3f,
                                  "The render peaks at -1 dB");
    }

    void testBorrowedDeviceStartsClean() {
        beginTest("A borrowed device starts every render without the tail it was holding");

        auto& trackManager = magda::TrackManager::getInstance();
        auto* track = trackManager.getTrack(trackManager.createTrack("Effect"));
        expect(track != nullptr, "The track exists");
        if (track == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(chorus(1));

        // Live playback left it ringing.
        auto device = std::make_shared<RingingDevice>();
        device->ringing = true;

        LendingHost lender({magda::ChainSegment::Fx, magda::DeviceId{1}}, device);

        {
            auto session = host::createEngineOfflineRenderSession(lender, false);

            for (const auto pass : {1, 2}) {
                const auto file =
                    scratchDirectory().getChildFile("borrowed_" + juce::String(pass) + ".wav");
                auto task =
                    session->createTask(requestFor(file, 32, magda::OfflineRenderDither::None));
                expect(task != nullptr && task->run().success,
                       "Pass " + juce::String(pass) + " renders");

                const auto stored = readBack(file);
                expect(stored.getNumSamples() > 512, "Pass " + juce::String(pass) + " is on disk");
                if (stored.getNumSamples() <= 512)
                    return;

                expectEquals(stored.getSample(0, 0), 0.0f,
                             "Pass " + juce::String(pass) + " starts silent");
                expectEquals(stored.getSample(0, 512), 1.0f,
                             "The device rang within pass " + juce::String(pass));
            }
        }

        expect(!device->ringing, "Live playback resumes without the render's tail");
    }

    void testDeclaredTail() {
        beginTest("A render left to find its tail rings for as long as its longest device");

        auto& trackManager = magda::TrackManager::getInstance();
        auto* track = trackManager.getTrack(trackManager.createTrack("Effect"));
        expect(track != nullptr, "The track exists");
        if (track == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(chorus(1));

        auto device = std::make_shared<RingingDevice>();
        device->tail = 0.25;
        LendingHost lender({magda::ChainSegment::Fx, magda::DeviceId{1}}, device);

        const auto file = scratchDirectory().getChildFile("declared_tail.wav");
        auto request = requestFor(file, 32, magda::OfflineRenderDither::None);
        request.tailSeconds = std::nullopt;

        auto session = host::createEngineOfflineRenderSession(lender, false);
        auto task = session->createTask(request);
        expect(task != nullptr && task->run().success, "The render succeeds");

        expectEquals(readBack(file).getNumSamples(),
                     kRangeSamples + static_cast<int>(kSampleRate / 4),
                     "The file is the range and the device's tail");
    }

    void testFreezeRendersUpToTheFader() {
        beginTest(
            "A freeze renders its track and what feeds it up to the fader, and plays the file");

        const auto signal = sine(0.5f, 441.0);
        auto& trackManager = magda::TrackManager::getInstance();
        const auto frozenId = trackManager.createTrack("Frozen");
        const auto feedingId = trackManager.createTrack("Feeding");
        magda::ClipManager::getInstance().createAudioClipBeats(
            feedingId, 0.0, 4.0, writeSource("feeding", signal).getFullPathName());

        auto* frozen = trackManager.getTrack(frozenId);
        auto* feeding = trackManager.getTrack(feedingId);
        auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(frozen != nullptr && feeding != nullptr && master != nullptr, "The tracks exist");
        if (frozen == nullptr || feeding == nullptr || master == nullptr)
            return;

        feeding->audioOutputDevice = "track:" + juce::String(frozenId);
        frozen->volume = 0.25f;
        frozen->muted = true;
        master->volume = 0.1f;

        host::EngineHost engineHost;
        expect(engineHost.planFreeze(feedingId).request == nullptr,
               "A track routed into another track is refused");

        const auto freeze = engineHost.planFreeze(frozenId);
        expect(freeze.request != nullptr, "The frozen track has a render: " + freeze.refusal);
        if (freeze.request == nullptr)
            return;

        const auto file = freeze.request->destination;
        file.getParentDirectory().createDirectory();
        {
            auto session = engineHost.createOfflineRenderSession(false);
            auto task = session->createTask(*freeze.request);
            const auto result =
                task != nullptr ? task->run() : magda::OfflineRenderResult{false, "no task"};
            expect(result.success, "The freeze renders: " + result.error);
        }
        engineHost.adoptFreeze(*freeze.request);

        const auto stored = readBack(file);
        expectEquals(stored.getNumSamples(), kRangeSamples, "The file is the clip's length");
        if (stored.getNumSamples() != kRangeSamples)
            return;

        auto worst = 0.0f;
        for (auto sample = 0; sample < kRangeSamples; ++sample)
            worst = std::max(worst, std::abs(stored.getSample(0, sample) - signal(sample)));
        expect(worst <= 1.0e-4f,
               "The fader, the mute and the master are not in it: off by " + juce::String(worst));

        // Unnotified: the shared engine's Tracktion bridge would run its own modal freeze.
        frozen->frozen = true;
        const auto& tracks = trackManager.getTracks();
        const auto frozenTracks = host::frozenTracksWithFiles(tracks);
        expect(frozenTracks.size() == 1 && frozenTracks[0].trackId == frozenId,
               "The frozen track has its file");
        if (frozenTracks.size() != 1)
            return;

        const auto played = host::tracksAsPlayed(tracks, frozenTracks);
        expect(std::ranges::find(played, feedingId, &magda::TrackInfo::id) == played.end(),
               "The track feeding it is not played");

        const auto tempo = host::tempoMapAt(120.0, 4, 4);
        const auto snapshot = magda::engine::compileClipSnapshot(
            host::lanesAsPlayed(host::clipLanesFor(tracks), tracks, frozenTracks, tempo),
            host::clipSources(), tempo);
        const auto* lane = snapshot.find(frozenId);
        expect(lane != nullptr && lane->audio.size() == 1 && lane->audio[0].events.size() == 1 &&
                   lane->audio[0].events[0].filePath == file.getFullPathName().toStdString(),
               "The frozen track plays its file");
        expect(snapshot.find(feedingId) == nullptr, "The feeding track's clip is not played");

        file.deleteFile();
        host::unfreezeTracksWithoutFiles();
        expect(!trackManager.getTrack(frozenId)->frozen, "A frozen track without its file thaws");
    }
};

EngineOfflineRenderTest engineOfflineRenderTest;

}  // namespace
