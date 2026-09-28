#include <juce_core/juce_core.h>

#include <memory>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/api/track_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/engine/AudioEngine.hpp"
#include "magda/daw/engine/AudioEngineChoice.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#if MAGDA_HAS_NATIVE_ENGINE
    #include "magda/daw/engine/MagdaAudioEngine.hpp"
#endif

namespace {

using namespace magda;

class RemoteRoutingEngineConformanceTest final : public juce::UnitTest {
  public:
    RemoteRoutingEngineConformanceTest()
        : juce::UnitTest("Remote Routing Engine Conformance", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] {
            exercise(nameOf(AudioEngineChoice::Tracktion),
                     std::make_unique<TracktionEngineWrapper>());
#if MAGDA_HAS_NATIVE_ENGINE
            exercise(nameOf(AudioEngineChoice::Magda),
                     std::make_unique<MagdaAudioEngine>(AudioEngineOptions{.headless = true}));
#endif
        });
    }

  private:
    void exercise(const juce::String& name, std::unique_ptr<AudioEngine> engine) {
        beginTest(name + " accepts the shared preflighted routing edit");

        auto& tracks = TrackManager::getInstance();
        auto& undo = UndoManager::getInstance();
        expect(engine->engineName() == name);
        tracks.clearAllTracks();
        undo.clearHistory();
        tracks.setAudioEngine(engine.get());

        const auto source = tracks.createTrack("Source", TrackType::Media);
        const auto destination = tracks.createTrack("Destination", TrackType::Media);
        TrackRoutingPatch patch;
        patch.audioInputEndpointId = "track:" + juce::String(source);

        TrackApiLive api;
        const auto result = api.setRouting(destination, patch);
        expect(result.status == SetTrackRoutingStatus::Applied);
        const auto* routed = tracks.getTrack(destination);
        expect(routed != nullptr);
        if (routed != nullptr)
            expect(routed->audioInputDevice == *patch.audioInputEndpointId);

        expect(undo.undo());
        const auto* restored = tracks.getTrack(destination);
        expect(restored != nullptr);
        if (restored != nullptr) {
            expect(restored->audioInputDevice.isEmpty());
            expect(restored->midiInputDevice == "all");
        }

        tracks.setAudioEngine(nullptr);
        undo.clearHistory();
        tracks.clearAllTracks();
    }
};

RemoteRoutingEngineConformanceTest remoteRoutingEngineConformanceTest;

}  // namespace
