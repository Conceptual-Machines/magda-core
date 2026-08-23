#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/ParameterUtils.hpp"
#include "exec/EngineDevice.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "param/ParamBlock.hpp"

/**
 * @file test_engine_device_layer.cpp
 * @brief The devices MAGDA ships, running under the native engine (#2174).
 *
 * The corpus knew one device until this slice, and it was written for the
 * corpus: a gain implemented twice, once in each leg. What that could prove was
 * that the two legs agree about a gain. What it could not is that either engine
 * runs a device somebody would find in a project, which is what a real project
 * is made of.
 *
 * These are the engine's half, on its own. They ask the two catalogs the app
 * asks, they build what those catalogs return, and they run it. What the two
 * hosts do with the same device is the corpus's question and is asked where
 * both legs are (magda_juce_tests).
 */

namespace {

namespace adapter = magda::daw::audio::engine_adapter;

/// A MIDI-driven device that needs nothing set to make a sound, which is what
/// lets a test assert on its output rather than on its parameters first.
constexpr const char* kInstrumentId = "magda_kick";

magda::engine::RenderContext contextFor(int blockSize = 512) {
    return {.sampleRate = 48000.0, .maxBlockSize = blockSize, .numChannels = 2};
}

/// One block of silence, and the block description that goes with it.
struct Block {
    explicit Block(const magda::engine::RenderContext& context)
        : buffer(context.numChannels, context.maxBlockSize) {
        buffer.clear();
        info.numSamples = context.maxBlockSize;
        info.playing = true;
        info.endSeconds = context.maxBlockSize / context.sampleRate;
        info.endBeat = info.endSeconds * 2.0;
    }

    magda::engine::DeviceBlock deviceBlock() {
        magda::engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(buffer);
        block.block = info;
        block.midiIn = &midi;
        return block;
    }

    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    magda::engine::BlockInfo info;
};

float peakOf(const juce::AudioBuffer<float>& buffer) {
    return buffer.getMagnitude(0, buffer.getNumSamples());
}

}  // namespace

TEST_CASE("the engine can run every device that has moved to the SDK", "[engine][devices][2174]") {
    // The rule rather than a list of names, so a device migrating to the SDK
    // does not have to remember to come back here: what the factory answers is
    // exactly what the catalog carries, in both directions. A device with no
    // createDevice is one the engine cannot run, and saying so is the point --
    // the alternative is a stand-in that passes signal while the incumbent runs
    // the real thing.
    int sdkDevices = 0;

    for (const auto* spec : magda::daw::audio::getAllInternalPluginSpecs()) {
        REQUIRE(spec != nullptr);
        REQUIRE(spec->pluginId != nullptr);

        const auto expected = spec->createDevice != nullptr;
        INFO("internal device " << spec->pluginId);
        CHECK(adapter::canCreateEngineDevice(spec->pluginId) == expected);

        if (expected) {
            ++sdkDevices;
            magda::DeviceInfo model;
            model.pluginId = spec->pluginId;
            CHECK(adapter::createEngineDevice(model) != nullptr);
        }
    }

    for (const auto* spec : magda::daw::audio::compiled::getAllCompiledPluginSpecs()) {
        REQUIRE(spec != nullptr);
        REQUIRE(spec->pluginId != nullptr);

        // A compiled device is not in the internal registry, only its parameter
        // aliases are, so the factory has to ask both catalogs. Asserted here
        // because a factory that asked one would answer no for every compiled
        // device, which is most of the fleet.
        const auto expected = spec->createDevice != nullptr;
        INFO("compiled device " << spec->pluginId);
        CHECK(adapter::canCreateEngineDevice(spec->pluginId) == expected);

        if (expected) {
            ++sdkDevices;
            magda::DeviceInfo model;
            model.pluginId = spec->pluginId;
            CHECK(adapter::createEngineDevice(model) != nullptr);
        }
    }

    // The rule above is satisfied by a build where nothing has moved to the SDK
    // at all, which is a green test over an empty set.
    CHECK(sdkDevices > 0);
}

TEST_CASE("a device the catalogs do not have is refused rather than stood in for",
          "[engine][devices][2174]") {
    CHECK_FALSE(adapter::canCreateEngineDevice("not_a_device"));

    magda::DeviceInfo model;
    model.pluginId = "not_a_device";
    CHECK(adapter::createEngineDevice(model) == nullptr);

    // An external plugin reaches the factory as a pluginId nothing registered,
    // and gets the same answer for the same reason: nothing hosts VST3 in the
    // engine yet (#1893).
    model.pluginId = "VST3-1234567890";
    CHECK(adapter::createEngineDevice(model) == nullptr);
}

TEST_CASE("a shipped instrument renders through the engine's device op",
          "[engine][devices][2174]") {
    magda::DeviceInfo model;
    model.pluginId = kInstrumentId;

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    const auto context = contextFor();
    device->prepare(context);

    Block silent(context);
    auto silentBlock = silent.deviceBlock();
    device->process(silentBlock);
    CHECK(peakOf(silent.buffer) == 0.0f);

    Block played(context);
    played.midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    auto playedBlock = played.deviceBlock();
    device->process(playedBlock);

    // What is asserted is that the note reached the DSP and the DSP wrote into
    // the executor's own buffer, which is the whole of what the adapter is for.
    // What it sounds like is the device's business and is pinned by its own
    // tests.
    CHECK(peakOf(played.buffer) > 0.0f);
}

TEST_CASE("the plan's resolved values reach the device's parameters", "[engine][devices][2174]") {
    magda::DeviceInfo model;
    model.pluginId = kInstrumentId;

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(device.get());
    REQUIRE(hosted != nullptr);
    REQUIRE(hosted->device().parameterCount() > 0);

    const auto info = hosted->device().parameterInfo(0);
    const auto context = contextFor();
    device->prepare(context);

    // The table the executor publishes, built by hand: one parameter, holding
    // one value for the block, in the parameter's own units and read through
    // its own scale. Which is what a device gets from the value layer, and the
    // adapter's job is to turn it back into the normalised position the SDK
    // takes without either end having an opinion about the scale.
    const auto target = info.minValue + 0.75f * (info.maxValue - info.minValue);

    magda::engine::ResolvedParams table;
    table.prepare(1);
    table.beginBlock(context.maxBlockSize);
    table.setDomain(0, magda::ParameterUtils::domainOf(info));

    const auto position = magda::ParameterUtils::realToNormalized(target, info);
    auto* slot = table.slotFor(0);
    REQUIRE(slot != nullptr);
    slot[0] = {.startSample = 0, .startValue = position, .endValue = position};
    table.setSegmentCount(0, 1);

    Block block(context);
    auto deviceBlock = block.deviceBlock();
    deviceBlock.params = table.device(0, 1);
    device->process(deviceBlock);

    CHECK(hosted->device().parameterValue(0) == Catch::Approx(position).margin(1.0e-5));
}
