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
#include "magda/daw/audio/TrackController.hpp"
#include "magda/daw/audio/WarpMarkerManager.hpp"
#include "magda/daw/audio/automation/AutomationBake.hpp"
#include "magda/daw/audio/modifiers/ModifierSync.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "magda/daw/audio/session/ClipSynchronizer.hpp"
#include "magda/daw/core/AutomationCurve.hpp"
#include "magda/daw/core/ChainNode.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
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

/// The property the gain's value is stored under, and the one the parameter is
/// attached to. Its own name rather than one of TE's IDs, because nothing else
/// in the Edit means the same thing by it.
const juce::Identifier kGainProperty("nulldiffGain");

/// The incumbent's half of the contract in NullDiffGain.hpp.
///
/// One parameter, applied as a plain multiply. No smoothing and no ramp: a
/// smoother is state, state is memory, and two smoothers primed differently
/// never agree, so a residual would be measuring the device rather than the
/// parameter.
///
/// The value is read once, at the top of the block, which is what an
/// AutomatableParameter is: TE settles every parameter from its automation and
/// its modifiers before the graph runs and holds it for the block. The engine's
/// twin reads per sample instead. Where the two could differ, which is inside
/// one block of a step, the cases play silence.
class GainPlugin final : public te::Plugin {
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

void registerGainDevice(magda::daw::audio::InternalPluginRegistry& registry) {
    magda::daw::audio::InternalPluginSpec spec;
    spec.pluginId = kGainPluginId;
    spec.displayName = GainPlugin::getPluginName();
    spec.browserCategory = "Utility";
    spec.description = "A gain with one parameter, for the null-diff corpus.";
    spec.createMode = magda::daw::audio::InternalPluginCreateMode::FreshValueTree;
    spec.canCreateDetached = false;
    spec.canCreateOnTrack = false;
    spec.showInBrowser = false;
    spec.matchesPlugin = [](magda::daw::audio::DevicePluginRef plugin) {
        return dynamic_cast<GainPlugin*>(
                   magda::daw::audio::tracktion_adapter::pluginFromRef(plugin)) != nullptr;
    };
    spec.createPlugin = [](const magda::daw::audio::DevicePluginCreationContext& context) {
        return magda::daw::audio::tracktion_adapter::pluginHandle(
            new GainPlugin(magda::daw::audio::tracktion_adapter::creationInfo(context)));
    };

    registry.registerPlugin(spec);
}

const bool gainDeviceRegistered = magda::daw::audio::registerDevicePack(registerGainDevice);

/// Where a link's target lives, for the walker that wires modifiers and macros.
///
/// The app's own lookup is PluginManager's, which resolves any device anywhere
/// in the chain hierarchy. This resolves the only devices this leg installs, by
/// the id their path names. It is a lookup rather than a sync: the wiring is
/// still ModifierSyncWalker's, which is the point -- a second walker written
/// for the harness would be a harness agreeing with itself about what a
/// modifier does.
class GainPluginLookup final : public magda::TargetPluginLookup {
  public:
    void add(magda::DeviceId id, te::Plugin* plugin) {
        plugins_[id] = plugin;
    }

    te::Plugin* getPlugin(const magda::ChainNodePath& path) const override {
        const auto found = plugins_.find(path.getDeviceId());
        return found == plugins_.end() ? nullptr : found->second;
    }

  private:
    std::map<magda::DeviceId, te::Plugin*> plugins_;
};

/// Everything one scope's modifiers and macros need to live in, for as long as
/// the Edit does.
///
/// The walker writes into references it is handed, because in the app these
/// live on PluginManager's synced-device records. Here they live for the length
/// of a render, and they are torn down before the Edit is: a macro parameter
/// outliving its list, or a modifier outliving the track it was inserted on, is
/// a crash in TE's own bookkeeping rather than a leak.
struct ScopeModifiers {
    std::map<magda::ModId, te::Modifier::Ptr> modifiers;
    std::map<magda::ModId, std::unique_ptr<magda::CurveSnapshotHolder>> curveSnapshots;
    std::map<int, te::MacroParameter*> macroParams;

    magda::ModifierSyncState state() {
        return magda::ModifierSyncState{modifiers, curveSnapshots, macroParams};
    }
};

/// Whether anything in @p node is worth building TE state for. A track with the
/// default sixteen empty macros and no modifiers gets none, which keeps every
/// case that is not about modulation exactly as it was.
bool carriesModulation(const magda::ConstChainNode& node) {
    if (node.mods != nullptr)
        for (const auto& mod : *node.mods)
            if (mod.enabled && mod.isLinked())
                return true;

    if (node.macros != nullptr)
        for (const auto& macro : *node.macros)
            if (macro.isLinked())
                return true;

    return false;
}

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

