#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_DeltaSolo.h"

using Catch::Approx;

TEST_CASE("Delta solo subtracts the dry signal", "[delta_solo][audio]") {
    juce::AudioBuffer<float> wet(2, 4);
    juce::AudioBuffer<float> dry(2, 4);

    for (int channel = 0; channel < 2; ++channel) {
        for (int sample = 0; sample < 4; ++sample) {
            dry.setSample(channel, sample, static_cast<float>(sample + 1));
            wet.setSample(channel, sample, 3.0f * dry.getSample(channel, sample));
        }
    }

    tracktion::engine::plugin_node_detail::applyDeltaSolo(
        tracktion::graph::toBufferView(wet), tracktion::graph::toBufferView(dry), true);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            CHECK(wet.getSample(channel, sample) == Approx(2.0f * dry.getSample(channel, sample)));
}

TEST_CASE("Delta solo leaves output unchanged while disabled", "[delta_solo][audio]") {
    juce::AudioBuffer<float> wet(1, 2);
    juce::AudioBuffer<float> dry(1, 2);
    wet.setSample(0, 0, 0.75f);
    wet.setSample(0, 1, -0.25f);
    dry.clear();
    dry.addSample(0, 0, 1.0f);

    tracktion::engine::plugin_node_detail::applyDeltaSolo(
        tracktion::graph::toBufferView(wet), tracktion::graph::toBufferView(dry), false);

    CHECK(wet.getSample(0, 0) == Approx(0.75f));
    CHECK(wet.getSample(0, 1) == Approx(-0.25f));
}

TEST_CASE("Delta solo aligns dry input to plugin latency", "[delta_solo][audio][latency]") {
    constexpr int latencySamples = 2;
    constexpr int blockSize = 4;

    tracktion::graph::LatencyProcessor dryDelay;
    dryDelay.setLatencyNumSamples(latencySamples);
    dryDelay.prepareToPlay(48000.0, blockSize, 1);

    juce::AudioBuffer<float> input(1, blockSize);
    input.clear();
    input.setSample(0, 0, 1.0f);

    juce::AudioBuffer<float> alignedDry(1, blockSize);
    dryDelay.writeAudio(tracktion::graph::toBufferView(input));
    dryDelay.readAudioOverwriting(tracktion::graph::toBufferView(alignedDry));

    juce::AudioBuffer<float> wet(1, blockSize);
    wet.clear();
    wet.setSample(0, latencySamples, 2.0f);
    tracktion::engine::plugin_node_detail::applyDeltaSolo(
        tracktion::graph::toBufferView(wet), tracktion::graph::toBufferView(alignedDry), true);

    CHECK(wet.getSample(0, 0) == Approx(0.0f));
    CHECK(wet.getSample(0, 1) == Approx(0.0f));
    CHECK(wet.getSample(0, 2) == Approx(1.0f));
    CHECK(wet.getSample(0, 3) == Approx(0.0f));
}

TEST_CASE("Delta latency history remains aligned while monitoring is off",
          "[delta_solo][audio][latency]") {
    constexpr int latencySamples = 2;
    constexpr int blockSize = 4;

    tracktion::graph::LatencyProcessor dryDelay;
    dryDelay.setLatencyNumSamples(latencySamples);
    dryDelay.prepareToPlay(48000.0, blockSize, 1);

    juce::AudioBuffer<float> disabledBlock(1, blockSize);
    disabledBlock.clear();
    disabledBlock.setSample(0, 3, 1.0f);
    dryDelay.writeAudio(tracktion::graph::toBufferView(disabledBlock));
    dryDelay.clearAudio(blockSize);

    juce::AudioBuffer<float> enabledBlock(1, blockSize);
    enabledBlock.clear();
    dryDelay.writeAudio(tracktion::graph::toBufferView(enabledBlock));

    juce::AudioBuffer<float> alignedDry(1, blockSize);
    dryDelay.readAudioOverwriting(tracktion::graph::toBufferView(alignedDry));

    juce::AudioBuffer<float> wet(1, blockSize);
    wet.clear();
    wet.setSample(0, 1, 2.0f);
    tracktion::engine::plugin_node_detail::applyDeltaSolo(
        tracktion::graph::toBufferView(wet), tracktion::graph::toBufferView(alignedDry), true);

    CHECK(wet.getSample(0, 0) == Approx(0.0f));
    CHECK(wet.getSample(0, 1) == Approx(1.0f));
    CHECK(wet.getSample(0, 2) == Approx(0.0f));
    CHECK(wet.getSample(0, 3) == Approx(0.0f));
}
