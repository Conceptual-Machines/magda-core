#include <faust/gui/MapUI.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "magda/daw/audio/plugins/FaustBackend.hpp"

/**
 * @file test_faust_default_synth_patch.cpp
 * @brief The patch every Faust Instrument comes up with, played (#2237).
 *
 * `fi.resonlp`'s second argument is a Q and it computes `1/Q`. The patch used to
 * hand it the resonance control unchanged, declared from zero, so at the bottom
 * of that slider the filter's feedback coefficient came out
 * `(a0 - inf + csq)/inf`. NaN, on the master, from the voice's first sample.
 *
 * It did not recover either: that NaN becomes the biquad's own state, so a voice
 * which touched zero once stayed dead however far the slider was moved back.
 *
 * Driven through libfaust rather than through FaustInstrumentPlugin, and that is
 * not a convenience. The plugin is a te::Plugin and wants an Edit, which puts it
 * in the JUCE target -- and the JUCE target cannot have the Faust standard
 * library staged beside it while #2238 stands, so a test there could not compile
 * this patch at all. Here the library is staged and the question is about the
 * DSP rather than about the host, which is where the defect was.
 */

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBlockSize = 64;

/// The bundled copy of the patch, which the plugin's compiled-in default mirrors
/// and is required by both their comments to stay in step with.
std::string defaultSynthSource() {
    return juce::File(MAGDA_FAUST_RUNTIME_DSP_DIR)
        .getChildFile("instruments/synth/simple_synth.dsp")
        .loadFileAsString()
        .toStdString();
}

/// A compiled instance of @p source, or null with the reason in @p errorOut.
struct Compiled {
    magda::faust::Factory* factory = nullptr;
    std::unique_ptr<dsp> instance;

    ~Compiled() {
        instance.reset();
        if (factory != nullptr)
            magda::faust::deleteFactory(factory);
    }
};

std::unique_ptr<Compiled> compile(const std::string& source, std::string& errorOut) {
    const std::string libraries = MAGDA_FAUST_LIBRARIES_DIR;
    const char* argv[] = {"-I", libraries.c_str()};

    auto compiled = std::make_unique<Compiled>();
    compiled->factory =
        magda::faust::createFactoryFromString("magda_default_synth", source, 2, argv, errorOut);
    if (compiled->factory == nullptr)
        return nullptr;

    compiled->instance.reset(compiled->factory->createDSPInstance());
    if (compiled->instance == nullptr) {
        errorOut = "the factory produced no instance";
        return nullptr;
    }

    compiled->instance->init(kSampleRate);
    return compiled;
}

}  // namespace

TEST_CASE("The default synth patch plays a finite signal at every resonance",
          "[faust][instrument]") {
    std::string error;
    const auto source = defaultSynthSource();
    REQUIRE_FALSE(source.empty());

    auto compiled = compile(source, error);
    INFO(error);
    REQUIRE(compiled != nullptr);

    MapUI ui;
    compiled->instance->buildUserInterface(&ui);

    // Addressed by the labels the patch declares, so a control that is renamed
    // or dropped fails here rather than being silently not driven.
    auto* resonance = ui.getParamZone("resonance");
    auto* gate = ui.getParamZone("gate");
    REQUIRE(resonance != nullptr);
    REQUIRE(gate != nullptr);

    const auto numOutputs = compiled->instance->getNumOutputs();
    REQUIRE(numOutputs > 0);

    std::vector<std::vector<FAUSTFLOAT>> channels(static_cast<std::size_t>(numOutputs),
                                                  std::vector<FAUSTFLOAT>(kBlockSize, 0.0f));
    std::vector<FAUSTFLOAT*> outputs;
    for (auto& channel : channels)
        outputs.push_back(channel.data());

    // Every step of the control rather than its ends. The divide by zero lived
    // at the bottom, and a filter is the kind of thing that can go non-finite
    // anywhere along a coefficient curve rather than only where somebody
    // predicted it would.
    constexpr int kSteps = 21;

    for (int step = 0; step < kSteps; ++step) {
        const auto position = static_cast<FAUSTFLOAT>(step) / static_cast<FAUSTFLOAT>(kSteps - 1);

        // Fresh for each position. A NaN is absorbing: once it reaches the
        // biquad's state the instance is dead for every position after it, so
        // reusing one would report the first failure twenty-one times and say
        // nothing about the other twenty.
        auto voice = compile(source, error);
        INFO(error);
        REQUIRE(voice != nullptr);

        MapUI voiceUi;
        voice->instance->buildUserInterface(&voiceUi);
        *voiceUi.getParamZone("resonance") = position;
        *voiceUi.getParamZone("gate") = 1.0f;

        auto peak = 0.0f;
        for (int block = 0; block < 8; ++block) {
            for (auto& channel : channels)
                std::fill(channel.begin(), channel.end(), 0.0f);

            voice->instance->compute(kBlockSize, nullptr, outputs.data());

            for (const auto& channel : channels)
                for (const auto sample : channel) {
                    INFO("resonance " << position);
                    REQUIRE(std::isfinite(sample));
                    peak = std::max(peak, std::abs(sample));
                }
        }

        // And it is a filter rather than a mute. A resonance that silenced the
        // voice at one end would satisfy the finiteness check perfectly well.
        INFO("resonance " << position);
        CHECK(peak > 0.0f);
    }
}
