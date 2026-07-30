// Covers the runtime Faust execution backend (issue #1382): the factory that
// FaustPlugin / FaustInstrumentPlugin compile user- and AI-authored DSP with.
//
// The rest of the Faust tests cover metadata and param plumbing, which is
// backend-agnostic. These exercise the part that actually changes when
// MAGDA_FAUST_BACKEND changes -- compiling source and running the compiled DSP
// -- so swapping the backend can't silently stop working.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "faust/dsp/dsp.h"
#include "magda/daw/audio/plugins/FaustBackend.hpp"

namespace {

// Phase markers for the MSVC 14.51 heap-corruption hunt. The corruption is
// detected mid-test by the debug heap, which names no code: these narrow it to
// compile / instantiate / init / compute. Same env gate as the heap validation
// in windows_no_dialogs.cpp, silent otherwise.
void phase(const char* message) {
    static const bool enabled = []() {
        const char* value = std::getenv("MAGDA_TEST_HEAP_CHECK");
        return value != nullptr && value[0] == '1';
    }();
    if (enabled) {
        std::fprintf(stderr, "faust-backend phase: %s\n", message);
        std::fflush(stderr);
    }
}

// Doubles its input, so a compiled instance is trivially verifiable.
constexpr const char* kGainDsp = R"(
process = _ , _ : *(2.0) , *(2.0);
)";

constexpr const char* kBrokenDsp = R"(
process = this is not valid faust ;
)";

magda::faust::Factory* compile(const std::string& source, std::string& errorOut) {
    // No -I: the DSP under test imports nothing, so the Faust standard library
    // (which lives beside the app bundle at runtime) isn't needed here.
    return magda::faust::createFactoryFromString("magda_test", source, 0, nullptr, errorOut);
}

}  // namespace

TEST_CASE("Faust backend compiles DSP source into a working instance", "[faust][backend]") {
    std::string error;
    phase("compile gain dsp");
    auto* factory = compile(kGainDsp, error);
    phase("compile returned");
    REQUIRE(factory != nullptr);
    REQUIRE(error.empty());

    phase("createDSPInstance");
    ::dsp* instance = factory->createDSPInstance();
    phase("instance created");
    REQUIRE(instance != nullptr);

    CHECK(instance->getNumInputs() == 2);
    CHECK(instance->getNumOutputs() == 2);

    phase("init");
    instance->init(44100);
    phase("init done");

    constexpr int kFrames = 64;
    std::vector<FAUSTFLOAT> inLeft(kFrames, 0.25f), inRight(kFrames, -0.5f);
    std::vector<FAUSTFLOAT> outLeft(kFrames, 0.0f), outRight(kFrames, 0.0f);
    FAUSTFLOAT* ins[] = {inLeft.data(), inRight.data()};
    FAUSTFLOAT* outs[] = {outLeft.data(), outRight.data()};

    phase("compute");
    instance->compute(kFrames, ins, outs);
    phase("compute done");

    using Catch::Matchers::WithinAbs;
    for (int i = 0; i < kFrames; ++i) {
        CHECK_THAT(outLeft[i], WithinAbs(0.5f, 1e-6));
        CHECK_THAT(outRight[i], WithinAbs(-1.0f, 1e-6));
    }

    phase("teardown");
    delete instance;
    magda::faust::deleteFactory(factory);
    phase("teardown done");
}

TEST_CASE("Faust backend reports a compile error instead of crashing", "[faust][backend]") {
    std::string error;
    phase("compile broken dsp");
    auto* factory = compile(kBrokenDsp, error);
    phase("broken compile returned");

    CHECK(factory == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("Faust backend factories carry a SHA key for caching", "[faust][backend]") {
    // getSHAKey() is what keys the compiled-DSP cache for the saved effect
    // library, so it has to be non-empty and stable for identical source.
    std::string errorA, errorB;
    auto* a = compile(kGainDsp, errorA);
    auto* b = compile(kGainDsp, errorB);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    const auto keyA = a->getSHAKey();
    CHECK_FALSE(keyA.empty());
    CHECK(keyA == b->getSHAKey());

    magda::faust::deleteFactory(a);
    magda::faust::deleteFactory(b);
}
