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
constexpr int kChannels = 2;

// 6-12 ms after the onset: past the amp envelope's attack, still inside the
// window a 10-20 ms parameter ramp would be climbing through.
constexpr int kMeasureFrom = 288;
constexpr int kMeasureTo = 576;

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

/// One initialised instance, driven the way the host drives a voice.
class Harness {
  public:
    explicit Harness(dsp& processor, bool instrument) : processor_(processor) {
        processor_.init(kSampleRate);
        processor_.buildUserInterface(&ui_);

        // The generated compute() writes one pointer per declared channel, and
        // these buffers are fixed at two.
        REQUIRE(processor_.getNumInputs() <= kChannels);
        REQUIRE(processor_.getNumOutputs() <= kChannels);

        if (instrument) {
            gate_ = ui_.getParamZone("gate");
            REQUIRE(gate_ != nullptr);
            *gate_ = 0;
        } else {
            inLeft_.fill(0.5f);
            inRight_.fill(0.5f);
        }
    }

    void set(const char* label, FAUSTFLOAT value) {
        auto* zone = ui_.getParamZone(label);
        REQUIRE(zone != nullptr);
        *zone = value;
    }

    void hold(int blocks) {
        for (int block = 0; block < blocks; ++block)
            compute();
    }

    void gateOn() {
        if (gate_ != nullptr)
            *gate_ = 1;
    }

    void gateOff() {
        if (gate_ != nullptr)
            *gate_ = 0;
    }

    /// RMS of the measurement window, counted from the next sample computed.
    double onsetRms() {
        double sumSquares = 0;
        int samples = 0;
        for (int block = 0; block * kBlockSize < kMeasureTo; ++block) {
            compute();
            for (int i = 0; i < kBlockSize; ++i) {
                const int frame = block * kBlockSize + i;
                if (frame >= kMeasureFrom && frame < kMeasureTo) {
                    sumSquares += double(outLeft_[i]) * double(outLeft_[i]);
                    ++samples;
                }
            }
        }
        return std::sqrt(sumSquares / samples);
    }

  private:
    void compute() {
        FAUSTFLOAT* inputs[] = {inLeft_.data(), inRight_.data()};
        FAUSTFLOAT* outputs[] = {outLeft_.data(), outRight_.data()};
        processor_.compute(kBlockSize, gate_ != nullptr ? nullptr : inputs, outputs);
    }

    dsp& processor_;
    MapUI ui_;
    FAUSTFLOAT* gate_ = nullptr;
    std::array<FAUSTFLOAT, kBlockSize> inLeft_{};
    std::array<FAUSTFLOAT, kBlockSize> inRight_{};
    std::array<FAUSTFLOAT, kBlockSize> outLeft_{};
    std::array<FAUSTFLOAT, kBlockSize> outRight_{};
};

/// The same onset straight after a clear and after the controls have settled.
/// Both start from silence, so only the parameter smoothers differ.
void checkFirstOnset(const char* path, bool instrument) {
    std::string error;
    auto compiled = compileDsp(path, error);
    INFO(error);
    REQUIRE(compiled != nullptr);

    std::unique_ptr<dsp> freshDsp(compiled->factory->createDSPInstance());
    std::unique_ptr<dsp> warmedDsp(compiled->factory->createDSPInstance());
    REQUIRE(freshDsp != nullptr);
    REQUIRE(warmedDsp != nullptr);

    Harness fresh(*freshDsp, instrument);
    fresh.gateOn();
    const double first = fresh.onsetRms();

    Harness warmed(*warmedDsp, instrument);
    warmed.hold(kWarmupBlocks);
    warmed.gateOn();
    const double later = warmed.onsetRms();

    INFO(path << ": first=" << first << ", warmed=" << later);
    REQUIRE(first > 0.001);
    REQUIRE(later > 0.001);
    CHECK(first / later > 0.8);
    CHECK(first / later < 1.2);
}

/// A voice is reused without instanceClear (poly-dsp.h keyOn never clears), so
/// a control moved while the voice is idle has to be taken on the next note -
/// not glided to from the note it last played.
void checkReusedVoiceOnset(const char* path, const char* label, FAUSTFLOAT idle,
                           FAUSTFLOAT played) {
    std::string error;
    auto compiled = compileDsp(path, error);
    INFO(error);
    REQUIRE(compiled != nullptr);

    std::unique_ptr<dsp> referenceDsp(compiled->factory->createDSPInstance());
    std::unique_ptr<dsp> reusedDsp(compiled->factory->createDSPInstance());
    REQUIRE(referenceDsp != nullptr);
    REQUIRE(reusedDsp != nullptr);

    // What the note should sound like: a voice that has only ever held `played`.
    Harness reference(*referenceDsp, true);
    reference.set(label, played);
    reference.gateOn();
    const double expected = reference.onsetRms();

    // A first note at `idle`, released, then the control moved and played again.
    Harness reused(*reusedDsp, true);
    reused.set(label, idle);
    reused.gateOn();
    reused.hold(kWarmupBlocks);
    reused.gateOff();
    reused.hold(kWarmupBlocks);
    reused.set(label, played);
    reused.gateOn();
    const double second = reused.onsetRms();

    INFO(path << " " << label << ": expected=" << expected << ", second note=" << second);
    REQUIRE(expected > 0.001);
    CHECK(second / expected > 0.8);
    CHECK(second / expected < 1.2);
}

}  // namespace

TEST_CASE("Compiled Faust synth controls are ready on the first note", "[faust][2661]") {
    checkFirstOnset("compiled/synth/magda_polysynth.dsp", true);
    checkFirstOnset("compiled/synth/magda_fm.dsp", true);
}

TEST_CASE("Compiled Faust effect controls are ready on the first sample", "[faust][2661]") {
    checkFirstOnset("compiled/distortion/magda_bitcrusher.dsp", false);
}

TEST_CASE("Faust voice controls are ready on a reused voice's next note", "[faust][2661]") {
    checkReusedVoiceOnset("compiled/synth/magda_polysynth.dsp", "Output", -40.0f, 0.0f);
    checkReusedVoiceOnset("compiled/synth/magda_fm.dsp", "Op1 Level", -40.0f, 0.0f);
    checkReusedVoiceOnset("runtime/instruments/bass/neuro_logical.dsp", "Output", -24.0f, 6.0f);
}
