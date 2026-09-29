#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

#include "magda/daw/audio/plugins/DeviceCatalogParameters.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"

// Relocating a sampler's file must not re-interpret the sample (#2170), and a restore of
// the document the model owns keeps what it does not name (#2377, #2379). The sampler is
// driven as the native engine builds it (#2556).

namespace {

namespace audio = magda::daw::audio;
using audio::MagdaSamplerPlugin;
using Catch::Approx;

juce::File scratchDir() {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_sampler_relocate");
    dir.createDirectory();
    return dir;
}

/// A sine of @p seconds; another length gives a different file at the same path.
bool writeTestWav(const juce::File& destination, double seconds = 1.0) {
    constexpr double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buffer(1, numSamples);
    const auto phaseInc =
        static_cast<float>(440.0 * juce::MathConstants<double>::twoPi / sampleRate);
    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, 0.5f * std::sin(phase));
        phase += phaseInc;
    }

    destination.deleteFile();
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::FileOutputStream>(destination);
    auto writer = wavFormat.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                        .withSampleRate(sampleRate)
                                                        .withNumChannels(1)
                                                        .withBitsPerSample(16));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
}

std::unique_ptr<audio::MagdaDevice> createSampler() {
    auto device = audio::createDetachedDevice(MagdaSamplerPlugin::xmlTypeName);
    REQUIRE(dynamic_cast<MagdaSamplerPlugin*>(device.get()) != nullptr);
    return device;
}

/// @p sampler holding @p sampleFile with deliberately non-default interpretation.
void loadInterpreted(MagdaSamplerPlugin& sampler, const juce::File& sampleFile) {
    sampler.loadSample(sampleFile);
    REQUIRE(sampler.getSampleFile().getFullPathName() == sampleFile.getFullPathName());

    sampler.setRootNote(48);
    sampler.setDisplayValue(MagdaSamplerPlugin::kSampleStart, 0.25f);
    sampler.setDisplayValue(MagdaSamplerPlugin::kSampleEnd, 0.75f);
    sampler.setDisplayValue(MagdaSamplerPlugin::kLoopStart, 0.3f);
    sampler.setDisplayValue(MagdaSamplerPlugin::kLoopEnd, 0.6f);
}

juce::ValueTree samplerState() {
    juce::ValueTree state{juce::Identifier("PLUGIN")};
    state.setProperty(juce::Identifier("type"), MagdaSamplerPlugin::xmlTypeName, nullptr);
    return state;
}

}  // namespace

TEST_CASE("Relocating a sample keeps root note and trim/loop markers",
          "[devices][sampler][relocate][2556]") {
    auto device = createSampler();
    auto& sampler = static_cast<MagdaSamplerPlugin&>(*device);

    const auto original = scratchDir().getNonexistentChildFile("relocate_source", ".wav");
    REQUIRE(writeTestWav(original));
    loadInterpreted(sampler, original);

    // Same audio, new home: what a media migration does.
    const auto moved = scratchDir().getNonexistentChildFile("relocate_dest", ".wav");
    REQUIRE(original.moveFileTo(moved));
    sampler.relocateSample(moved);

    CHECK(sampler.getSampleFile().getFullPathName() == moved.getFullPathName());
    CHECK(sampler.getRootNote() == 48);
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kSampleStart) == Approx(0.25f).margin(0.0001f));
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kSampleEnd) == Approx(0.75f).margin(0.0001f));
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kLoopStart) == Approx(0.3f).margin(0.0001f));
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kLoopEnd) == Approx(0.6f).margin(0.0001f));

    moved.deleteFile();
}

TEST_CASE("Choosing a genuinely new sample still resets interpretation",
          "[devices][sampler][relocate][2556]") {
    auto device = createSampler();
    auto& sampler = static_cast<MagdaSamplerPlugin&>(*device);

    const auto original = scratchDir().getNonexistentChildFile("reset_source", ".wav");
    REQUIRE(writeTestWav(original));
    loadInterpreted(sampler, original);

    const auto other = scratchDir().getNonexistentChildFile("reset_other", ".wav");
    REQUIRE(writeTestWav(other));
    sampler.loadSample(other);

    CHECK(sampler.getRootNote() == 60);
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kSampleStart) == Approx(0.0f).margin(0.0001f));
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kLoopStart) == Approx(0.0f).margin(0.0001f));

    original.deleteFile();
    other.deleteFile();
}

TEST_CASE("A restore that does not name loopEnabled switches looping off",
          "[devices][sampler][relocate][2556]") {
    // The document is the whole authored state, so an absent switch reads as off (#2377).
    auto device = createSampler();
    auto& sampler = static_cast<MagdaSamplerPlugin&>(*device);
    sampler.setLoopEnabled(true);

    auto state = samplerState();
    sampler.restoreState(state);
    CHECK_FALSE(sampler.loopEnabled());

    state.setProperty(MagdaSamplerPlugin::StateIDs::loopEnabled, true, nullptr);
    sampler.restoreState(state);
    CHECK(sampler.loopEnabled());
}

TEST_CASE("Restoring the source already loaded leaves the markers alone",
          "[devices][sampler][relocate][2556]") {
    // An authored-state edit arrives as a restore naming the sample already held; re-reading
    // the file would cut sounding voices and re-derive markers the model owns (#2379).
    const auto file = scratchDir().getNonexistentChildFile("same_source", ".wav");
    REQUIRE(writeTestWav(file));

    auto device = createSampler();
    auto& sampler = static_cast<MagdaSamplerPlugin&>(*device);
    sampler.loadSample(file);
    sampler.setDisplayValue(MagdaSamplerPlugin::kSampleEnd, 0.4f);
    sampler.setDisplayValue(MagdaSamplerPlugin::kLoopEnd, 0.0f);

    auto state = samplerState();
    state.setProperty(MagdaSamplerPlugin::StateIDs::source, file.getFullPathName(), nullptr);
    state.setProperty(MagdaSamplerPlugin::StateIDs::rootNote, 48, nullptr);
    state.setProperty(MagdaSamplerPlugin::StateIDs::loopEnabled, true, nullptr);
    sampler.restoreState(state);

    CHECK(sampler.loopEnabled());
    CHECK(sampler.getRootNote() == 48);
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kSampleEnd) == Approx(0.4f).margin(0.0001f));
    CHECK(sampler.displayValue(MagdaSamplerPlugin::kLoopEnd) == Approx(0.0f).margin(0.0001f));

    file.deleteFile();
}

TEST_CASE("A file replaced under the same name is read again",
          "[devices][sampler][relocate][2556]") {
    // The same-source shortcut must not swallow a re-render at the same path (#2379).
    const auto file = scratchDir().getNonexistentChildFile("replaced_source", ".wav");
    REQUIRE(writeTestWav(file, 1.0));

    auto device = createSampler();
    auto& sampler = static_cast<MagdaSamplerPlugin&>(*device);
    sampler.loadSample(file);
    REQUIRE(sampler.getSampleLengthSeconds() == Approx(1.0).margin(0.01));

    REQUIRE(writeTestWav(file, 2.0));
    auto state = samplerState();
    state.setProperty(MagdaSamplerPlugin::StateIDs::source, file.getFullPathName(), nullptr);
    sampler.restoreState(state);

    CHECK(sampler.getSampleLengthSeconds() == Approx(2.0).margin(0.01));

    file.deleteFile();
}
