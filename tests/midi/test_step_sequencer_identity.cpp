// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <magda/sdk/sequencer/MonoStepSequencer.hpp>
#include <magda/sdk/sequencer/PolyStepSequencer.hpp>
#include <vector>

#define STEP_CORPUS_NS magda::sdk::sequencer
#include <support/StepCorpus.hpp>

/**
 * @file test_step_sequencer_identity.cpp
 * @brief The step sequencers' notes over a fixed corpus, pinned to what they played before they
 * moved to the SDK (#2935), and re-pinned when the clock went stateless (magda-sdk#22).
 */

namespace {

using magda::stepcorpus::Entry;

const std::vector<Entry>& pinned() {
    static const std::vector<Entry> pins{
        {"mono.basic", 37, 0x91c90c1ef105e7afULL, 0xf3a7b96b1f104140ULL},
        {"mono.swing", 40, 0xb44f831e24236cc3ULL, 0xa1f1cfbe4b8fbd08ULL},
        {"mono.eighth.short", 21, 0x10ea31206d7141f6ULL, 0xdd439bbbd841fa3eULL},
        {"mono.triplet.reverse", 57, 0xecc333ff80361afcULL, 0x07361d2b47848a23ULL},
        {"mono.dotted.pingpong", 12, 0x70d0c90ad477741cULL, 0x4bb10f45ea147284ULL},
        {"mono.random", 36, 0x486e0e07e34d1327ULL, 0x760b103952ef2654ULL},
        {"mono.rush", 39, 0x4adef5863893c12fULL, 0x24f13f7796246bd9ULL},
        {"mono.rushdrag", 40, 0xb44f831e24236cc3ULL, 0x87d1e0ed729ba55cULL},
        {"mono.backwards", 27, 0x8d9fb2a93e5ff2d4ULL, 0xfea65baaf89bdcd6ULL},
        {"mono.thereandback", 73, 0xbe5b6a756946191fULL, 0x1692116689d5fa8bULL},
        {"mono.quantize", 39, 0x4adef5863893c12fULL, 0x3703c5c1eba3740aULL},
        {"mono.long.bigblock", 104, 0x4b89ba89bd7bd382ULL, 0x508bb947ea0e6945ULL},
        {"mono.accents", 10, 0xcfb968e7ac831f81ULL, 0xe680e5aa779c4611ULL},
        {"mono.midstart", 39, 0x0954058ef1a37f39ULL, 0xaaefc0c9ca208960ULL},
        {"mono.startstop", 29, 0x32f654feb3b867fbULL, 0x2ca40d42e9ff267fULL},
        {"mono.loop", 58, 0x3bce62d5effe368bULL, 0x248dd1da2325892dULL},
        {"mono.rateswitch", 28, 0x10eea752acfe0280ULL, 0x2d2276ad2d4f0131ULL},
        {"mono.hold.startstop", 53, 0x2f4427ba099d8b74ULL, 0x77ba0baa1a8b0acbULL},
        {"poly.basic", 74, 0x8b00cc9896decf78ULL, 0xda23828afb419932ULL},
        {"poly.swing", 74, 0x8b00cc9896decf78ULL, 0x4a991577074d5f10ULL},
        {"poly.seeded", 71, 0x31204454f6b24607ULL, 0xb585c6a4b6e6d00cULL},
        {"poly.eighth.short", 37, 0xf677b61d09d7c689ULL, 0xc051bf0273c532dcULL},
        {"poly.triplet.reverse", 108, 0xbaf9a09eaa448d7eULL, 0x7d11c62cda1c05b7ULL},
        {"poly.pingpong", 29, 0x7fcecaae0dec3d96ULL, 0xe0476a714cef7094ULL},
        {"poly.random", 78, 0x6364276c9f6baf3dULL, 0x74982e7fe53762e1ULL},
        {"poly.rush", 68, 0x015a2449756145a6ULL, 0x907276bdf1c124caULL},
        {"poly.thereandback", 143, 0xf3a2b8dbb4ff70e1ULL, 0xcad1cea02c8d7fc3ULL},
        {"poly.quantize", 71, 0xa44b7dde350032d2ULL, 0x958e9abf07eea8beULL},
        {"poly.long.bigblock", 219, 0x88961f4a31d0c7e3ULL, 0x4925d49eec901d7eULL},
        {"poly.startstop", 48, 0x8188681bd55306d0ULL, 0x19fc09aa052edf78ULL},
        {"poly.loop", 112, 0xb5a37492c8c29f03ULL, 0x8dc6ebe0c32ed173ULL},
        {"poly.rateswitch", 57, 0x0ba965131f7296acULL, 0x3bd5e23f9b057c86ULL},
    };
    return pins;
}

}  // namespace

TEST_CASE("The corpus is deterministic", "[sequencer][identity]") {
    CHECK(magda::stepcorpus::runCorpus() == magda::stepcorpus::runCorpus());
}

TEST_CASE("Saved patterns play the same notes and velocities as before the move",
          "[sequencer][identity]") {
    const auto actual = magda::stepcorpus::runCorpus();
    REQUIRE(actual.size() == pinned().size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO(actual[i].name);
        CHECK(actual[i].name == pinned()[i].name);
        CHECK(actual[i].count == pinned()[i].count);
        CHECK(actual[i].sequenceHash == pinned()[i].sequenceHash);
    }
}

// Sample offsets and lengths come from beat arithmetic in double, which can differ in the last
// bit across compilers; the pins hold where they were captured.
#if defined(__APPLE__) && defined(__aarch64__)
TEST_CASE("Saved patterns land on the same samples with the same lengths as before the move",
          "[sequencer][identity]") {
    const auto actual = magda::stepcorpus::runCorpus();
    REQUIRE(actual.size() == pinned().size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO(actual[i].name);
        CHECK(actual[i].fullHash == pinned()[i].fullHash);
    }
}
#endif
