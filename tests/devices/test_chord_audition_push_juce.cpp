#include <juce_core/juce_core.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/plugins/MidiChordEnginePlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

namespace audio = magda::daw::audio;

/**
 * @brief The Chord Engine on @p trackId, wherever on the chain it sits.
 *
 * @param bridge  the live bridge holding the engine's tracks
 * @param trackId the track to search
 * @return the device, or null when the track carries none.
 */
audio::MidiChordEnginePlugin* engineOn(magda::AudioBridge& bridge, TrackId trackId) {
    auto* teTrack = bridge.getAudioTrack(trackId);
    if (teTrack == nullptr)
        return nullptr;

    for (auto* plugin : teTrack->pluginList)
        if (auto* engine =
                audio::tracktion_adapter::deviceFromPlugin<audio::MidiChordEnginePlugin>(plugin))
            return engine;

    return nullptr;
}

}  // namespace

/**
 * @brief The audition toggle reaching the device (#2314).
 *
 * The device is told what the chord track's mute is rather than polling for it,
 * which puts the burden on the host: a device that is never told keeps the
 * default and lets playback into its detection. What these hold is that it is
 * told, on the two occasions it would otherwise miss.
 */
class ChordAuditionPushTests : public juce::UnitTest {
  public:
    ChordAuditionPushTests() : juce::UnitTest("Chord Audition Push", "magda") {}

    void runTest() override {
        magda::test::ScopedJuceTestState guard;

        beginTest("An engine on an already-muted chord track is told");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "the shared engine must have a bridge");
        if (bridge == nullptr)
            return;

        auto& tracks = TrackManager::getInstance();

        // createTrack notifies before it adds the chord track's own devices, so
        // the full sync that follows has no engine to reach. What catches it is
        // the push after the devices land.
        const auto trackId = tracks.createTrack("Chords", TrackType::Chord);
        bridge->syncTrackPlugins(trackId);

        auto* track = tracks.getTrack(trackId);
        expect(track != nullptr, "the chord track must exist");
        auto* engine = engineOn(*bridge, trackId);
        expect(engine != nullptr, "the chord track must carry a Chord Engine");

        if (track == nullptr || engine == nullptr)
            return;

        // Muted first, then the device sync, which is the order the report
        // describes: adding a Chord Engine to a chord track whose audition is
        // already off. `true` can only have come from the push, because the
        // device's own default is `false`.
        track->muted = true;
        bridge->trackDevicesChanged(trackId);

        expect(engine->chordTrackMuted(), "an engine must be told on the sync that creates it");

        beginTest("Toggling audition reaches the engine");

        track->muted = false;
        bridge->trackPropertyChanged(static_cast<int>(trackId));

        expect(!engine->chordTrackMuted(), "the toggle must reach the device");

        tracks.clearAllTracks();
    }
};

static ChordAuditionPushTests chordAuditionPushTests;
