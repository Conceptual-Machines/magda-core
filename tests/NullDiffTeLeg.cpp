#include "NullDiffTeLeg.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "NullDiffGain.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/PluginWindowBridge.hpp"
#include "magda/daw/audio/TrackController.hpp"
#include "magda/daw/audio/WarpMarkerManager.hpp"
#include "magda/daw/audio/automation/AutomationBake.hpp"
#include "magda/daw/audio/modifiers/ModifierSync.hpp"
#include "magda/daw/audio/plugin_manager/PluginManager.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "magda/daw/audio/racks/InstrumentRackManager.hpp"
#include "magda/daw/audio/racks/RackSyncManager.hpp"
#include "magda/daw/audio/session/ClipSynchronizer.hpp"
#include "magda/daw/audio/transport/TransportStateManager.hpp"
#include "magda/daw/core/AutomationCurve.hpp"
#include "magda/daw/core/ChainNode.hpp"
#include "magda/daw/core/ChainRoutingModel.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/OfflineRenderHelper.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "plan/PlanCompiler.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace magda::nulldiff {

namespace te = tracktion;

namespace {

/// How long to let a proxy render before calling the case unmeasurable. Long
/// enough that a loaded machine is not a failure, short enough that a job which
/// is never going to finish does not hold the suite up.
constexpr int kProxyTimeoutMs = 20000;

/// Records what reaches the instrument slot. The comparison point on this side,
/// and it has to be the same point as on the other: what a synth receives.
///
/// Built directly rather than through the plugin cache, because registering a
/// type would put a test-only device in the app's own registry, where it would
/// show up in the browser and in everything else that walks it.
class MidiCapturePlugin final : public te::Plugin {
  public:
    explicit MidiCapturePlugin(te::PluginCreationInfo info) : te::Plugin(info) {}

    static const char* getPluginName() {
        return "Null Diff MIDI Capture";
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return "nulldiffmidicapture";
    }
    juce::String getShortName(int) override {
        return "NDCap";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    bool takesAudioInput() override {
        return true;
    }
    bool takesMidiInput() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    bool canBeAddedToClip() override {
        return false;
    }
    bool needsConstantBufferSize() override {
        return false;
    }

    void initialise(const te::PluginInitialisationInfo& info) override {
        sampleRate_ = info.sampleRate;
    }
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& context) override {
        if (context.destBuffer != nullptr)
            context.destBuffer->clear();

        if (context.bufferForMidiMessages == nullptr)
            return;

        // Where the block sits on the timeline, so a captured message carries a
        // position in the render rather than one inside a callback. TE stamps
        // its messages in seconds from the start of the block.
        const auto blockStart = context.editTime.getStart().inSeconds();

        for (auto& message : *context.bufferForMidiMessages) {
            if (message.getRawDataSize() < 2 || message.getRawDataSize() > 3)
                continue;

            const auto at = blockStart + message.getTimeStamp();
            const auto* raw = message.getRawData();

            captured.push_back({static_cast<std::int64_t>(std::llround(at * sampleRate_)),
                                static_cast<std::uint8_t>(raw[0]),
                                static_cast<std::uint8_t>(raw[1]),
                                message.getRawDataSize() > 2 ? static_cast<std::uint8_t>(raw[2])
                                                             : std::uint8_t{0}});
        }
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}

    MidiStream captured;

  private:
    double sampleRate_ = 44100.0;
};

/// The capture device, as the app's own registry sees it.
///
/// Registered rather than newly constructed, because a te::Plugin whose type
/// nothing can create is re-created from its own state by the plugin list and
/// refused: the log says "unknown type" and the track is left without the
/// mapping the clips are about to be synced onto. So this goes in through the
/// same door every internal device does, marked unsupported and out of the
/// browser the way the app's other internal taps are, and only ever in a test
/// binary.
const char* const kCapturePluginId = "nulldiffmidicapture";

/// What marks a device as the corpus's own rather than the product's.
///
/// The three devices in this file are registered in the app's registry because
/// that is the only way either leg can create one, and they are hidden from the
/// browser, but the registry is also what the device parameter freeze walks
/// (test_device_param_schema_juce.cpp). That file is the product's contract
/// about parameter order for saved automation and links, and a device that
/// exists only inside a test binary has no business in it: recording them would
/// freeze test fixtures as product schema, and leaving them out with no marker
/// fails the freeze on every build.
const char* const kCorpusDeviceTags[] = {"null-diff-corpus"};

void registerCaptureDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kCapturePluginId;
    spec.displayName = "Null Diff MIDI Capture";
    spec.browserCategory = "Utility";
    spec.description = "Records the MIDI reaching an instrument slot, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    spec.canCreateDetached = false;
    spec.canCreateOnTrack = false;
    spec.showInBrowser = false;
    spec.tags = kCorpusDeviceTags;
    spec.tagCount = static_cast<int>(std::size(kCorpusDeviceTags));
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        return dynamic_cast<MidiCapturePlugin*>(
                   magda::daw::audio::tracktion_adapter::pluginFromRef(plugin)) != nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new MidiCapturePlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

const bool captureDeviceRegistered = magda::daw::audio::registerDevicePack(registerCaptureDevice);

// =============================================================================
// The device with a parameter (#2123)
// =============================================================================

/// The property the gain's value is stored under.
const juce::Identifier kGainProperty("nulldiffGain");

/// The incumbent's half of the contract in NullDiffGain.hpp.
///
/// One parameter, a plain multiply, no smoothing. The value is read once at the
/// top of the block, which is what an AutomatableParameter is; the engine's twin
/// reads per sample, and the cases play silence wherever that could differ.
class GainPlugin : public te::Plugin {
  public:
    explicit GainPlugin(te::PluginCreationInfo info) : te::Plugin(info) {
        auto* undo = getUndoManager();
        value_.referTo(state, kGainProperty, undo, kGainDefault);

        gain = addParam(
            "gain", "Gain", {0.0f, 1.0f}, [](float value) { return juce::String(value, 4); },
            [](const juce::String& text) { return text.getFloatValue(); });
        gain->attachToCurrentValue(value_);
    }

