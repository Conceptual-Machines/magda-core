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

/**
 * @brief Every Chord Engine on @p trackId, at any rack depth.
 *
 * Its own descent rather than the one under test: a finder sharing the walk it
 * is checking would pass for the same reason the walk was wrong.
 *
 * @param bridge  the live bridge holding the engine's tracks
 * @param trackId the track to search
 * @return the devices, outermost first.
 */
std::vector<audio::MidiChordEnginePlugin*> everyEngineOn(magda::AudioBridge& bridge,
                                                         TrackId trackId) {
    std::vector<audio::MidiChordEnginePlugin*> found;

    auto* teTrack = bridge.getAudioTrack(trackId);
    if (teTrack == nullptr)
        return found;

    const std::function<void(te::Plugin*)> visit = [&](te::Plugin* plugin) {
        if (auto* engine =
                audio::tracktion_adapter::deviceFromPlugin<audio::MidiChordEnginePlugin>(plugin))
            found.push_back(engine);

        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin))
            if (rackInstance->type != nullptr)
                for (auto* innerPlugin : rackInstance->type->getPlugins())
                    visit(innerPlugin);
    };

    for (auto* plugin : teTrack->pluginList)
        visit(plugin);

    return found;
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

        beginTest("An engine inside a nested rack is told too");

        // A nested rack is a rack type of its own with a RackInstance in the
        // outer one (RackSyncManager), so a walk that descends one level still
        // misses what is inside it.
        const auto outerRack = tracks.addRackToTrack(trackId, "Outer");
        expect(outerRack != INVALID_RACK_ID, "the outer rack must be created");

        const auto outerRackPath = ChainNodePath::rack(trackId, outerRack);
        const auto* rack = tracks.getRackByPath(outerRackPath);
        expect(rack != nullptr && !rack->chains.empty(), "the outer rack must have a chain");
        if (rack == nullptr || rack->chains.empty())
            return;

        const auto outerChainPath = outerRackPath.withChain(rack->chains.front().id);
        const auto innerRack = tracks.addRackToChainByPath(outerChainPath, "Inner");
        expect(innerRack != INVALID_RACK_ID, "the inner rack must be created");

        const auto innerRackPath = outerChainPath.withRack(innerRack);
        const auto* inner = tracks.getRackByPath(innerRackPath);
        expect(inner != nullptr && !inner->chains.empty(), "the inner rack must have a chain");
        if (inner == nullptr || inner->chains.empty())
            return;

        DeviceInfo nested;
        nested.name = "Chord Engine";
        nested.pluginId = "midichordengine";
        nested.format = PluginFormat::Internal;

        const auto nestedId = tracks.addDeviceToChainByPath(
            innerRackPath.withChain(inner->chains.front().id), nested);
        expect(nestedId != INVALID_DEVICE_ID, "the nested engine must be created");

        track->muted = true;
        bridge->trackPropertyChanged(static_cast<int>(trackId));

        // Every engine the track carries, not the nested one by name: what the
        // push owes is that none of them is missed.
        const auto engines = everyEngineOn(*bridge, trackId);
        expectGreaterThan(static_cast<int>(engines.size()), 1,
                          "the nested engine must reach the graph beside the track's own");

        for (auto* each : engines)
            expect(each->chordTrackMuted(), "every engine on the track must be told");

        tracks.clearAllTracks();
    }
};

static ChordAuditionPushTests chordAuditionPushTests;
