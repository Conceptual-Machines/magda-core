#include <catch2/catch_test_macros.hpp>
#include <magda/sdk/sequencer/MonoStepSequencer.hpp>
#include <magda/sdk/sequencer/PolyStepSequencer.hpp>
#include <vector>

#define STEP_CORPUS_NS magda::sdk::sequencer
#include <support/StepCorpus.hpp>

/**
 * @file test_step_sequencer_identity.cpp
 * @brief The step sequencers' notes over a fixed corpus, pinned to what they played before they
 * moved to the SDK (#2935).
 */

namespace {

using magda::stepcorpus::Entry;

const std::vector<Entry>& pinned() {
    static const std::vector<Entry> pins{
        {"mono.basic", 37, 0x91c90c1ef105e7afULL, 0xf3a7b96b1f104140ULL},
        {"mono.swing", 40, 0xb44f831e24236cc3ULL, 0xa1f1cfbe4b8fbd08ULL},
        {"mono.eighth.short", 21, 0x10ea31206d7141f6ULL, 0xdd439bbbd841fa3eULL},
        {"mono.triplet.reverse", 57, 0xecc333ff80361afcULL, 0x107c4f9744e1a244ULL},
        {"mono.dotted.pingpong", 12, 0x70d0c90ad477741cULL, 0x4bb10f45ea147284ULL},
        {"mono.random", 42, 0x9ab75cffc0c511b7ULL, 0xe85449cfcd5dcee4ULL},
        {"mono.ramp", 42, 0x758f248f37fea731ULL, 0x9b3a6373483ada85ULL},
        {"mono.ramp.hard", 41, 0xf906ddd0a8d68729ULL, 0x622fcaf5e671d288ULL},
        {"mono.quantize", 39, 0x8eda17499730a529ULL, 0x4bcd5a602e575901ULL},
        {"mono.long.bigblock", 104, 0x4b89ba89bd7bd382ULL, 0x508bb947ea0e6945ULL},
        {"mono.accents", 10, 0xcfb968e7ac831f81ULL, 0xe680e5aa779c4611ULL},
        {"mono.midstart", 39, 0x00f67c1745f03439ULL, 0x4f755bac2d3020b4ULL},
        {"mono.startstop", 30, 0x419687d9e09b0f2eULL, 0x5205ad3471b5eb15ULL},
        {"mono.loop", 60, 0xd65a5ca27b10c002ULL, 0x128662b6fe305faeULL},
        {"mono.rateswitch", 29, 0xd7b62c596b6aad97ULL, 0x238f154b26e8f72fULL},
        {"mono.ramp.startstop", 56, 0xa4fdd61f80d4bc46ULL, 0x648c90b8ee345c4fULL},
        {"poly.basic", 74, 0x8b00cc9896decf78ULL, 0xda23828afb419932ULL},
        {"poly.swing", 74, 0x8b00cc9896decf78ULL, 0x4a991577074d5f10ULL},
        {"poly.seeded", 71, 0x31204454f6b24607ULL, 0xb585c6a4b6e6d00cULL},
        {"poly.eighth.short", 37, 0xf677b61d09d7c689ULL, 0xc051bf0273c532dcULL},
        {"poly.triplet.reverse", 108, 0xbaf9a09eaa448d7eULL, 0x7d11c62cda1c05b7ULL},
        {"poly.pingpong", 29, 0x7fcecaae0dec3d96ULL, 0xe0476a714cef7094ULL},
        {"poly.random", 75, 0xd604e2ccc6203be1ULL, 0x1e11446ed2665218ULL},
        {"poly.ramp", 70, 0x2891fde06359e8e7ULL, 0xbb97b8f22bfec87bULL},
        {"poly.quantize", 71, 0xde8dda4eed57b90fULL, 0x6917745bfb45ab52ULL},
        {"poly.long.bigblock", 219, 0x88961f4a31d0c7e3ULL, 0x4925d49eec901d7eULL},
        {"poly.startstop", 53, 0x4d9c5f2d659cf1cdULL, 0x6e75e30e9ce85a01ULL},
        {"poly.loop", 109, 0x27063b041ecdda14ULL, 0xf4b7898b121a7293ULL},
        {"poly.rateswitch", 54, 0xc87c7fd78204236dULL, 0x3db118824d5f734aULL},
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