    ~GainPlugin() override {
        notifyListenersOfDeletion();
        if (gain != nullptr)
            gain->detachFromCurrentValue();
    }

    static const char* getPluginName() {
        return "Null Diff Gain";
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return kGainPluginId;
    }
    juce::String getShortName(int) override {
        return "NDGain";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    bool takesAudioInput() override {
        return true;
    }
    bool takesMidiInput() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    bool canBeAddedToClip() override {
        return false;
    }
    bool needsConstantBufferSize() override {
        return false;
    }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& context) override {
        if (context.destBuffer == nullptr)
            return;

        context.destBuffer->applyGain(context.bufferStartSample, context.bufferNumSamples,
                                      gain->getCurrentValue());
    }

    void restorePluginStateFromValueTree(const juce::ValueTree& tree) override {
        te::copyPropertiesToCachedValues(tree, value_);
    }

    te::AutomatableParameter* gain = nullptr;

  private:
    juce::CachedValue<float> value_;
};

/// The narrow one (#2139).
///
/// One channel in and one out, which is the whole of what it is: the DSP is its
/// base's, and what a mono case measures is the bus around the device rather
/// than the device. Inside a rack the width is what the connection matrix reads
/// off the plugin, so declaring it here is what makes the incumbent narrow at
/// this point in the chain; the plan reads the same widths off the model.
///
/// The gain lands on the first channel alone, which is what a one-channel
/// plugin can touch whatever width of buffer the graph hands it.
class MonoGainPlugin final : public GainPlugin {
  public:
    explicit MonoGainPlugin(te::PluginCreationInfo info) : GainPlugin(std::move(info)) {}

    static const char* getPluginName() {
        return "Null Diff Mono Gain";
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return kMonoGainPluginId;
    }
    juce::String getShortName(int) override {
        return "NDMono";
    }

    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override {
        if (ins != nullptr)
            ins->add(TRANS("Mono"));
        if (outs != nullptr)
            outs->add(TRANS("Mono"));
    }

    int getNumOutputChannelsGivenInputs(int) override {
        return 1;
    }

    void applyToBuffer(const te::PluginRenderContext& context) override {
        if (context.destBuffer == nullptr || context.destBuffer->getNumChannels() == 0)
            return;

        context.destBuffer->applyGain(0, context.bufferStartSample, context.bufferNumSamples,
                                      gain->getCurrentValue());
    }
};

// =============================================================================
// The instrument (#2139)
// =============================================================================

/// The incumbent's half of the instrument contract in NullDiffGain.hpp.
///
/// A synth rather than an effect, and not only because the current engine's
/// multi-out path wants one: an instrument is the device that generates
/// instead of processing, and both engines route audio around it rather than
/// into it. Its width is the only thing its two subclasses differ by.
class ImpulseSynthPlugin : public te::Plugin {
  public:
    explicit ImpulseSynthPlugin(te::PluginCreationInfo info) : te::Plugin(info) {}

    ~ImpulseSynthPlugin() override {
        notifyListenersOfDeletion();
    }

    /// How many channels this one writes: two for the plain instrument, four
    /// for the multi-out one. The rack matrix reads it off the plugin and the
    /// plan reads the same figure off the model.
    virtual int channelCount() const = 0;

    juce::String getSelectableDescription() override {
        return getName();
    }

