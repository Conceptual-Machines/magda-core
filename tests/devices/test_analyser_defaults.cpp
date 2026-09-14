#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/audio/plugins/OscilloscopePlugin.hpp"
#include "../../magda/daw/audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "../../magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "../../magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "../../magda/daw/core/Config.hpp"
#include "../../magda/daw/core/DeviceInfo.hpp"
#include "../../magda/daw/core/DeviceState.hpp"

/**
 * #2663 — the track header's analysis toggle deletes the device and adds a new
 * one, so nothing the device itself held survives turning an analyser off and
 * on. What survives is the last-used setting, and the native engine builds its
 * devices detached: with no session registry to ask, it had nowhere to read one
 * from and every analyser started at the factory's.
 */

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;

namespace {

magda::DeviceInfo analyser(const juce::String& pluginId) {
    magda::DeviceInfo device;
    device.name = pluginId;
    device.pluginId = pluginId;
    device.format = magda::PluginFormat::Internal;
    device.deviceType = magda::DeviceType::Analysis;
    return device;
}

/// The device inside what the engine's factory built, or null.
template <typename Device> Device* deviceIn(magda::engine::EngineDevice* built) {
    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(built);
    return hosted != nullptr ? dynamic_cast<Device*>(&hosted->device()) : nullptr;
}

/// Config is a singleton the rest of the suite shares, and these are in-memory
/// writes: put back what was there.
struct ScopedAnalyserDefaults {
    ScopedAnalyserDefaults()
        : scope(magda::Config::getInstance().getOscilloscopeDefaults()),
          spectrum(magda::Config::getInstance().getSpectrumDefaults()) {}

    ~ScopedAnalyserDefaults() {
        magda::Config::getInstance().setOscilloscopeDefaults(scope);
        magda::Config::getInstance().setSpectrumDefaults(spectrum);
    }

    magda::Config::OscilloscopeDefaults scope;
    magda::Config::SpectrumDefaults spectrum;
};

}  // namespace

TEST_CASE("A fresh analyser starts from the last-used settings", "[analyser-defaults][2663]") {
    const ScopedAnalyserDefaults restore;

    magda::Config::OscilloscopeDefaults scopeDefaults;
    scopeDefaults.timebaseMs = 250.0f;
    scopeDefaults.traceColour = 3;
    magda::Config::getInstance().setOscilloscopeDefaults(scopeDefaults);

    magda::Config::SpectrumDefaults spectrumDefaults;
    spectrumDefaults.fftOrder = 12;
    spectrumDefaults.slopeDbPerOct = 3.0f;
    spectrumDefaults.smoothing = 0.25f;
    spectrumDefaults.traceColour = 2;
    magda::Config::getInstance().setSpectrumDefaults(spectrumDefaults);

    const auto builtScope = adapter::createEngineDevice(analyser("oscilloscope"),
                                                        /*offlineRender=*/false);
    auto* scope = deviceIn<audio::OscilloscopePlugin>(builtScope.get());
    REQUIRE(scope != nullptr);
    CHECK(scope->getTimebaseMs() == Catch::Approx(250.0f));
    CHECK(scope->getTraceColourIndex() == 3);

    const auto builtSpectrum = adapter::createEngineDevice(analyser("spectrumanalyzer"),
                                                           /*offlineRender=*/false);
    auto* spectrum = deviceIn<audio::SpectrumAnalyzerPlugin>(builtSpectrum.get());
    REQUIRE(spectrum != nullptr);
    CHECK(spectrum->getFftOrder() == 12);
    CHECK(spectrum->getSlopeDbPerOct() == Catch::Approx(3.0f));
    CHECK(spectrum->getSmoothing() == Catch::Approx(0.25f));
    CHECK(spectrum->getTraceColourIndex() == 2);
}

TEST_CASE("A saved setting still beats the last-used one", "[analyser-defaults][2663]") {
    const ScopedAnalyserDefaults restore;

    magda::Config::OscilloscopeDefaults scopeDefaults;
    scopeDefaults.timebaseMs = 250.0f;
    scopeDefaults.traceColour = 3;
    magda::Config::getInstance().setOscilloscopeDefaults(scopeDefaults);

    // A device the project has state for is not a fresh one: what it holds is
    // what it comes back as, whatever was last used elsewhere.
    magda::device_state::Doc doc;
    doc.deviceType = "oscilloscope";
    doc.root.props.set(juce::Identifier("timebaseMs"), 40.0f);
    doc.root.props.set(juce::Identifier("traceColour"), 1);

    auto device = analyser("oscilloscope");
    device.pluginState = magda::device_state::encode(doc);

    const auto built = adapter::createEngineDevice(device, /*offlineRender=*/false);
    auto* scope = deviceIn<audio::OscilloscopePlugin>(built.get());
    REQUIRE(scope != nullptr);
    CHECK(scope->getTimebaseMs() == Catch::Approx(40.0f));
    CHECK(scope->getTraceColourIndex() == 1);
}
