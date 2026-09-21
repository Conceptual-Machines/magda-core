#include <faust/gui/MapUI.h>
#include <juce_core/juce_core.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <string>

#include "magda/daw/audio/plugins/FaustBackend.hpp"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 64;
constexpr int kWarmupBlocks = 75;  // 100 ms with the gate down or silent input

struct CompiledDsp {
    magda::faust::Factory* factory = nullptr;

    ~CompiledDsp() {
        if (factory != nullptr)
            magda::faust::deleteFactory(factory);
    }
};

std::unique_ptr<CompiledDsp> compileDsp(const char* relativePath, std::string& error) {
    const auto source =
        juce::File(MAGDA_FAUST_DSP_DIR).getChildFile(relativePath).loadFileAsString();
    if (source.isEmpty()) {
        error = std::string("Could not read ") + relativePath;
        return nullptr;
    }

    const std::string standardLibraries = MAGDA_FAUST_LIBRARIES_DIR;
    const std::string magdaLibraries = std::string(MAGDA_FAUST_DSP_DIR) + "/lib";
    const char* argv[] = {"-I", standardLibraries.c_str(), "-I", magdaLibraries.c_str()};

    auto compiled = std::make_unique<CompiledDsp>();
    compiled->factory =
        magda::faust::createFactoryFromString(relativePath, source.toStdString(), 4, argv, error);
    if (compiled->factory == nullptr)
        return nullptr;
    return compiled;
}

// Compare the same onset directly after a clear and after silent samples have
// allowed the controls to settle. The signal path starts from silence in both
// cases, so only the parameter smoothers should differ.
double onsetEnergy(dsp& processor, bool warmup, bool instrument) {
    processor.init(kSampleRate);
    MapUI ui;
    processor.buildUserInterface(&ui);

    auto* gate = instrument ? ui.getParamZone("gate") : nullptr;
    if (instrument && gate == nullptr)
        return -1.0;
    if (gate != nullptr)
        *gate = 0;

    std::array<FAUSTFLOAT, kBlockSize> inLeft{};
    std::array<FAUSTFLOAT, kBlockSize> inRight{};
    std::array<FAUSTFLOAT, kBlockSize> outLeft{};
    std::array<FAUSTFLOAT, kBlockSize> outRight{};
    FAUSTFLOAT* inputs[] = {inLeft.data(), inRight.data()};
    FAUSTFLOAT* outputs[] = {outLeft.data(), outRight.data()};

    if (warmup) {
        for (int block = 0; block < kWarmupBlocks; ++block)
            processor.compute(kBlockSize, instrument ? nullptr : inputs, outputs);
    }

    if (gate != nullptr)
        *gate = 1;
    else {
        inLeft.fill(0.5f);
        inRight.fill(0.5f);
    }

    double sumSquares = 0;
    int samples = 0;
    for (int block = 0; block < 10; ++block) {
        processor.compute(kBlockSize, instrument ? nullptr : inputs, outputs);
        for (int i = 0; i < kBlockSize; ++i) {
            const int frame = block * kBlockSize + i;
            if (frame >= 288 && frame < 576) {  // 6–12 ms after onset
                sumSquares += double(outLeft[i]) * double(outLeft[i]);
                ++samples;
            }
        }
    }
    return std::sqrt(sumSquares / samples);
}

void checkOnset(const char* path, bool instrument) {
    std::string error;
    auto compiled = compileDsp(path, error);
    INFO(error);
    REQUIRE(compiled != nullptr);

    std::unique_ptr<dsp> fresh(compiled->factory->createDSPInstance());
    std::unique_ptr<dsp> warmed(compiled->factory->createDSPInstance());
    REQUIRE(fresh != nullptr);
    REQUIRE(warmed != nullptr);

    const double first = onsetEnergy(*fresh, false, instrument);
    const double later = onsetEnergy(*warmed, true, instrument);
    INFO(path << ": first=" << first << ", warmed=" << later);
    REQUIRE(later > 0.001);
    REQUIRE(first > 0.001);
    CHECK(first / later > 0.8);
    CHECK(first / later < 1.2);
}

}  // namespace

TEST_CASE("Compiled Faust synth controls are ready on the first note", "[faust][2661]") {
    checkOnset("compiled/synth/magda_polysynth.dsp", true);
    checkOnset("compiled/synth/magda_fm.dsp", true);
}

TEST_CASE("Compiled Faust effect controls are ready on the first sample", "[faust][2661]") {
    checkOnset("compiled/distortion/magda_bitcrusher.dsp", false);
}