    bool takesAudioInput() override {
        return false;
    }
    bool takesMidiInput() override {
        return true;
    }
    bool isSynth() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return true;
    }
    bool canBeAddedToClip() override {
        return false;
    }
    bool canBeAddedToRack() override {
        return true;
    }
    bool needsConstantBufferSize() override {
        return false;
    }

    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override {
        // No audio in: neither engine feeds an instrument one, and saying
        // otherwise would have the rack matrix wire a bus this device never
        // reads.
        juce::ignoreUnused(ins);

        if (outs != nullptr)
            for (int channel = 1; channel <= channelCount(); ++channel)
                outs->add("Out " + juce::String(channel));
    }

    int getNumOutputChannelsGivenInputs(int) override {
        return channelCount();
    }

    void initialise(const te::PluginInitialisationInfo& info) override {
        sampleRate_ = info.sampleRate;
    }
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& context) override {
        if (context.destBuffer == nullptr)
            return;

        // A synth writes rather than processes, so the block starts silent:
        // whatever the graph left in this buffer is not this device's.
        context.destBuffer->clear(context.bufferStartSample, context.bufferNumSamples);

        if (context.bufferForMidiMessages == nullptr)
            return;

        for (auto& message : *context.bufferForMidiMessages) {
            if (!message.isNoteOn())
                continue;

            // TE stamps a message in seconds from the start of the block, and
            // the corpus's other MIDI cases are what pin that this lands on the
            // same sample the engine puts it on.
            const auto at = context.bufferStartSample +
                            static_cast<int>(std::llround(message.getTimeStamp() * sampleRate_));
            if (at < context.bufferStartSample ||
                at >= context.bufferStartSample + context.bufferNumSamples)
                continue;

            const auto level = static_cast<float>(message.getVelocity()) / 127.0f;

            // Pair 0 on the first two channels, every further pair at the
            // declared scale. A two-channel one has no further pair and writes
            // the first alone.
            for (int channel = 0; channel < channelCount(); ++channel)
                write(*context.destBuffer, channel, at,
                      channel < 2 ? level : level * kMultiOutSecondPairScale);
        }
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}

  private:
    static void write(juce::AudioBuffer<float>& target, int channel, int sample, float level) {
        if (channel < target.getNumChannels())
            target.addSample(channel, sample, level);
    }

    double sampleRate_ = 44100.0;
};

/// The plain one, at the width every other device in the corpus has.
class SynthPlugin final : public ImpulseSynthPlugin {
  public:
    explicit SynthPlugin(te::PluginCreationInfo info) : ImpulseSynthPlugin(std::move(info)) {}

    static const char* getPluginName() {
        return "Null Diff Synth";
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return kSynthPluginId;
    }
    juce::String getShortName(int) override {
        return "NDSynth";
    }

    int channelCount() const override {
        return 2;
    }
};

/// The one with two further channels, so the multi-out wrapper has pins three
/// and four to wire to the rack outputs a second RackInstance reads.
class MultiOutSynthPlugin final : public ImpulseSynthPlugin {
  public:
    explicit MultiOutSynthPlugin(te::PluginCreationInfo info)
        : ImpulseSynthPlugin(std::move(info)) {}

    static const char* getPluginName() {
        return "Null Diff Multi Out";
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return kMultiOutPluginId;
    }
    juce::String getShortName(int) override {
        return "NDMulti";
    }

    int channelCount() const override {
        return 4;
    }
};

void registerSynthDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kSynthPluginId;
    spec.displayName = SynthPlugin::getPluginName();
    spec.browserCategory = "Utility";
    spec.description = "A MIDI-driven instrument, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    spec.canCreateDetached = true;
    // True since #2174: the sync is what installs these now, and
    // loadDeviceAsPlugin refuses a spec that cannot be made on a track. What
    // keeps them out of the app is showInBrowser, and they are only ever
    // registered inside a test binary anyway.
    spec.canCreateOnTrack = true;
    spec.showInBrowser = false;
    spec.isInstrument = true;
    spec.tags = kCorpusDeviceTags;
    spec.tagCount = static_cast<int>(std::size(kCorpusDeviceTags));
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        return dynamic_cast<SynthPlugin*>(
                   magda::daw::audio::tracktion_adapter::pluginFromRef(plugin)) != nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new SynthPlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

void registerMultiOutDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kMultiOutPluginId;
    spec.displayName = MultiOutSynthPlugin::getPluginName();
    spec.browserCategory = "Utility";
    spec.description = "A four-channel instrument, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    spec.canCreateDetached = true;
    // True since #2174: the sync is what installs these now, and
    // loadDeviceAsPlugin refuses a spec that cannot be made on a track. What
    // keeps them out of the app is showInBrowser, and they are only ever
    // registered inside a test binary anyway.
    spec.canCreateOnTrack = true;
    spec.showInBrowser = false;
    spec.isInstrument = true;
    spec.tags = kCorpusDeviceTags;
    spec.tagCount = static_cast<int>(std::size(kCorpusDeviceTags));
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        return dynamic_cast<MultiOutSynthPlugin*>(
                   magda::daw::audio::tracktion_adapter::pluginFromRef(plugin)) != nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new MultiOutSynthPlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

