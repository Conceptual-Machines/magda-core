// The fader boundary on the Tracktion side (#2087).
//
// `de7a0b7c` introduced the post-FX stage and recorded "post-fader audio routing
// (the fader boundary in PluginManagerSync)" as deferred. Until it landed,
// `ensureVolumePluginPosition` treated `LevelMeterPlugin` as the only thing that
// legitimately follows the fader and hoisted the fader past everything else, so
// a meter dropped into post-FX read pre-fader audio and the always-on
// measurement tap was demoted below the fader on the sync after it was
// installed.
//
// The native engine's half of the same contract is asserted in
// test_render_plan_compiler.cpp, against the plan rather than a plugin list.
// Both have to agree, which is why the ordering is stated as a rule about the
// fader rather than as an index in either.
//
// Lives in the JUCE target because it asserts on a real tracktion::engine::AudioTrack's
// pluginList, which needs the shared engine.

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/plugin_manager/PluginManager.hpp"
#include "magda/daw/audio/plugins/LevelsPlugin.hpp"
#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

/// The fader's own VolumeAndPanPlugin index, or -1. A Utility device is also a
/// VolumeAndPanPlugin, so the first one from the end that TE owns is not a safe
/// answer; the track fader is the one TE created with the track, which is the
/// only one present in these fixtures.
int faderIndex(tracktion::engine::AudioTrack& track) {
    for (int i = 0; i < track.pluginList.size(); ++i)
        if (dynamic_cast<tracktion::engine::VolumeAndPanPlugin*>(track.pluginList[i]) != nullptr)
            return i;
    return -1;
}

/// Index of the plugin the given device path is synced to, or -1. Identity by
/// path rather than by name: a TE plugin reports the plugin's own name, not the
/// DeviceInfo name the model gave it.
int indexOfDevice(tracktion::engine::AudioTrack& track, PluginManager& pluginManager,
                  const ChainNodePath& path) {
    for (int i = 0; i < track.pluginList.size(); ++i)
        if (pluginManager.getDevicePathForPlugin(track.pluginList[i]) == path)
            return i;
    return -1;
}

DeviceInfo makePostFxDevice() {
    DeviceInfo device;
    device.name = "Post Delay";
    device.manufacturer = "MAGDA";
    device.pluginId = "delay";
    device.deviceType = DeviceType::Effect;
    device.format = PluginFormat::Internal;
    return device;
}

}  // namespace

class PostFxFaderOrderTest final : public juce::UnitTest {
  public:
    PostFxFaderOrderTest() : juce::UnitTest("Post-FX Fader Order Tests", "magda") {}

    void runTest() override {
        // New tracks seed their fader side from the preference (#2094), which is
        // read off the machine's own config file. Pin it so these engine-order
        // tests do not depend on how the developer running them has it set.
        const bool previousDefault = Config::getInstance().getPostFxPostFaderByDefault();
        Config::getInstance().setPostFxPostFaderByDefault(true);
        const juce::ScopeGuard restoreDefault{[previousDefault] {
            Config::getInstance().setPostFxPostFaderByDefault(previousDefault);
        }};

        magda::test::runWithCleanJuceState([this] {
            testPostFaderIsDefault();
            testPreFaderPlacesTheStageBeforeTheFader();
            testTogglingMovesTheStage();
            testTheFaderReachesAPostFaderMeter();
            testSendsSitOnTheSideTheirFlagNames();
            testMultiOutTrackGetsItsSendsToo();
        });
    }

  private:
    struct Fixture {
        TrackId trackId = INVALID_TRACK_ID;
        tracktion::engine::AudioTrack* teTrack = nullptr;
        AudioBridge* bridge = nullptr;
        ChainNodePath devicePath;

        int deviceIndex() const {
            return indexOfDevice(*teTrack, bridge->getPluginManager(), devicePath);
        }
        int fader() const {
            return faderIndex(*teTrack);
        }
        void resync() const {
            bridge->syncTrackPlugins(trackId);
        }
    };

    Fixture makeTrackWithPostFxDevice() {
        Fixture f;
        auto& wrapper = magda::test::getSharedEngine();
        f.bridge = wrapper.getAudioBridge();
        if (f.bridge == nullptr)
            return f;

        auto& tm = TrackManager::getInstance();
        f.trackId = tm.createTrack("PostFx", TrackType::Media);
        const auto deviceId = tm.addDeviceToPostFx(f.trackId, makePostFxDevice());
        f.devicePath = ChainNodePath::postFxDevice(f.trackId, deviceId);
        f.bridge->syncTrackPlugins(f.trackId);
        f.teTrack = f.bridge->getAudioTrack(f.trackId);
        return f;
    }

