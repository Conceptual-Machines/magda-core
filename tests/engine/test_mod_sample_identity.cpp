#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/ModInfo.hpp"
#include "param/ModBridge.hpp"
#include "transport/TempoMap.hpp"

#define MOD_CORPUS_NS magda::sdk
#define MOD_CORPUS_TRIGGER magda::sdk::LFOTriggerMode
#define MOD_CORPUS_RATE magda::sdk::ModRateType
#include <support/ModCorpus.hpp>

/**
 * @file test_mod_sample_identity.cpp
 * @brief The modulators' output through the engine's block bridge, pinned to what they produced
 * before moving to the SDK (#2932).
 */

namespace {

class Hasher {
  public:
    void f(float v) {
        bytes(&v, sizeof v);
    }

    std::uint64_t value() const {
        return h_;
    }

  private:
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            h_ ^= p[i];
            h_ *= 1099511628211ULL;
        }
    }

    std::uint64_t h_ = 14695981039346656037ULL;
};

/// Synced modulators marched across a tempo and signature change, the way the transport hands
/// blocks over.
std::uint64_t tempoMapHash(double originBeat) {
    using namespace magda::engine;
    const TempoMap tempo({{.startBeat = 0.0, .bpm = 120.0},
                          {.startBeat = 6.0, .bpm = 120.0},
                          {.startBeat = 6.0, .bpm = 75.0},
                          {.startBeat = 14.0, .bpm = 100.0}},
                         {{.startBeat = 0.0, .numerator = 4, .denominator = 4},
                          {.startBeat = 8.0, .numerator = 3, .denominator = 4},
                          {.startBeat = 14.0, .numerator = 6, .denominator = 8}});
    Hasher h;
    const double sampleRate = 48000.0;
    for (int bs : {64, 480, 4800, 24000})
        for (int rate : {1, 3, 5, 7, 10, 13, 16, 20})
            for (auto sync : {ModSync::Free, ModSync::Transport}) {
                LfoSettings ls;
                ls.sync = sync;
                ls.tempoSync = true;
                ls.rate.rateType = rate;
                LfoState lfo;
                AdsrSettings as;
                as.sync = sync;
                as.tempoSync = true;
                as.rateType = rate;
                AdsrState adsr;
                RandomSettings rs;
                rs.sync = sync;
                rs.tempoSync = true;
                rs.rate.rateType = rate;
                RandomState random;
                seedRandom(random, 7);

                double seconds = tempo.beatToTime(originBeat);
                const double originSeconds = seconds;
                for (int i = 0; i < 60; ++i) {
                    BlockInfo block;
                    block.numSamples = bs;
                    block.sampleRate = sampleRate;
                    block.playing = true;
                    const double next = seconds + bs / sampleRate;
                    block.seconds = {seconds - originSeconds, next - originSeconds};
                    block.beats = {tempo.timeToBeat(seconds) - originBeat,
                                   tempo.timeToBeat(next) - originBeat};
                    block.tempo = &tempo;
                    block.materialOrigin.beat = originBeat;
                    block.materialOrigin.seconds = originSeconds;
                    seconds = next;

                    const auto timing = modTimingFor(block, sampleRate);
                    const auto reduced = modBlockFor(block, timing);
                    h.f(advanceLfo(lfo, ls, {}, reduced, timing));
                    h.f(advanceAdsr(adsr, as, reduced, timing));
                    h.f(advanceRandom(random, rs, reduced, timing));
                }
            }
    return h.value();
}

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

// Exact pins hold where they were captured; float results differ in the last
// bit across compilers and platforms.
#if defined(__APPLE__) && defined(__aarch64__)
TEST_CASE("The modulators reproduce their output from before the move", "[engine][mod][identity]") {
    const std::vector<magda::modcorpus::Entry> pinned{
        {"lfo.free.shapes", 0xbcc218008662c36aULL},
        {"lfo.custom.loop", 0x2b54e548bae2de25ULL},
        {"lfo.sync.free", 0x1f9e536a9e954495ULL},
        {"lfo.sync.transport", 0x1619738fa3a549b3ULL},
        {"lfo.note.trigger", 0x6ae7ad52eb1bf8d5ULL},
        {"adsr.free", 0x4b482912e6c9c032ULL},
        {"adsr.note.retrigger", 0xc7160c486e54a394ULL},
        {"adsr.sync", 0x8bc3f30c0a0dcdfeULL},
        {"adsr.segment", 0x19656e9e4771a003ULL},
        {"random.free", 0x592203b5acdc8350ULL},
        {"random.sync.free", 0x351110d2f69d3c63ULL},
        {"random.sync.transport", 0xd0bff8d654d6a20cULL},
        {"random.note.restart", 0x5ceeb779fcc17ddbULL},
        {"follower.all", 0x60cbfa401f107e5dULL},
    };

    const auto actual = magda::modcorpus::runCorpus<ReducedBlock>();
    REQUIRE(actual.size() == pinned.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO(actual[i].name);
        CHECK(actual[i].name == pinned[i].name);
        CHECK(actual[i].hash == pinned[i].hash);
    }

    CHECK(tempoMapHash(0.0) == 0x0a9a31d8cc574733ULL);
    CHECK(tempoMapHash(1.0) == 0xac38712a96d11b37ULL);
}
#endif