const bool synthDeviceRegistered = magda::daw::audio::registerDevicePack(registerSynthDevice);
const bool multiOutDeviceRegistered = magda::daw::audio::registerDevicePack(registerMultiOutDevice);

void registerGainDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kGainPluginId;
    spec.displayName = GainPlugin::getPluginName();
    spec.browserCategory = "Utility";
    spec.description = "A gain with one parameter, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    // Detached creation is what a rack needs: RackSyncManager builds its inner
    // plugins through PluginManager::createPluginOnly, which refuses a spec
    // that cannot be made off a track (#2139). Nothing else changes with it --
    // the browser still never shows this device, and the track-level path
    // still builds its own.
    spec.canCreateDetached = true;
    // True since #2174: the sync is what installs these now, and
    // loadDeviceAsPlugin refuses a spec that cannot be made on a track. What
    // keeps them out of the app is showInBrowser, and they are only ever
    // registered inside a test binary anyway.
    spec.canCreateOnTrack = true;
    spec.showInBrowser = false;
    spec.tags = kCorpusDeviceTags;
    spec.tagCount = static_cast<int>(std::size(kCorpusDeviceTags));
    // The narrow one derives from this one, so a bare dynamic_cast would claim
    // it for this spec and it would be created two channels wide.
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        auto* found = magda::daw::audio::tracktion_adapter::pluginFromRef(plugin);
        return dynamic_cast<GainPlugin*>(found) != nullptr &&
               dynamic_cast<MonoGainPlugin*>(found) == nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new GainPlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

void registerMonoGainDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kMonoGainPluginId;
    spec.displayName = MonoGainPlugin::getPluginName();
    spec.browserCategory = "Utility";
    spec.description = "A one-channel gain, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    spec.canCreateDetached = true;
    // True since #2174: the sync is what installs these now, and
    // loadDeviceAsPlugin refuses a spec that cannot be made on a track. What
    // keeps them out of the app is showInBrowser, and they are only ever
    // registered inside a test binary anyway.
    spec.canCreateOnTrack = true;
    spec.showInBrowser = false;
    spec.tags = kCorpusDeviceTags;
    spec.tagCount = static_cast<int>(std::size(kCorpusDeviceTags));
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        return dynamic_cast<MonoGainPlugin*>(
                   magda::daw::audio::tracktion_adapter::pluginFromRef(plugin)) != nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new MonoGainPlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

const bool gainDeviceRegistered = magda::daw::audio::registerDevicePack(registerGainDevice);
const bool monoGainDeviceRegistered = magda::daw::audio::registerDevicePack(registerMonoGainDevice);

void pumpMessageThread(int milliseconds) {
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        manager->runDispatchLoopUntil(milliseconds);
}

/// Put the case's tempo into the Edit as steps.
///
/// TE's own tempo changes are steps unless a curve is set, so this is the
/// straightforward reading of the same list the native leg turns into pairs of
/// changes at one beat.
void applyTempo(te::Edit& edit, const Case& value) {
    auto& sequence = edit.tempoSequence;

    while (sequence.getNumTempos() > 1)
        sequence.removeTempo(sequence.getNumTempos() - 1, false);

    if (auto* first = sequence.getTempo(0))
        first->setBpm(value.tempo.front().bpm);

    for (std::size_t index = 1; index < value.tempo.size(); ++index)
        sequence.insertTempo(te::BeatPosition::fromBeats(value.tempo[index].beat),
                             value.tempo[index].bpm, 1.0f);
}

void installGrooves(te::Engine& engine, const Case& value) {
    if (value.grooveXml.isEmpty())
        return;

    const auto document = juce::parseXML(value.grooveXml);
    if (document == nullptr)
        return;

    auto& manager = engine.getGrooveTemplateManager();

    for (const auto* node : document->getChildWithTagNameIterator("GROOVETEMPLATE")) {
        const te::GrooveTemplate groove(node);

        // Replace one of the same name rather than adding a second, so that a
        // suite running the corpus twice does not accumulate them.
        auto index = manager.getNumTemplates();
        for (auto existing = 0; existing < manager.getNumTemplates(); ++existing)
            if (manager.getTemplateName(existing) == groove.getName())
                index = existing;

        manager.updateTemplate(index, groove);
    }
}

