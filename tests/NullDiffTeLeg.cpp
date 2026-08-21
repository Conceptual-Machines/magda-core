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

/// The property the gain's value is stored under.
const juce::Identifier kGainProperty("nulldiffGain");

/// The incumbent's half of the contract in NullDiffGain.hpp.
///
/// One parameter, a plain multiply, no smoothing. The value is read once at the
/// top of the block, which is what an AutomatableParameter is; the engine's twin
/// reads per sample, and the cases play silence wherever that could differ.
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
/// A lookup rather than a sync: the wiring is still ModifierSyncWalker's, which
/// is the point. The app's own lookup is PluginManager's; this resolves the
/// only devices this leg installs.
///
/// Keyed by the whole path, not by the device id in it. An id is unique within
/// a chain segment and not across the hierarchy (#1899), so matching on the
/// number would wire a rack-inner or post-FX target onto the top-level plugin.
class GainPluginLookup final : public magda::TargetPluginLookup {
  public:
    void add(const magda::ChainNodePath& path, te::Plugin* plugin) {
        plugins_[path] = plugin;
    }

    te::Plugin* getPlugin(const magda::ChainNodePath& path) const override {
        const auto found = plugins_.find(path);
        return found == plugins_.end() ? nullptr : found->second;
    }

  private:
    std::map<magda::ChainNodePath, te::Plugin*> plugins_;
};

/// Where one scope's modifiers and macros live. The walker writes into
/// references it is handed; in the app these sit on PluginManager's synced
/// device records, here they last the length of a render.
struct ScopeModifiers {
    std::map<magda::ModId, te::Modifier::Ptr> modifiers;
    std::map<magda::ModId, std::unique_ptr<magda::CurveSnapshotHolder>> curveSnapshots;
    std::map<int, te::MacroParameter*> macroParams;

    magda::ModifierSyncState state() {
        return magda::ModifierSyncState{modifiers, curveSnapshots, macroParams};
    }
};

