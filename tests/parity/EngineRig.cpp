#include "EngineRig.hpp"

#include <cstdlib>
#include <numeric>
#include <utility>

#include "TracktionProxies.hpp"
#include "magda/daw/audio/io/AudioIOService.hpp"
#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/AudioEngine.hpp"
#include "magda/daw/engine/MagdaAudioEngine.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace magda::parity {

namespace {

void setEngineChoice(const char* value) {
#if JUCE_WINDOWS
    _putenv_s("MAGDA_AUDIO_ENGINE", value);
#else
    setenv("MAGDA_AUDIO_ENGINE", value, 1);
#endif
}

bool chainIsLoading(const std::vector<ChainElement>& elements, TrackId trackId) {
    auto loading = false;
    chain_walk::forEachDevice(elements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Enter,
                              [&loading](const DeviceInfo& device, const ChainNodePath&) {
                                  loading = loading || device.loadState == DeviceLoadState::Loading;
                              });
    return loading;
}

/// Whether any device the model holds is still waiting for its plugin to instantiate.
bool anyDeviceLoading() {
    const auto trackIsLoading = [](const TrackInfo& track) {
        if (chainIsLoading(track.chain.fxChainElements, track.id))
            return true;

        for (const auto* stage :
             {&track.chain.postFxChainElements, &track.chain.mixerAnalysisElements})
            for (const auto& element : *stage) {
                if (element.device.loadState == DeviceLoadState::Loading)
                    return true;
                if (element.device.pads)
                    for (const auto& pad : element.device.pads->chains)
                        if (chainIsLoading(pad.elements, track.id))
                            return true;
            }
        return false;
    };

    auto& tracks = TrackManager::getInstance();
    for (const auto& track : tracks.getTracks())
        if (trackIsLoading(track))
            return true;

    const auto* master = tracks.getTrack(MASTER_TRACK_ID);
    return master != nullptr && trackIsLoading(*master);
}

}  // namespace

std::unique_ptr<EngineRig> EngineRig::create(const std::string& engine, double sampleRate,
                                             int blockSize, std::string& failure) {
    std::unique_ptr<EngineRig> rig(new EngineRig());
    rig->native_ = engine == "native";
    if (!rig->native_ && engine != "tracktion") {
        failure = "no engine called " + engine;
        return nullptr;
    }

    setEngineChoice(rig->native_ ? "magda" : "tracktion");
    rig->engine_ = createDefaultAudioEngine({.headless = true});
    if (!rig->engine_->initialize()) {
        failure = "the engine did not initialise";
        return nullptr;
    }

    const auto* const expected =
        nameOf(rig->native_ ? AudioEngineChoice::Magda : AudioEngineChoice::Tracktion);
    if (rig->engine_->engineName() != juce::String(expected)) {
        failure = "asked for " + engine + " and got " + rig->engine_->engineName().toStdString();
        return nullptr;
    }

    TrackManager::getInstance().setAudioEngine(rig->engine_.get());

    auto backend = std::make_unique<PumpBackend>(sampleRate, blockSize);
    rig->backend_ = backend.get();

    if (rig->native_) {
        auto* audioIO = static_cast<AudioIOService*>(rig->engine_->getAudioIO());
        audioIO->getDeviceManager().addAudioDeviceType(std::move(backend));
    } else {
        // Headless skips Tracktion's device manager, which is what attaches its callback to
        // the interface; initialised here on the pump, as the app does on the real one.
        auto* wrapper = static_cast<TracktionEngineWrapper*>(rig->engine_.get());
        auto& devices = wrapper->getEngine()->getDeviceManager();
        devices.deviceManager.addAudioDeviceType(std::move(backend));
        devices.deviceManager.setCurrentAudioDeviceType(kPumpBackend, true);
        devices.initialise(0, kPumpOutputChannels);
    }

    std::vector<int> outputs(kPumpOutputChannels);
    std::iota(outputs.begin(), outputs.end(), 0);

    const auto error = rig->engine_->getAudioIO()->apply({.backend = kPumpBackend,
                                                          .inputInterface = {},
                                                          .outputInterface = kPumpInterface,
                                                          .sampleRate = sampleRate,
                                                          .bufferSize = blockSize,
                                                          .inputChannels = {},
                                                          .outputChannels = outputs});
    if (error.isNotEmpty() || rig->device() == nullptr) {
        failure = "the pump would not open: " + error.toStdString();
        return nullptr;
    }

    // The Edit was built before any interface existed; this is the graph the app has once
    // its interface is open.
    if (!rig->native_)
        if (auto* edit = static_cast<TracktionEngineWrapper*>(rig->engine_.get())->getEdit())
            edit->getTransport().ensureContextAllocated(true);

    return rig;
}

EngineRig::~EngineRig() = default;

void EngineRig::loadStarting() {
    graphBuiltForLoad_ = false;
    if (native_)
        publishRequestsAtLoad_ =
            static_cast<MagdaAudioEngine*>(engine_.get())->hostForTesting().publishRequests();
}

bool EngineRig::isReady() {
    if (anyDeviceLoading())
        return false;

    if (native_) {
        auto& host = static_cast<MagdaAudioEngine*>(engine_.get())->hostForTesting();
        const auto heard =
            !publishRequestsAtLoad_ || host.publishRequests() > *publishRequestsAtLoad_;
        return heard && host.isSettled() && host.pluginsLoading() == 0;
    }

    auto* wrapper = static_cast<TracktionEngineWrapper*>(engine_.get());
    auto* edit = wrapper->getEdit();
    auto* context = edit != nullptr ? edit->getCurrentPlaybackContext() : nullptr;
    if (context == nullptr || !context->isPlaybackGraphAllocated())
        return false;

    int waited = 0;
    if (!test::proxiesReady(*wrapper->getEngine(), *edit, waited))
        return false;

    // The Edit's own 1 ms rebuild timer is private, so whether it has fired since the last change
    // cannot be asked. The graph is built here instead, which is the rebuild that timer owes.
    if (!std::exchange(graphBuiltForLoad_, true))
        edit->getTransport().ensureContextAllocated(true);
    return true;
}

int EngineRig::reportedLatencySamples() const {
    if (native_)
        return static_cast<MagdaAudioEngine*>(engine_.get())->hostForTesting().latencySamples();

    auto* edit = static_cast<TracktionEngineWrapper*>(engine_.get())->getEdit();
    auto* context = edit != nullptr ? edit->getCurrentPlaybackContext() : nullptr;
    return context != nullptr ? context->getLatencySamples() : 0;
}

}  // namespace magda::parity