    void testPostFaderIsDefault() {
        beginTest("A post-FX device sits after the fader by default");

        auto f = makeTrackWithPostFxDevice();
        expect(f.teTrack != nullptr, "the track must reach the engine");
        if (f.teTrack == nullptr)
            return;

        expect(TrackManager::getInstance().getTrack(f.trackId)->chain.postFxPostFader,
               "a new chain is post-fader");

        expect(f.fader() >= 0, "the track must have a fader");
        expect(f.deviceIndex() >= 0, "the post-FX device must reach the plugin list");
        expect(f.fader() < f.deviceIndex(), "the fader must come before the post-FX device");
    }

    void testPreFaderPlacesTheStageBeforeTheFader() {
        beginTest("A pre-fader chain puts the stage back above the fader");

        auto f = makeTrackWithPostFxDevice();
        if (f.teTrack == nullptr)
            return;

        TrackManager::getInstance().setPostFxPostFader(f.trackId, false);
        f.resync();

        expect(f.fader() >= 0 && f.deviceIndex() >= 0, "both must be present");
        expect(f.deviceIndex() < f.fader(), "a pre-fader stage feeds the fader");
    }

    void testTogglingMovesTheStage() {
        beginTest("Toggling moves the stage across the fader and back");

        auto f = makeTrackWithPostFxDevice();
        if (f.teTrack == nullptr)
            return;

        auto& tm = TrackManager::getInstance();

        tm.setPostFxPostFader(f.trackId, false);
        f.resync();
        expect(f.deviceIndex() < f.fader(), "pre-fader after the first toggle");

        tm.setPostFxPostFader(f.trackId, true);
        f.resync();
        expect(f.fader() < f.deviceIndex(), "post-fader after toggling back");

        // The meter stays last whichever side the stage is on: it is what TE
        // reads for the track's own level display, and a stage that got between
        // the meter and the output would make the two disagree.
        const int meter = [&] {
            for (int i = 0; i < f.teTrack->pluginList.size(); ++i)
                if (dynamic_cast<tracktion::engine::LevelMeterPlugin*>(f.teTrack->pluginList[i]) !=
                    nullptr)
                    return i;
            return -1;
        }();
        if (meter >= 0)
            expect(meter == f.teTrack->pluginList.size() - 1, "the level meter stays last");
    }

    /// The acceptance criterion the ordering assertions above are a proxy for:
    /// pull the fader down and the reading has to follow it.
    ///
    /// Driven by walking the synced plugin list in order and applying each
    /// plugin to one buffer, which is what a linear TE plugin list does to a
    /// block. That keeps the test on the real ordering the sync produced rather
    /// than on a graph assembled here, and it needs no transport or render.
    void testTheFaderReachesAPostFaderMeter() {
        beginTest("A post-fader Levels device follows the track fader");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto trackId = tm.createTrack("Levels", TrackType::Media);

        DeviceInfo levels;
        levels.name = "Levels";
        levels.manufacturer = "MAGDA";
        levels.pluginId = daw::audio::LevelsPlugin::xmlTypeName;
        levels.deviceType = DeviceType::Analysis;
        levels.format = PluginFormat::Internal;
        const auto levelsId = tm.addDeviceToPostFx(trackId, levels);

        bridge->syncTrackPlugins(trackId);
        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "the track must reach the engine");
        if (teTrack == nullptr)
            return;

        auto plugin = bridge->getPlugin(ChainNodePath::postFxDevice(trackId, levelsId));
        auto* levelsPlugin = dynamic_cast<daw::audio::LevelsPlugin*>(plugin.get());
        expect(levelsPlugin != nullptr, "the Levels device must reach the engine");
        if (levelsPlugin == nullptr)
            return;

        // Measurement is gated on the meter being on screen.
        levelsPlugin->setActive(true);