/// Whether @p node is worth building TE state for. A track with the default
/// sixteen empty macros and no modifiers gets none.
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
    // track's plugin list so it sits where the plan puts its Device op. The
    // first device the corpus runs under both engines at all (#2123); every
    // other device in the model is still a thing neither leg runs.

    GainPluginLookup lookup;
    std::map<ChainNodePath, GainPlugin*> gains;

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

            // The path the model addresses it by, which is what a lane's target
            // and a link's target both carry.
            const auto path = ChainNodePath::topLevelDevice(track.id, device.id);
            lookup.add(path, plugin.get());
            gains[path] = gain;
        }
    }

    // Where the modulation this render builds lives, and what unwinds it.
    //
    // Declared ahead of both sections that fill them because destruction order
    // is load bearing: the teardown reaches into the scope maps, and everything
    // has to be gone before the Edit is. A macro destroyed while its list is
    // already tearing down, or one holding a populated curve, unwinds TE's
    // bookkeeping in the wrong order.
    //
    // A guard rather than loops at the end, because a proxy that never arrives
    // returns from the middle of the wait below.
    std::map<TrackId, ScopeModifiers> trackModulation;
    std::map<ChainNodePath, ScopeModifiers> deviceModulation;
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
    // The app's own walker, at the two scopes a track can carry without a rack:
    // the track itself and each top-level device. Both take the track's
    // modifier and macro lists, which is what
    // PluginManager::syncDeviceModifiers hands them for a non-instrument
    // device. Rack scope needs a te::RackType only RackSyncManager builds, so
    // it is refused below rather than skipped (#1892).
    // What the walker would drop in silence, refused out loud first: it wires
    // nothing for a target this leg has no plugin for, while the native table
    // resolves it and modulates. That is a modulated render compared against an
    // unmodulated one, and where the target is inaudible it is a false null.
    const auto unwirable = [&](const ControlTarget& target, const ChainNodePath& scopePath,
                               const magda::ModArray& scopeMods) -> std::string {
        switch (target.kind) {
            case ControlTarget::Kind::PluginParam:
                if (gains.count(target.devicePath) == 0)
                    return "a device this leg does not install: " +
                           target.devicePath.toString().toStdString();
                return {};

            case ControlTarget::Kind::ModParam:
                // Same scope, asked of the path rather than of the id.
                // resolveSameScopeModParam looks the modifier up by id alone,
                // so a target naming another scope is wired to whichever local
                // modifier shares its number while the native table resolves
                // the whole path to a different parameter. The two then differ
                // over a link both of them think they honoured.
                if (target.devicePath != scopePath)
                    return "a modifier in another scope (" +
                           target.devicePath.toString().toStdString() +
                           "), which the walker resolves by id alone";

                // Rate is the only parameter the native table gives a modifier;
                // the walker also resolves index 1 to depth.
                if (target.modParamIndex != 0)
                    return "a modifier parameter that is not its Rate";

                for (const auto& mod : scopeMods)
                    if (mod.id == target.modId && mod.enabled)
                        return {};
                return "a modifier this scope does not have, or one that is off";

            default:
                // A fader, a send, a macro. The native table carries the first
                // two; the walker wires none of them.
                return std::string("a ") + magda::toString(target.kind) +
                       " target, which this leg cannot wire";
        }
    };

    const auto refuseUnwirableLinks = [&](const magda::ConstChainNode& node,
                                          const ChainNodePath& scopePath) -> std::string {
        static const magda::ModArray noMods;
        const auto& mods = node.mods == nullptr ? noMods : *node.mods;

        for (const auto& mod : mods)
            if (mod.enabled)
                for (const auto& link : mod.links)
                    if (link.enabled && link.isValid())
                        if (auto why = unwirable(link.target, scopePath, mods); !why.empty())
                            return "a modifier links to " + why;

        if (node.macros != nullptr)
            for (const auto& macro : *node.macros)
                for (const auto& link : macro.links)
                    if (link.target.isValid())
                        if (auto why = unwirable(link.target, scopePath, mods); !why.empty())
                            return "a macro links to " + why;

        return {};
    };

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

            // Torn down through the same walker handed an empty node, which
            // is the path a bypassed device already takes.
            tearDownModulation.emplace_back([&scope, ctx, this_node = node]() mutable {
                this_node.mods = nullptr;
                this_node.macros = nullptr;
                std::vector<std::unique_ptr<magda::CurveSnapshotHolder>> discarded;
                auto state = scope.state();
                magda::ModifierSyncWalker::syncStructure(this_node, ctx, state, discarded);
            });
        };

        const auto refuse = [&](const std::string& why) {
            result.failure = why;
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(previousTempo);
        };

        magda::ConstChainNode trackNode;
        trackNode.scope = magda::ChainScope::Track;
        trackNode.trackId = track.id;
        trackNode.mods = &track.mods;
        trackNode.macros = &track.macros;

        if (carriesModulation(trackNode)) {
            if (auto why = refuseUnwirableLinks(trackNode, ChainNodePath::trackLevel(track.id));
                !why.empty()) {
                refuse(why);
                return result;
            }
            sync(trackNode, trackModulation[track.id]);
        }

        // A rack's own modifiers and macros need a te::RackType this leg does
        // not build (#1892). Refused rather than skipped.
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isRack(element))
                continue;

            const auto& rack = magda::getRack(element);
            magda::ConstChainNode rackNode;
            rackNode.scope = magda::ChainScope::Rack;
            rackNode.mods = &rack.mods;
            rackNode.macros = &rack.macros;

            if (carriesModulation(rackNode)) {
                refuse("a rack carries modulation, which this leg cannot sync");
                return result;
            }
        }

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

            if (carriesModulation(deviceNode)) {
                const auto devicePath = ChainNodePath::topLevelDevice(track.id, device.id);

                if (auto why = refuseUnwirableLinks(deviceNode, devicePath); !why.empty()) {
                    refuse(why);
                    return result;
                }
                sync(deviceNode, deviceModulation[devicePath]);
            }
        }
    }

    // --- the host write ------------------------------------------------------
    //
    // Last, and the ordering is the case rather than a detail: a stored value
    // arrives at a parameter that already has whatever the project gave it.
    //
    // setParameterFromHost because that is the rule for a host write, not
    // because it is what makes these cases pass: the fork's guard is a runtime
    // condition, so a write made before the render starts goes through either
    // call. The corpus was run both ways to find that out.
    for (const auto& track : value.tracks) {
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;

            const auto& device = magda::getDevice(element);
            const auto found = gains.find(ChainNodePath::topLevelDevice(track.id, device.id));
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