/// Every wave clip's playback file exists and nothing is still rendering it.
///
/// The check is on the render manager rather than on the file, because
/// AudioFile::isValid can go true before the job has released it, which the
/// app's own reverse path already had to learn.
bool proxiesReady(te::Engine& engine, te::Edit& edit, int& waitedFor) {
    waitedFor = 0;

    for (auto* track : te::getAudioTracks(edit)) {
        for (auto* clip : track->getClips()) {
            auto* audio = dynamic_cast<te::AudioClipBase*>(clip);
            if (audio == nullptr)
                continue;

            const auto playbackFile = audio->getPlaybackFile();
            if (!playbackFile.isValid())
                return false;

            if (engine.getRenderManager().isProxyBeingGenerated(playbackFile)) {
                ++waitedFor;
                return false;
            }
        }
    }

    return true;
}

}  // namespace

IncumbentRender renderIncumbent(const Case& value) {
    IncumbentRender result;

    auto& wrapper = magda::test::getSharedEngine();
    auto* engine = wrapper.getEngine();
    if (engine == nullptr) {
        result.failure = "no Tracktion engine";
        return result;
    }

    auto edit = te::test_utilities::createTestEdit(*engine, 1);
    if (edit == nullptr) {
        result.failure = "no Edit";
        return result;
    }

    applyTempo(*edit, value);
    installGrooves(*engine, value);

    const auto previousTempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    ProjectManager::getInstance().setTempo(value.tempo.front().bpm);

    TrackController trackController(*engine, *edit);
    WarpMarkerManager warpMarkers;
    ClipSynchronizer clipSync(*edit, trackController, warpMarkers);

    for (const auto& track : value.tracks)
        trackController.ensureTrackMapping(track.id, track.name);

    // --- the project, where the app's own sync reads it from ------------------
    //
    // PluginManager takes a TrackManager& and then does not use it: its sync
    // path reaches TrackManager::getInstance() forty-eight times between
    // PluginManagerSync and RackSyncManager. So a case's tracks go into the
    // singleton, which is what the app's device path reads, and a leg that
    // populated a local instance instead would watch that path read an empty
    // one and install nothing.
    //
    // The same arrangement the leg already has with ClipManager, and the idiom
    // a dozen other files in this binary use: fill it, run, clear it. Cleared
    // through a guard rather than at each return, because there are seven of
    // those and a case that left its tracks behind would be found by whichever
    // test ran next.
    struct TrackManagerUnwind {
        ~TrackManagerUnwind() {
            TrackManager::getInstance().clearAllTracks();
        }
    } trackManagerUnwind;

    auto& trackManager = TrackManager::getInstance();
    trackManager.clearAllTracks();

    for (const auto& track : value.tracks)
        trackManager.restoreTrack(track);

    // The master is not in that vector: TrackManager keeps it as its own
    // TrackInfo, which getTrack returns for MASTER_TRACK_ID.
    if (auto* master = trackManager.getTrack(MASTER_TRACK_ID))
        *master = value.master;

    // --- the mixer ------------------------------------------------------------
    //
    // The same four calls AudioBridge::trackPropertyChanged makes, in the order
    // it makes them. Mute and solo go straight to the te::AudioTrack there, and
    // volume and pan through AudioBridgeMixer, which is a pass-through to these
    // exact TrackController functions. Not a second mixer written for the
    // harness: the sync layer is part of what is being validated, and a fader
    // that reaches Tracktion wrong is wrong for the same user.

    for (const auto& track : value.tracks) {
        if (auto* audioTrack = trackController.getAudioTrack(track.id)) {
            audioTrack->setMute(track.muted);
            audioTrack->setSolo(track.soloed);
        }

        trackController.setTrackVolume(track.id, track.volume);
        trackController.setTrackPan(track.id, track.pan);
    }

    // Master, resolved the way AudioBridge::masterChannelChanged resolves it: a
    // muted master is a volume of zero rather than a flag of its own, and a
    // volume of zero is -100 dB rather than silence, because that is where the
    // plugin's own range floors.
    if (auto masterPlugin = edit->getMasterVolumePlugin()) {
        const auto volume = value.master.muted ? 0.0f : value.master.volume;
        masterPlugin->setVolumeDb(volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f);
        masterPlugin->setPan(value.master.pan);
    }

    // --- the devices and the racks -------------------------------------------
    //
    // The app's own device path, driven rather than copied (#2174). Every
    // device the model declares is created the way the app creates it, in the
    // segment and at the position the model puts it, racks and their inner
    // plugins and multi-out wrappers included, because all of that is what
    // PluginManager::syncAllPlugins does with a project.
    //
    // This is the boundary the corpus refused to cross until now, and the
    // refusal was right while the alternative was writing plugins straight into
    // the leg: a second sync is something that can agree with itself while both
    // engines are wrong. What makes crossing it safe is that this is not a
    // second sync. It is the first one, called.
    //
    // Its PluginManager is built over this render's own Edit rather than taken
    // from an AudioBridge, because the Edit is this render's: the constructor
    // is member initialisation and nothing else, and the manager is destroyed
    // with the render that made it.

    PluginWindowBridge pluginWindows;
    TransportStateManager transportState;
    PluginManager pluginManager(*engine, *edit, trackController, pluginWindows, transportState,
                                TrackManager::getInstance());
    auto& rackSync = pluginManager.getRackSyncManager();

    pluginManager.syncAllPlugins();

    /// Where a link's target lives, answered by the manager that installed it.
    ///
    /// The app's own lookup rather than a map this file fills: a target is a
    /// path, and which plugin a path names is exactly what PluginManager knows.
    class SyncedPluginLookup final : public magda::TargetPluginLookup {
      public:
        explicit SyncedPluginLookup(PluginManager& manager) : manager_(manager) {}

        te::Plugin* getPlugin(const magda::ChainNodePath& path) const override {
            return manager_.getPlugin(path).get();
        }

      private:
        PluginManager& manager_;
    } lookup(pluginManager);

    // The gain devices this project has, by the path that addresses them, with
    // the plugin the sync installed for each.
    //
    // Still collected, because the automation bake and the host write below
    // both address a parameter and the corpus's gain is the only device whose
    // parameter either of them knows. Resolved through the manager rather than
    // recorded while installing, which is what lets the walk be over the model
    // alone: a device inside a rack is found the same way one on a track is.
    std::map<ChainNodePath, GainPlugin*> gains;
    std::map<ChainNodePath, const magda::DeviceInfo*> gainDevices;

    // A rack's devices, down through nested racks, under the path a link's
    // target carries. Rebuilt on the way down rather than derived from the
    // device id, because an id is unique within a chain segment and not across
    // the hierarchy (#1899).
    std::function<void(const magda::RackInfo&, const ChainNodePath&)> collectRackGains =
        [&](const magda::RackInfo& rack, const ChainNodePath& rackPath) {
            for (const auto& chain : rack.chains) {
                const auto chainPath = rackPath.withChain(chain.id);
                for (const auto& element : chain.elements) {
                    if (magda::isRack(element)) {
                        const auto& nested = magda::getRack(element);
                        collectRackGains(nested, chainPath.withRack(nested.id));
                        continue;
                    }

                    const auto& device = magda::getDevice(element);
                    if (!isGainDevice(device))
                        continue;

                    const auto path = chainPath.withDevice(device.id);
                    if (auto* gain =
                            dynamic_cast<GainPlugin*>(pluginManager.getPlugin(path).get())) {
                        gains[path] = gain;
                        gainDevices[path] = &device;
                    }
                }
            }
        };

    // A track's own list is not a chain path: a top-level device is addressed
    // by topLevelDevice() rather than by a Device step under the track, and the
    // two do not compare equal.
    for (const auto& track : value.tracks)
        for (const auto& element : track.chain.fxChainElements) {
            if (magda::isRack(element)) {
                const auto& rack = magda::getRack(element);
                collectRackGains(rack, ChainNodePath::rack(track.id, rack.id));
                continue;
            }

            const auto& device = magda::getDevice(element);
            if (!isGainDevice(device))
                continue;

            const auto path = ChainNodePath::topLevelDevice(track.id, device.id);
            if (auto* gain = dynamic_cast<GainPlugin*>(pluginManager.getPlugin(path).get())) {
                gains[path] = gain;
                gainDevices[path] = &device;
            }
        }

    // The racks come down with the render that built them, before the Edit
    // does. A RackType outliving its plugins unwinds TE's bookkeeping in the
    // wrong order, the way the modulation teardown below has to for the same
    // reason.
    struct RackUnwind {
        RackSyncManager& sync;
        ~RackUnwind() {
            sync.clear();
        }
    } rackUnwind{rackSync};

    // What unwinds the curves this render bakes.
    //
    // A guard rather than loops at the end, because a proxy that never arrives
    // returns from the middle of the wait below. The modulation half of this is
    // gone with the section that built it: the sync owns those now, and
    // PluginManager takes them down with itself.
    struct Unwind {
        std::vector<te::AutomatableParameter*> bakedParams;

        ~Unwind() {
            for (auto* param : bakedParams) {
                param->getCurve().clear(nullptr);
                param->updateStream();
            }
        }
    } unwind;

    auto& bakedParams = unwind.bakedParams;

    // --- automation ----------------------------------------------------------
    //
    // Baked through the same emission the app's playback engine uses
    // (AutomationBake.hpp): a second bake here would be a corpus agreeing with
    // itself about what a step means.
    //
    // Device parameters only. Every other target resolves through
    // ControlTargetResolver, which wants a PluginManager -- the second sync
    // this corpus refuses, and the boundary sends stop at too (#1892).

    for (const auto& lane : value.lanes) {
        if (lane.target.kind != ControlTarget::Kind::PluginParam) {
            result.failure = "a lane plays over a target this leg cannot resolve";
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }

        const auto found = gains.find(lane.target.devicePath);
        if (found == gains.end() || lane.target.paramIndex != kGainParamIndex) {
            result.failure = "a lane plays over a parameter this project does not have";
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }

        auto* param = found->second->gain;
        auto& curve = param->getCurve();
        curve.clear(nullptr);

        const auto getClip = [&](AutomationClipId clipId) -> const AutomationClipInfo* {
            for (const auto& clip : value.automationClips)
                if (clip.id == clipId)
                    return &clip;
            return nullptr;
        };

        // Zero to one on both sides, so the lane's normalised position is
        // already what the parameter stores (NullDiffGain.hpp).
        bakeLaneIntoCurve(
            curve, lane, getClip,
            [&](double beat) { return magda::automation::laneValueAtBeat(lane, getClip, beat); },
            [](double normalized) { return juce::jlimit(0.0f, 1.0f, (float)normalized); });

        param->updateStream();
        bakedParams.push_back(param);
    }

    // --- modifiers and macros ------------------------------------------------
    //
    // Nothing here. The sync above wires them, at every scope the model has,
    // because PluginManager::syncTrackPlugins calls syncDeviceModifiers with
    // the track's modifier and macro lists (#2174).
    //
    // This file used to drive ModifierSyncWalker itself, at the two scopes a
    // track can carry without a rack, and refuse the rest out loud: rack scope
    // wanted a te::RackType only RackSyncManager builds, and a link whose
    // target this leg had no plugin for would have been wired to nothing while
    // the native table resolved it and modulated. Both refusals were about
    // devices this leg did not install. It installs them now, so they are gone
    // rather than relaxed -- the difference matters, because a refusal removed
    // while its cause remains is how a modulated render comes to be compared
    // against an unmodulated one and called a null.

    // --- the host write ------------------------------------------------------
    //
    // Last, and the ordering is the case rather than a detail: a stored value
    // arrives at a parameter that already has whatever the project gave it.
    //
    // setParameterFromHost because that is the rule for a host write, not
    // because it is what makes these cases pass: the fork's guard is a runtime
    // condition, so a write made before the render starts goes through either
    // call. The corpus was run both ways to find that out.
    for (const auto& [path, device] : gainDevices) {
        const auto found = gains.find(path);
        if (found == gains.end())
            continue;

        const auto* info = device->parameters.empty() ? nullptr : &device->parameters.front();
        found->second->gain->setParameterFromHost(
            info == nullptr ? kGainDefault : info->currentValue, juce::dontSendNotification);
    }

    // The capture devices, where the plan on the other side has them. Inserted
    // straight into each track's list: the model device stands for a synth, and
    // what is compared is what would reach it.
    //
    // One per track that consumes MIDI, not one on the first track. The native
    // leg binds a capture to every Device op the plan emits and aggregates them
    // all, so anything less here is asymmetric: an instrument on the second
    // track would give the incumbent an empty stream against a full one, and a
    // project with two instrument tracks would compare one against both. That
    // was invisible while a case was either MIDI or audio and every MIDI case
    // had one track; it stops being invisible the moment a project has an
    // instrument track beside audio tracks, which is what this slice enables.
    //
    // Which tracks those are is the compiler's own answer (chainConsumesMidi)
    // rather than a second opinion written here. Deciding it separately is how
    // the two would drift the first time a device type was added.
    // Paired with the track each stands on, so the streams can be compared where
    // they were received rather than only in aggregate.
    std::vector<std::pair<TrackId, MidiCapturePlugin*>> captures;
    if (value.capturesMidi()) {
        if (!captureDeviceRegistered) {
            result.failure = "the capture device could not be registered";
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }

        for (const auto& track : value.tracks) {
            if (!magda::engine::chainConsumesMidi(track))
                continue;

            auto* audioTrack = trackController.getAudioTrack(track.id);
            if (audioTrack == nullptr)
                continue;

            juce::ValueTree state(te::IDs::PLUGIN);
            state.setProperty(te::IDs::type, kCapturePluginId, nullptr);

            auto plugin = edit->getPluginCache().createNewPlugin(state);
            if (auto* capture = dynamic_cast<MidiCapturePlugin*>(plugin.get())) {
                audioTrack->pluginList.insertPlugin(plugin, 0, nullptr);
                captures.emplace_back(track.id, capture);
            }
        }

        if (captures.empty()) {
            result.failure = "no capture device could be placed on a MIDI-consuming track";
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }
    }

    ClipManager::getInstance().clearAllClips();
    for (const auto& clip : value.clips)
        ClipManager::getInstance().restoreClip(clip);
    for (const auto& clip : value.clips)
        clipSync.syncClipToEngine(clip.id);

    // --- wait for the proxies -----------------------------------------------

    const auto started = juce::Time::getMillisecondCounter();
    auto waited = 0;
    while (!proxiesReady(*engine, *edit, waited)) {
        result.proxiesWaitedFor = std::max(result.proxiesWaitedFor, waited);
        pumpMessageThread(10);

        result.waitMilliseconds = static_cast<int>(juce::Time::getMillisecondCounter() - started);
        if (result.waitMilliseconds > kProxyTimeoutMs) {
            result.failure = "proxy not ready after " + std::to_string(kProxyTimeoutMs) + " ms";
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }
    }

    // --- render --------------------------------------------------------------

    const auto startSeconds =
        edit->tempoSequence.toTime(te::BeatPosition::fromBeats(value.startBeat)).inSeconds();
    const auto endSeconds =
        edit->tempoSequence.toTime(te::BeatPosition::fromBeats(value.endBeat)).inSeconds();

    juce::TemporaryFile destination(".wav");

    te::Renderer::Parameters params(*edit);
    params.destFile = destination.getFile();
    params.audioFormat = engine->getAudioFileFormatManager().getWavFormat();

    // Float, and the corpus depends on it: sixteen bits would put quantisation
    // noise twenty-four decibels above the floor and every case would be
    // measuring the file format. Dithering is off by default and named anyway,
    // because it would put noise there too and it would be different noise on
    // every run.
    params.bitDepth = 32;
    params.ditheringEnabled = false;

    params.sampleRateForAudio = value.sampleRate;
    params.blockSizeForAudio = value.blockSize;
    params.time = te::TimeRange(te::TimePosition::fromSeconds(startSeconds),
                                te::TimePosition::fromSeconds(endSeconds));
    params.tracksToDo = te::toBitSet(te::getAllTracks(*edit));
    params.usePlugins = true;
    params.useMasterPlugins = true;
    params.checkNodesForAudio = false;
    params.realTimeRender = false;

    prepareEditForOfflineRender(*edit);

    {
        std::atomic<float> progress{0.0f};
        te::Renderer::RenderTask task("MAGDA Null Diff", params, &progress, nullptr);

        for (;;) {
            const auto status = task.runJob();
            if (status == juce::ThreadPoolJob::jobHasFinished)
                break;
            if (status != juce::ThreadPoolJob::jobNeedsRunningAgain) {
                result.failure = task.errorMessage.isNotEmpty()
                                     ? task.errorMessage.toStdString()
                                     : std::string("the render task failed");
                break;
            }
        }

        if (result.failure.empty() && task.errorMessage.isNotEmpty())
            result.failure = task.errorMessage.toStdString();
    }

    if (result.failure.empty()) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(destination.getFile()));

        if (reader == nullptr) {
            result.failure = "the render produced nothing that could be read back";
        } else {
            // Checked rather than trusted. A parameter that quietly did not
            // apply is exactly the failure this guards, and it would look like
            // an engine that is 96 dB wrong everywhere.
            result.renderedInFloat = reader->usesFloatingPointData;

            result.audio.setSize(static_cast<int>(reader->numChannels),
                                 static_cast<int>(reader->lengthInSamples));
            reader->read(&result.audio, 0, static_cast<int>(reader->lengthInSamples), 0, true,
                         true);
        }
    }

    // Every capture's stream, in timeline order, which is how the native leg
    // aggregates its own. Sorted rather than concatenated: two instrument tracks
    // would otherwise hand over one track's whole timeline followed by the
    // other's, and the comparison reads a stream as a function of time.
    for (const auto& [trackId, capture] : captures) {
        auto& perTrack = result.midiByTrack[trackId];
        perTrack.insert(perTrack.end(), capture->captured.begin(), capture->captured.end());
        result.midi.insert(result.midi.end(), capture->captured.begin(), capture->captured.end());
    }

    std::stable_sort(result.midi.begin(), result.midi.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    // --- put the process back the way it was ---------------------------------
    //
    // A job that outlives its case reads a source file the next case has
    // already deleted, which the app's own reverse path had to learn as well.
    engine->getBackgroundJobs().getPool().removeAllJobs(false, 10000);
    pumpMessageThread(50);

    ClipManager::getInstance().clearAllClips();
    ProjectManager::getInstance().setTempo(previousTempo);

    return result;
}

}  // namespace magda::nulldiff