        auto* fader = dynamic_cast<tracktion::engine::VolumeAndPanPlugin*>(
            teTrack->pluginList[faderIndex(*teTrack)]);
        expect(fader != nullptr, "the track must have a fader");
        if (fader == nullptr)
            return;

        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            tracktion::engine::PluginInitialisationInfo info;
            info.startTime = tracktion::TimePosition();
            info.sampleRate = kSampleRate;
            info.blockSizeSamples = kBlockSize;
            teTrack->pluginList[i]->baseClassInitialise(info);
        }

        // One block of full-scale DC through the plugin list in order, which is
        // what TE does to a block for a linear list. DC rather than a tone
        // because the assertion is about how much signal survives the fader, and
        // a constant makes the sample peak the gain itself.
        auto runBlock = [&] {
            juce::AudioBuffer<float> buffer(2, kBlockSize);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 1.0f, kBlockSize);

            tracktion::engine::MidiMessageArray midi;
            tracktion::engine::PluginRenderContext context(
                &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
                tracktion::TimeRange(), true, false, false, true);

            for (int i = 0; i < teTrack->pluginList.size(); ++i)
                teTrack->pluginList[i]->applyToBuffer(context);

            return buffer.getMagnitude(0, kBlockSize);
        };

        struct Reading {
            float bufferPeak = 0.0f;
            float meterDb = 0.0f;
        };

        auto readAt = [&](float sliderPos) {
            fader->setSliderPos(sliderPos);

            // The fader ramps its gain across a block rather than stepping, so
            // the first block after a move still peaks at the old gain. Settle
            // it, then reset the meter and measure one clean block.
            for (int warmUp = 0; warmUp < 8; ++warmUp)
                runBlock();

            levelsPlugin->requestReset();
            Reading reading;
            reading.bufferPeak = runBlock();
            reading.meterDb = levelsPlugin->getSnapshot().samplePeakDb;
            return reading;
        };

        const auto atUnity = readAt(tracktion::engine::decibelsToVolumeFaderPosition(0.0f));
        const auto atSilence = readAt(0.0f);

        // The buffer assertion first: if the fader is not attenuating at all
        // then the meter reading says nothing about where it sits.
        expect(atUnity.bufferPeak > 0.5f, "at unity the signal must survive the fader");
        expect(atSilence.bufferPeak < 0.01f, "at the bottom the fader must silence the signal");

        expect(atUnity.meterDb > -6.0f, "at unity the meter must see the signal");
        expect(atSilence.meterDb < atUnity.meterDb - 40.0f,
               "pulling the fader to silence must take the signal out of the meter");

        // And the other direction, which is what makes the assertion above mean
        // something: put the stage back above the fader and the meter stops
        // following it. This is the reported bug, reproduced on demand.
        tm.setPostFxPostFader(trackId, false);
        bridge->syncTrackPlugins(trackId);
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            tracktion::engine::PluginInitialisationInfo info;
            info.startTime = tracktion::TimePosition();
            info.sampleRate = kSampleRate;
            info.blockSizeSamples = kBlockSize;
            teTrack->pluginList[i]->baseClassInitialise(info);
        }

        const auto preFaderAtUnity = readAt(tracktion::engine::decibelsToVolumeFaderPosition(0.0f));
        const auto preFaderAtSilence = readAt(0.0f);

        expect(preFaderAtSilence.bufferPeak < 0.01f, "the fader still silences the track output");
        expect(preFaderAtSilence.meterDb > preFaderAtUnity.meterDb - 1.0f,
               "a pre-fader meter reads the same whatever the fader does");
    }

    /// Sends are sequenced across the fader by their own flag now. They used to
    /// be appended and never moved, which only looked consistent because the
    /// fader was hoisted past everything: every send ended up above it whatever
    /// SendInfo::preFader said, while the native compiler has always split on it.
    ///
    /// Pinned because a send crossing the fader is as audible as the meter fix
    /// this PR is about, and because without the sequencing the side a send
    /// lands on depends on whether the track owns a post-FX device.
    void testSendsSitOnTheSideTheirFlagNames() {
        beginTest("A send sits on the side of the fader its flag names");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto trackId = tm.createTrack("Sends", TrackType::Media);

        SendInfo pre;
        pre.busIndex = 0;
        pre.preFader = true;
        SendInfo post;
        post.busIndex = 1;
        post.preFader = false;

        auto* trackInfo = tm.getTrack(trackId);
        expect(trackInfo != nullptr, "the track must exist in the model");
        if (trackInfo == nullptr)
            return;
        trackInfo->sends.push_back(pre);
        trackInfo->sends.push_back(post);

        bridge->syncTrackPlugins(trackId);
        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "the track must reach the engine");
        if (teTrack == nullptr)
            return;

        auto sendIndex = [&](int busIndex) {
            for (int i = 0; i < teTrack->pluginList.size(); ++i)
                if (auto* aux =
                        dynamic_cast<tracktion::engine::AuxSendPlugin*>(teTrack->pluginList[i]);
                    aux != nullptr && aux->getBusNumber() == busIndex)
                    return i;
            return -1;
        };

        const int fader = faderIndex(*teTrack);
        expect(fader >= 0, "the track must have a fader");
        expect(sendIndex(0) >= 0 && sendIndex(1) >= 0, "both sends must reach the plugin list");
        expect(sendIndex(0) < fader, "a pre-fader send taps above the fader");
        expect(fader < sendIndex(1), "a post-fader send taps below it");

        // And the side must not depend on the post-FX stage being occupied,
        // which is the failure mode the sequencing exists to rule out.
        DeviceInfo levels;
        levels.name = "Levels";
        levels.manufacturer = "MAGDA";
        levels.pluginId = daw::audio::LevelsPlugin::xmlTypeName;
        levels.deviceType = DeviceType::Analysis;
        levels.format = PluginFormat::Internal;
        tm.addDeviceToPostFx(trackId, levels);
        bridge->syncTrackPlugins(trackId);

        const int faderWithStage = faderIndex(*teTrack);
        expect(sendIndex(0) < faderWithStage, "a pre-fader send stays above the fader");
        expect(faderWithStage < sendIndex(1), "a post-fader send stays below it");
    }

    /// A multi-out track takes its own sync path, which returned from
    /// syncTrackPlugins long before the send reconciliation ran. It therefore
    /// had no AuxSendPlugins at all, while the native compiler emitted its sends
    /// from the same model: the two engines disagreed about whether the sends
    /// existed, not merely about where they sat. Ordering could not surface that,
    /// because it can only place plugins that are already there.
    void testMultiOutTrackGetsItsSendsToo() {
        beginTest("A multi-out track's sends exist and straddle the fader");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();

        // A real multi-out child, built the way the model builds one: the source
        // instrument declares its output pairs and activating one creates the
        // child track and its link. A hand-made link is not enough — the sync
        // path resolves the source device and returns early without it.
        const auto sourceId = tm.createTrack("Source", TrackType::Media);
        DeviceInfo instrument;
        instrument.name = "MultiOutSynth";
        instrument.format = PluginFormat::Internal;
        instrument.pluginId = "multisynth";
        instrument.isInstrument = true;
        instrument.multiOut.isMultiOut = true;
        instrument.multiOut.totalOutputChannels = 4;
        instrument.multiOut.outputPairs = {
            {0, "Main 1-2", false, INVALID_TRACK_ID, 1, 2},
            {1, "Out 3-4", false, INVALID_TRACK_ID, 3, 2},
        };
        const auto deviceId = tm.addDeviceToTrack(sourceId, instrument);
        const auto trackId = tm.activateMultiOutPair(sourceId, deviceId, 1);
        expect(trackId != INVALID_TRACK_ID, "the multi-out child must be created");
        if (trackId == INVALID_TRACK_ID)
            return;

        auto* trackInfo = tm.getTrack(trackId);
        expect(trackInfo != nullptr, "the child must exist in the model");
        if (trackInfo == nullptr)
            return;
        expect(trackInfo->type == TrackType::MultiOut, "the child must take the multi-out path");

        SendInfo pre;
        pre.busIndex = 0;
        pre.preFader = true;
        SendInfo post;
        post.busIndex = 1;
        post.preFader = false;
        trackInfo->sends.push_back(pre);
        trackInfo->sends.push_back(post);

        bridge->syncTrackPlugins(trackId);
        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "the track must reach the engine");
        if (teTrack == nullptr)
            return;

        auto sendIndex = [&](int busIndex) {
            for (int i = 0; i < teTrack->pluginList.size(); ++i)
                if (auto* aux =
                        dynamic_cast<tracktion::engine::AuxSendPlugin*>(teTrack->pluginList[i]);
                    aux != nullptr && aux->getBusNumber() == busIndex)
                    return i;
            return -1;
        };

        expect(sendIndex(0) >= 0, "the pre-fader send must exist on a multi-out track");
        expect(sendIndex(1) >= 0, "the post-fader send must exist on a multi-out track");

        const int fader = faderIndex(*teTrack);
        expect(fader >= 0, "the track must have a fader");
        expect(sendIndex(0) < fader, "a pre-fader send taps above the fader");
        expect(fader < sendIndex(1), "a post-fader send taps below it");
    }
};

static PostFxFaderOrderTest postFxFaderOrderTest;
