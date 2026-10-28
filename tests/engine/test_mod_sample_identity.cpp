// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/ModInfo.hpp"
#include "param/ModBridge.hpp"

#define MOD_CORPUS_NS magda::sdk
#define MOD_CORPUS_TRIGGER magda::sdk::LFOTriggerMode
#define MOD_CORPUS_RATE magda::sdk::ModRateType
#include <support/ModCorpus.hpp>

/**
 * @file test_mod_sample_identity.cpp
 * @brief The engine's block bridge hands the modulators the same block the SDK reads (#2932).
 */

namespace {

struct EngineBlock {
    using Block = magda::engine::BlockInfo;

    static Block make(const magda::modcorpus::Spec& spec, const magda::engine::ModTiming& timing) {
        Block block;
        block.numSamples = spec.numSamples;
        block.playing = spec.playing;
        block.beats = {spec.beatStart, spec.beatStart + spec.beatLen};
        block.seconds = {spec.secStart, spec.secStart + spec.numSamples / timing.sampleRate};
        return block;
    }
};

/// The reduction the SDK tests apply to a block with no tempo map.
struct ReducedBlock {
    using Block = magda::sdk::ModBlock;

    static Block make(const magda::modcorpus::Spec& spec, const magda::engine::ModTiming& timing) {
        return magda::engine::modBlockFor(EngineBlock::make(spec, timing), timing);
    }
};

/// A bridge that reduced a block differently would diverge here on any platform.
struct DirectBlock {
    using Block = magda::sdk::ModBlock;

    static Block make(const magda::modcorpus::Spec& spec, const magda::engine::ModTiming& timing) {
        const double beatLength = (spec.beatStart + spec.beatLen) - spec.beatStart;
        const double barBeats = magda::sdk::barBeatsOf(timing.numerator, timing.denominator);

        Block block;
        block.numSamples = spec.numSamples;
        block.playing = spec.playing;
        block.secondsStart = spec.secStart;
        block.barPosition =
            spec.beatStart / std::max(4.0 * timing.numerator / timing.denominator, 1.0e-6);
        block.barsElapsed = spec.playing && beatLength > 0.0
                                ? beatLength / std::max(barBeats, 1.0e-6)
                                : std::max(spec.numSamples, 0) / std::max(timing.sampleRate, 1.0) *
                                      timing.bpm / 60.0 / std::max(barBeats, 1.0e-6);
        return block;
    }
};

}  // namespace

TEST_CASE("A block with no tempo map reduces to the SDK's own reading of it",
          "[engine][mod][identity]") {
    const auto bridged = magda::modcorpus::runCorpus<ReducedBlock>();
    const auto direct = magda::modcorpus::runCorpus<DirectBlock>();

    REQUIRE(bridged.size() == direct.size());
    for (std::size_t i = 0; i < bridged.size(); ++i) {
        INFO(bridged[i].name);
        CHECK(bridged[i].hash == direct[i].hash);
    }
}