    // --- the device with a parameter -----------------------------------------
    //
    // One GainPlugin per model gain device, in chain order, at the head of the
    // track's plugin list so it sits where the plan puts its Device op: ahead
    // of the fader, behind nothing. The corpus installs no other device, and
    // every other device in the model is still a thing neither leg runs.
    //
    // This is the first case of the corpus running a device under both engines
    // at all (#2123). Until it did, every parameter, every curve, every
    // modifier and every macro in #1891 had been judged only by tests written
    // beside them.

    GainPluginLookup lookup;
    std::map<DeviceId, GainPlugin*> gains;

    for (const auto& track : value.tracks) {
        auto* audioTrack = trackController.getAudioTrack(track.id);
        if (audioTrack == nullptr)
            continue;

        auto slot = 0;
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;

            const auto& device = magda::getDevice(element);
            if (!isGainDevice(device))
                continue;

            juce::ValueTree state(te::IDs::PLUGIN);
            state.setProperty(te::IDs::type, kGainPluginId, nullptr);

            auto plugin = edit->getPluginCache().createNewPlugin(state);
            auto* gain = dynamic_cast<GainPlugin*>(plugin.get());
            if (gain == nullptr) {
                result.failure = "the gain device could not be created";
                ClipManager::getInstance().clearAllClips();
                ProjectManager::getInstance().setTempo(previousTempo);
                return result;
            }

            audioTrack->pluginList.insertPlugin(plugin, slot++, nullptr);

            lookup.add(device.id, plugin.get());
            gains[device.id] = gain;
        }
    }

    // Where the modulation this render builds lives, and what unwinds it.
    //
    // Declared here, ahead of both sections that fill them, because the order
    // they are destroyed in is load bearing. The unwinder's teardown reaches
    // into the scope maps, so it has to run while they are still alive, and
    // every one of them has to be gone before the Edit is: a macro parameter
    // destroyed while its list is already tearing down, or one holding a
    // populated curve, unwinds TE's per-edit-element bookkeeping in the wrong
    // order. The app's playback engine clears its own baked curves in its
    // destructor for the same reason.
    //
    // A guard rather than a pair of loops at the end, because this function has
    // an exit that does not reach the end: a proxy that never arrives returns
    // from the middle of the wait below.
    std::map<TrackId, ScopeModifiers> trackModulation;
    std::map<DeviceId, ScopeModifiers> deviceModulation;
    std::vector<std::unique_ptr<magda::CurveSnapshotHolder>> deferredHolders;

    struct Unwind {
        std::vector<te::AutomatableParameter*> bakedParams;
        std::vector<std::function<void()>> modulation;

        ~Unwind() {
            for (auto* param : bakedParams) {
                param->getCurve().clear(nullptr);
                param->updateStream();
            }
            for (auto& tearDown : modulation)
                tearDown();
        }
    } unwind;

    auto& bakedParams = unwind.bakedParams;
    auto& tearDownModulation = unwind.modulation;

    // --- automation ----------------------------------------------------------
    //
    // Baked into the fork's own curve on the parameter the lane names, through
    // the same emission the app's playback engine uses (AutomationBake.hpp): a
    // second bake written here would be a corpus agreeing with itself about
    // what a step or a bezier means.
    //
    // Device parameters only. Every other target -- a fader, a send, a macro,
    // a modifier's rate -- resolves through ControlTargetResolver, which wants
    // a PluginManager and the device layer behind it, and that is the second
    // sync this corpus refuses to have. It is the same boundary sends stop at,
    // and it moves with #1892.

    for (const auto& lane : value.lanes) {
        if (lane.target.kind != ControlTarget::Kind::PluginParam) {
            result.failure = "a lane plays over a target this leg cannot resolve";
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(previousTempo);
            return result;
        }

        const auto found = gains.find(lane.target.devicePath.getDeviceId());
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

        // The gain parameter is zero to one on both sides, so a lane's
        // normalised position is already what the parameter stores and neither
        // leg converts. See NullDiffGain.hpp for why that is the contract
        // rather than a convenience.
        bakeLaneIntoCurve(
            curve, lane, getClip,
            [&](double beat) { return magda::automation::laneValueAtBeat(lane, getClip, beat); },
            [](double normalized) { return juce::jlimit(0.0f, 1.0f, (float)normalized); });

        param->updateStream();
        bakedParams.push_back(param);
    }

    // --- modifiers and macros ------------------------------------------------
    //
    // The app's own walker, at the two scopes a track can carry without a rack:
    // the track itself, and each top-level device. Both take the track's
    // modifier list and the track's macro list, which is exactly what
    // PluginManager::syncDeviceModifiers hands them for a non-instrument
    // device.
    //
    // Rack scope is the third the model has and is not here. A rack's
    // modifiers and macros live on a te::RackType that RackSyncManager builds
    // out of a PluginManager, so wiring one up would be the second sync the
    // corpus refuses; it moves with the rest of the routing graph in #1892.
    for (const auto& track : value.tracks) {
        auto* audioTrack = trackController.getAudioTrack(track.id);
        if (audioTrack == nullptr)
            continue;

        auto* modifierList = audioTrack->getModifierList();
        auto* macroList = &audioTrack->getMacroParameterListForWriting();

        // Every plugin the walker may have to scrub a stale assignment off.
        const auto forEachPlugin = [audioTrack](const std::function<void(te::Plugin*)>& visit) {
            for (auto* plugin : audioTrack->pluginList)
                visit(plugin);
        };

        const auto sync = [&](magda::ConstChainNode node, ScopeModifiers& scope) {
            magda::ModifierSyncContext ctx;
            ctx.modifierList = modifierList;
            ctx.macroList = macroList;
            ctx.lookup = &lookup;
            ctx.forEachScopePlugin = forEachPlugin;
            ctx.hasCrossTrackSidechain = false;

            auto state = scope.state();
            magda::ModifierSyncWalker::syncStructure(node, ctx, state, deferredHolders);

            // Torn down through the same walker, handed a node with nothing on
            // it, which is the path a bypassed device already takes. The macro
            // parameters and the modifiers have to be gone before the Edit is:
            // a macro outliving its list unwinds TE's own bookkeeping in the
            // wrong order, which the app's playback engine had to learn too.
            tearDownModulation.emplace_back([&scope, ctx, this_node = node]() mutable {
                this_node.mods = nullptr;
                this_node.macros = nullptr;
                std::vector<std::unique_ptr<magda::CurveSnapshotHolder>> discarded;
                auto state = scope.state();
                magda::ModifierSyncWalker::syncStructure(this_node, ctx, state, discarded);
            });
        };

        magda::ConstChainNode trackNode;
        trackNode.scope = magda::ChainScope::Track;
        trackNode.trackId = track.id;
        trackNode.mods = &track.mods;
        trackNode.macros = &track.macros;

        if (carriesModulation(trackNode))
            sync(trackNode, trackModulation[track.id]);

        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;

            const auto& device = magda::getDevice(element);

            magda::ConstChainNode deviceNode;
            deviceNode.scope = magda::ChainScope::Device;
            deviceNode.trackId = track.id;
            deviceNode.deviceId = device.id;
            deviceNode.mods = &device.mods;
            deviceNode.macros = &device.macros;
            deviceNode.params = &device.parameters;

            if (carriesModulation(deviceNode))
                sync(deviceNode, deviceModulation[device.id]);
        }
    }

    // --- the host write ------------------------------------------------------
    //
    // Last, and that ordering is the case rather than an implementation detail.
    // A stored value is what a knob move, a control surface, a preset load or a
    // restored project sets, and every one of those arrives at a parameter that
    // already has whatever automation and modifiers the project gave it. Writing
    // it before the modifier was attached would be writing to a parameter
    // nothing was modulating, which is the one arrangement where the bug this
    // asserts against cannot happen.
    //
    // setParameterFromHost rather than setParameter, because that is the rule
    // for a host write and this leg is the app's paths rather than a second set
    // of them. What it is not is what makes the host-write cases pass: the
    // fork's guard is a runtime condition (setParameter returns early only
    // while currentModifierValue is non-zero), so a write made before the
    // render starts goes through either call. The corpus was run both ways to
    // find that out, and the case's own comment says what it does and does not
    // therefore pin.
    for (const auto& track : value.tracks) {
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;

            const auto& device = magda::getDevice(element);
            const auto found = gains.find(device.id);
            if (found == gains.end())
                continue;

            const auto* info = device.parameters.empty() ? nullptr : &device.parameters.front();
            found->second->gain->setParameterFromHost(
                info == nullptr ? kGainDefault : info->currentValue, juce::dontSendNotification);
        }
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
