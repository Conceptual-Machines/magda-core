#include "NullDiffNativeLeg.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

#include "NullDiffGain.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipVoicePool.hpp"
#include "clip/GrooveTemplate.hpp"
#include "exec/OfflineRender.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "io/PrefetchThread.hpp"
#include "magda/daw/audio/plugin_manager/ExternalPluginState.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/core/ChainWalk.hpp"
#include "plan/PlanCompiler.hpp"
#include "transport/TempoMap.hpp"

namespace magda::nulldiff {

using namespace magda::engine;

namespace adapter = ::magda::daw::audio::engine_adapter;

namespace {

/// Opens the files a snapshot names, which is the one thing the engine leaves
/// to whoever is hosting it: a snapshot carries paths because that is what the
/// model has, and which formats exist is not the engine's business.
class WavFactory final : public AudioFileReaderFactory {
  public:
    WavFactory() {
        formats_.registerBasicFormats();
    }

    std::unique_ptr<AudioFileReader> open(const std::string& path) override {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats_.createReaderFor(juce::File(juce::String(path))));
        if (reader == nullptr)
            return nullptr;
        return std::make_unique<JuceAudioFileReader>(std::move(reader));
    }

  private:
    juce::AudioFormatManager formats_;
};

/// Records what reaches a track's chain, which is what the corpus compares.
///
/// Bound to the track's TrackMidiInput op: the merge of its clips, its live
/// input and anything routed in, before any device sees it. That is a property
/// of the track, so nothing about which devices are installed, how many there
/// are, whether they consume MIDI, or whether the chain is bypassed can change
/// where this reads.
///
/// It replaces a stand-in device that used to do this job. Reading a device
/// meant the harness had to work out which device stood for the track, and that
/// question has no good answer: two instruments record the same notes twice, an
/// audio effect at the head records the wrong thing or nothing, a track whose
/// MIDI is only routed elsewhere has a wired device that the incumbent never
/// captures. Those were all one mistake, which was observing the graph at a
/// point that belongs to the project rather than at the point being compared.
class ChainMidiTap final : public MidiTap {
  public:
    explicit ChainMidiTap(double sampleRate) : sampleRate_(sampleRate) {}

    void write(const juce::MidiBuffer& midi, const BlockInfo& block) override {
        // Where the block sits on the timeline, so a captured message carries a
        // position in the render rather than one inside a callback. Taken from
        // the block's own seconds, which the clock derives from a sample count
        // rather than accumulating, so this recovers that count exactly and
        // keeps doing so through the tail, where the timeline stands still.
        const auto blockStart =
            static_cast<std::int64_t>(std::llround(block.startSeconds * sampleRate_));

        for (const auto message : midi) {
            const auto data = message.getMessage();
            if (data.getRawDataSize() < 2 || data.getRawDataSize() > 3)
                continue;

            const auto* raw = data.getRawData();
            captured.push_back(
                {blockStart + message.samplePosition, static_cast<std::uint8_t>(raw[0]),
                 static_cast<std::uint8_t>(raw[1]),
                 data.getRawDataSize() > 2 ? static_cast<std::uint8_t>(raw[2]) : std::uint8_t{0}});
        }
    }

    MidiStream captured;

  private:
    double sampleRate_ = 44100.0;
};

/// Stands where the incumbent has no plugin at all.
///
/// The native leg has to bind something to every Device op or the executor
/// refuses the plan, but the incumbent instantiates none of the model's devices:
/// it inserts one capture on a MIDI-consuming track and nothing anywhere else.
/// A stand-in that cleared the buffer would therefore silence an audio track
/// that merely carries an effect, and the two legs would differ over a device
/// neither of them is really running. Doing nothing is what the incumbent does.
class Passthrough final : public EngineDevice {
  public:
    void process(DeviceBlock&) override {}
};

/// The one device the corpus runs under both engines (#2123).
///
/// Multiplies by parameter zero and nothing else, so what it renders is the
/// value of its own parameter. NullDiffGain.hpp has the contract.
///
/// Read per sample rather than once at the top of the block: a device reading
/// only the block's opening value would give up the invariance the segments
/// exist for and fail the block-size gate (#2078).
class GainDevice final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        const auto gain = block.params[kGainParamIndex];
        if (gain.empty())
            return;

        const auto numSamples = static_cast<int>(block.audio.getNumSamples());
        const auto numChannels = static_cast<int>(block.audio.getNumChannels());

        if (gain.isConstant()) {
            block.audio.multiplyBy(gain.value());
            return;
        }

        for (auto sample = 0; sample < numSamples; ++sample) {
            const auto value = gain.valueAt(sample);
            for (auto channel = 0; channel < numChannels; ++channel)
                block.audio.setSample(channel, sample,
                                      block.audio.getSample(channel, sample) * value);
        }
    }
};

/// The engine's half of the instrument contract in NullDiffGain.hpp (#2139).
///
/// One sample per note-on into pair 0, half of it into pair 1. The pairs the
/// plan gave it and no others: `extraOutputs` is one block per pair the device
/// declared, cleared on the way in, so a pair nothing opened is written and
/// dropped rather than skipped here.
class ImpulseSynthDevice final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        if (block.midiIn == nullptr)
            return;

        const auto numSamples = static_cast<int>(block.audio.getNumSamples());

        for (const auto metadata : *block.midiIn) {
            const auto message = metadata.getMessage();
            if (!message.isNoteOn())
                continue;

            const auto at = metadata.samplePosition;
            if (at < 0 || at >= numSamples)
                continue;

            const auto level = static_cast<float>(message.getVelocity()) / 127.0f;
            write(block.audio, at, level);

            for (auto& pair : block.extraOutputs)
                write(pair, at, level * kMultiOutSecondPairScale);
        }
    }

  private:
    static void write(juce::dsp::AudioBlock<float>& target, int sample, float level) {
        for (std::size_t channel = 0; channel < target.getNumChannels(); ++channel)
            target.setSample(static_cast<int>(channel), sample,
                             target.getSample(static_cast<int>(channel), sample) + level);
    }
};

/// Every device in @p elements and everything nested inside them, keyed in
/// @p segment.
///
/// Keyed by DeviceKey and not by DeviceId: an id is unique within a chain
/// segment and not across them (#1899), so a map keyed by the number alone
/// would let a post-FX device stand in for the FX device with the same one.
/// OpKey::deviceKey() carries the segment for that reason; this has to match.
///
/// The walk is the model's own (ChainWalk.hpp) rather than one written here,
/// entered with Pads::Enter. A Drum Grid's pads are chains of devices and
/// PlanCompiler::emitPadRack() emits an op for each of them, so a walk that
/// stopped at the grid would leave every plugin in every pad looked up and not
/// found -- bound to a stand-in, never resolved, and reported as nothing. Two
/// definitions of "every device in a project" is exactly the disagreement that
/// file exists to prevent.
void collectDevices(std::vector<magda::ChainElement>& elements,
                    const magda::ChainNodePath& parentPath, ChainSegment segment,
                    std::map<DeviceKey, magda::DeviceInfo*>& out) {
    magda::chain_walk::forEachDevice(
        elements, parentPath, magda::chain_walk::Pads::Enter,
        [&out, segment](magda::DeviceInfo& device, const magda::ChainNodePath&) {
            out[DeviceKey{segment, device.id}] = &device;
        });
}

/// The same for a flat stage, whose elements are devices rather than a tree.
///
/// Flat means no racks; it does not mean no pads, so a device that carries them
/// is descended into the same way.
void collectFlatDevices(std::vector<magda::PostFxChainElement>& elements,
                        const magda::ChainNodePath& parentPath, ChainSegment segment,
                        std::map<DeviceKey, magda::DeviceInfo*>& out) {
    for (auto& element : elements) {
        out[DeviceKey{segment, element.device.id}] = &element.device;

        if (!element.device.pads)
            continue;

        for (auto& pad : element.device.pads->chains)
            collectDevices(pad.elements, parentPath, segment, out);
    }
}

/// Every device the plan can emit an op for, by the key the op carries.
///
/// The master's chain is walked with the rest. It is as much of the project as
/// any track's, the compiler emits Device ops for it, and a master left out of
/// this map is a master limiter that silently becomes a stand-in: the case
/// still renders, still compares, and is measuring a project without its
/// master chain in it.
std::map<DeviceKey, magda::DeviceInfo*> devicesIn(std::vector<TrackInfo>& tracks,
                                                  TrackInfo& master) {
    std::map<DeviceKey, magda::DeviceInfo*> devices;

    const auto collectTrack = [&devices](TrackInfo& track) {
        const auto trackPath = magda::ChainNodePath::trackLevel(track.id);
        collectDevices(track.chain.fxChainElements, trackPath, ChainSegment::Fx, devices);
        collectFlatDevices(track.chain.postFxChainElements, trackPath, ChainSegment::PostFx,
                           devices);
        collectFlatDevices(track.chain.mixerAnalysisElements, trackPath,
                           ChainSegment::MixerAnalysis, devices);
    };

    for (auto& track : tracks)
        collectTrack(track);

    collectTrack(master);
    return devices;
}

/// Whether @p device is a plugin somebody else shipped, which is the one kind
/// this leg cannot build from a catalog and has to find on the machine.
bool isExternalDevice(const magda::DeviceInfo& device) {
    return device.format != magda::PluginFormat::Internal;
}

/**
 * @brief Correct every external device against the scan, before the plan reads it.
 *
 * The plan is compiled from the model, and for an external plugin two of the
 * things it compiles from are the project's guesses rather than facts: the
 * effect/instrument role, which decides whether the device is routed MIDI at
 * all, and the cached capability flags. A project imported from another host
 * carries whatever that host said, and #2252's whole point is that resolution
 * is entitled to correct it.
 *
 * So resolution happens here, once, against a copy of the model, and both the
 * plan and the device creation below read the corrected one. A failure is left
 * alone rather than reported: the creation below asks the same question and
 * reports the same answer, and one diagnostic per absent plugin is what a
 * reader wants.
 */
std::map<DeviceKey, std::string> resolveExternalDevices(
    std::vector<TrackInfo>& tracks, TrackInfo& master,
    const adapter::ExternalPluginServices& services) {
    std::map<DeviceKey, std::string> identities;

    for (auto& [key, device] : devicesIn(tracks, master)) {
        if (!isExternalDevice(*device))
            continue;

        auto resolved = adapter::resolveEngineExternalPlugin(*device, services);
        if (!resolved)
            continue;

        // What the scan actually matched, kept for the report. The project's
        // own name for the device is not the answer: a project names what its
        // author called the slot, and what the render ran is whichever build of
        // whichever plugin this machine matched it to.
        const auto& description = resolved.description;
        identities.emplace(key, (description.name + " " +
                                 (description.version.isNotEmpty() ? description.version
                                                                   : juce::String("no version")) +
                                 " (" + description.pluginFormatName + ")")
                                    .toStdString());

        *device = std::move(resolved.planDevice);
    }

    return identities;
}

/// The keys of the Device ops @p plan emits, which is which of a project's
/// devices actually render.
std::set<DeviceKey> deviceKeysIn(const RenderPlan& plan) {
    std::set<DeviceKey> keys;

    for (const auto& op : plan.ops)
        if (op.kind == OpKind::Device)
            keys.insert(op.key.deviceKey());

    return keys;
}

/**
 * @brief Create every external plugin the plan reaches, before the plan is final.
 *
 * Scan metadata is not the whole answer about an external device. A plugin's own
 * saved state is allowed to change its topology -- a sampler whose patch turns
 * its drum outs on, a compressor whose patch enables its sidechain, an
 * instrument that is stereo for one program and mono for another -- and
 * adaptExternalPluginInstance() reads the live bus counts and MIDI capabilities
 * back only after the chunk has been applied, which is why it hands a
 * resolvedDevice back at all.
 *
 * Those facts have to reach the model before PlanCompiler freezes port widths
 * and MIDI edges from it. So the instances are made here, their corrections
 * written into @p tracks and @p master, and the plan compiled from the result;
 * the same instances are then bound, rather than a second set made from the
 * corrected model.
 *
 * @p reached is the first compile's answer to which devices render, and it is
 * all the first compile is used for. A device on a bypassed chain is not
 * instantiated and not reported, because a project does not go without a plugin
 * it was never going to run.
 */
std::map<DeviceKey, adapter::ExternalDeviceResult> createExternalDevices(
    std::vector<TrackInfo>& tracks, TrackInfo& master, const std::set<DeviceKey>& reached,
    const adapter::ExternalPluginServices& services) {
    std::map<DeviceKey, adapter::ExternalDeviceResult> created;

    for (auto& [key, device] : devicesIn(tracks, master)) {
        if (!isExternalDevice(*device) || !reached.contains(key))
            continue;

        // Built for an offline render, which is what this leg is; see the
        // binding loop for why that matters on both sides.
        auto result = adapter::createEngineExternalDevice(*device, services,
                                                          /*offlineRender=*/true);

        if (result.device != nullptr) {
            // The live topology first, then what the restore left behind on top
            // of it. Both are read from this model by what comes next: the
            // compiler for the widths and the roles, the value layer for the
            // parameters.
            if (result.resolvedDevice.has_value())
                *device = *result.resolvedDevice;

            magda::applyRestoredParameters(*device, result.restoredParameters);
        }

        created.emplace(key, std::move(result));
    }

    return created;
}

engine::TempoMap tempoMapFor(const Case& value) {
    // Consecutive changes ramp: the tempo travels from one to the next across
    // the beats between them. A step is two changes at the same beat, which is
    // what the model's curve editor draws when two points share an x position.
    //
    // So a case's tempo list is turned into steps here rather than handed over
    // as it stands. Every render case in the corpus wants steps, because a ramp
    // is the one place the two engines' tempo maps are known to be able to
    // disagree, and a render case built on one would report that disagreement
    // as a clip in the wrong place.
    std::vector<engine::TempoChange> changes;

    for (const auto& point : value.tempo) {
        if (!changes.empty()) {
            engine::TempoChange hold;
            hold.startBeat = point.beat;
            hold.bpm = changes.back().bpm;
            changes.push_back(hold);
        }

        engine::TempoChange change;
        change.startBeat = point.beat;
        change.bpm = point.bpm;
        changes.push_back(change);
    }

    return engine::TempoMap(std::move(changes), {});
}

GrooveTemplateSet groovesFor(const Case& value) {
    if (value.grooveXml.isEmpty())
        return {};

    const auto document = juce::parseXML(value.grooveXml);
    if (document == nullptr)
        return {};

    return GrooveTemplateSet::parse(*document);
}

/// Collects the render into one buffer, so that two renders are comparable
/// sample for sample however either was cut into blocks.
class BufferSink final : public OfflineRenderSink {
  public:
    explicit BufferSink(int channels) : channels_(channels) {}

    void write(const juce::AudioBuffer<float>& block, int numSamples) override {
        const auto start = buffer.getNumSamples();
        buffer.setSize(channels_, start + numSamples, true, true, false);
        for (auto channel = 0; channel < channels_; ++channel)
            buffer.copyFrom(channel, start, block, channel, 0, numSamples);
    }

    juce::AudioBuffer<float> buffer;

  private:
    int channels_;
};

}  // namespace

NativeRender renderNative(const Case& value, const InstalledPlugins& installed) {
    NativeRender result;

    const RenderContext context{value.sampleRate, value.blockSize, value.channels};
    const auto tempo = tempoMapFor(value);

    // --- what the track plays ------------------------------------------------

    std::vector<ClipLane> lanes;
    for (const auto& track : value.tracks) {
        ClipLane lane;
        lane.trackId = track.id;
        for (const auto& clip : value.clips) {
            if (clip.trackId != track.id)
                continue;

            // The arrangement's clips, which is what an arrangement lane is.
            // Filtered here rather than left to the compiler's own guard,
            // because the two are answering different questions: the guard
            // exists so that a caller who put a session clip in an arrangement
            // lane hears about it, and this leg is the caller deciding what
            // goes in one.
            //
            // A code-built case never had the distinction to make -- every clip
            // in the corpus is an arrangement clip -- and a real project always
            // does, since the session and the arrangement are two views of one
            // project and both are saved. Without this the demo project reports
            // sixty-four diagnostics for having a session view, and every one of
            // them would make it unmeasurable.
            //
            // The same rule the incumbent already applies at the same point:
            // ClipSynchronizer::syncArrangementClipToEngine refuses a session
            // clip and the launcher schedules it instead, so an offline
            // arrangement render plays the arrangement on both sides.
            if (clip.view != ClipView::Arrangement)
                continue;

            lane.clips.push_back(clip);
        }
        lanes.push_back(std::move(lane));
    }

    std::vector<ClipSourceInfo> sources;
    for (const auto& source : value.sources)
        sources.push_back(
            {source.id, source.path.toStdString(), source.sampleRate, source.durationSeconds});

    auto snapshot = std::make_shared<const ClipSnapshot>(
        compileClipSnapshot(lanes, sources, tempo, groovesFor(value)));
    for (const auto& diagnostic : snapshot->diagnostics)
        result.diagnostics.push_back("snapshot: " + diagnostic);

    // --- the plan ------------------------------------------------------------

    // The scan an external plugin is resolved against, and the settings one is
    // instantiated at. Both legs read the same scan; a leg that was handed none
    // resolves nothing and says so per device below.
    const adapter::ExternalPluginServices services{
        .formats = installed.formats, .knownPlugins = installed.knownPlugins, .context = context};

    // The model the plan and the devices are both built from, corrected against
    // the scan first. Resolution may change the role an external device is
    // compiled with, and a plan compiled from the project's own guess would
    // route MIDI to the wrong places before any plugin was created.
    auto tracks = value.tracks;
    auto master = value.master;
    const auto identities = resolveExternalDevices(tracks, master, services);

    CompileOptions options;
    options.deviceMeters = false;

    // Compiled twice, and the first one is thrown away. Its only job is to say
    // which devices this project actually renders, so that the plugins can be
    // made -- and a plugin's own state is entitled to change the topology the
    // second compile reads (createExternalDevices).
    //
    // A project with no external device compiles the same plan both times, which
    // is every case in the code-built corpus.
    auto externals = createExternalDevices(
        tracks, master, deviceKeysIn(compileRenderPlan(tracks, master, options)), services);

    // What the render actually ran, for the report's environment line. Read off
    // the created set rather than the resolved one: resolution answers every
    // external device in the project, and the ones that reached a plan op and
    // loaded are the ones that made a sound.
    for (const auto& [key, created] : externals) {
        if (created.device == nullptr)
            continue;

        if (const auto identity = identities.find(key); identity != identities.end())
            result.plugins.push_back(identity->second);
    }

    const auto plan = compileRenderPlan(tracks, master, options);
    for (const auto& diagnostic : plan.diagnostics)
        result.diagnostics.push_back("plan: " + diagnostic);

    // --- what the ops resolve to ---------------------------------------------

    ClipSnapshotFeed clips;
    clips.publish(snapshot);

    // The reader's own thread is off, and the render drives it. See
    // PrefetchThread: a render outruns any thread, so whether a clip's chunks
    // arrived in time would come down to the scheduler, and every underrun that
    // produced would look like a clip playing silence.
    PrefetchThread reader(false);
    WavFactory files;
    ClipVoicePool voices(files, reader, context);
    voices.setSnapshot(snapshot);

    std::map<TrackId, std::unique_ptr<ClipAudioSource>> audioSources;
    std::map<TrackId, std::unique_ptr<ClipMidiSource>> midiSources;
    std::vector<std::unique_ptr<Passthrough>> passthroughs;

    // The devices the app's own factory built, kept alive for the render. One
    // vector for all of them, because what they have in common is who owns them
    // rather than what they are.
    std::vector<std::unique_ptr<EngineDevice>> hosted;
    std::vector<std::unique_ptr<GainDevice>> gains;
    std::vector<std::unique_ptr<ImpulseSynthDevice>> synths;

    const auto modelDevices = devicesIn(tracks, master);

    // The tracks the corpus compares MIDI for, which is the same question the
    // incumbent asks when it decides where to put a capture. Asked once, of the
    // model, through the compiler's own predicate, rather than inferred from the
    // graph afterwards.
    std::set<TrackId> midiTracks;
    for (const auto& track : tracks)
        if (chainConsumesMidi(track))
            midiTracks.insert(track.id);

    std::map<TrackId, std::unique_ptr<ChainMidiTap>> taps;

    PlanBindings bindings;

    for (const auto& op : plan.ops) {
        switch (op.kind) {
            case OpKind::ClipAudio: {
                auto source =
                    std::make_unique<ClipAudioSource>(op.key.trackId, clips, voices.feed());
                source->prepare(context);
                bindings.clipAudio[op.key.trackId] = source.get();
                audioSources[op.key.trackId] = std::move(source);
                break;
            }

            case OpKind::ClipMidi: {
                auto source = std::make_unique<ClipMidiSource>(op.key.trackId, clips);
                source->prepare(context);
                bindings.clipMidi[op.key.trackId] = source.get();
                midiSources[op.key.trackId] = std::move(source);
                break;
            }

            case OpKind::Device: {
                // The corpus's own two where the model names them, the app's
                // own factory for everything else, and a stand-in for what
                // neither can build.
                //
                // The corpus's two come first because nothing else can build
                // them: they are registered so that the incumbent can create
                // one, and they carry no createDevice, so the factory would
                // return null for both and they would fall through to the
                // stand-in. Every device MAGDA ships that has moved to the SDK
                // is built by the factory below, which is what makes a corpus
                // case able to contain one (#2174).
                const auto found = modelDevices.find(op.key.deviceKey());
                auto* model = found == modelDevices.end() ? nullptr : found->second;

                if (model != nullptr && isImpulseSynthDevice(*model)) {
                    auto device = std::make_unique<ImpulseSynthDevice>();
                    device->prepare(context);
                    bindings.devices[op.key.deviceKey()] = device.get();
                    synths.push_back(std::move(device));
                    break;
                }

                if (model != nullptr && isGainDevice(*model)) {
                    auto device = std::make_unique<GainDevice>();
                    device->prepare(context);
                    bindings.devices[op.key.deviceKey()] = device.get();
                    gains.push_back(std::move(device));
                    break;
                }

                if (model != nullptr && isExternalDevice(*model)) {
                    // Already made, above, because the plan was compiled from
                    // what the instance turned out to be rather than from what
                    // the project guessed. This binds that instance; a second
                    // one made here would be a second plugin, at its own state,
                    // for a plan describing the first.
                    const auto made = externals.find(op.key.deviceKey());

                    if (made != externals.end() && made->second.device != nullptr) {
                        made->second.device->prepare(context);
                        bindings.devices[op.key.deviceKey()] = made->second.device.get();
                        hosted.push_back(std::move(made->second.device));
                        break;
                    }

                    // Said out loud, always. A plugin this machine does not
                    // have is not a quieter project, it is a different one, and
                    // a case that compared anyway would be passing by having
                    // tested less than it claims. The runner turns any
                    // diagnostic the case did not declare into unmeasurable,
                    // which is the same treatment a proxy that never arrived
                    // already gets (#2175).
                    //
                    // An op with nothing made for it at all is a device the
                    // first compile did not reach and the second did, which is
                    // the plan changing shape around the corrected topology.
                    result.diagnostics.push_back(
                        "devices: " + (made != externals.end()
                                           ? made->second.failure.toStdString()
                                           : "external plugin \"" + model->name.toStdString() +
                                                 "\" was not reached by the first compile"));
                } else if (model != nullptr) {
                    // Built for an offline render, because that is what this
                    // leg is and what the other leg's Renderer tells its own
                    // devices. A device that skips live-only work has to skip
                    // it on both sides or the corpus is comparing two different
                    // decisions about the same block.
                    if (auto device = adapter::createEngineDevice(*model, /*offlineRender=*/true)) {
                        device->prepare(context);
                        bindings.devices[op.key.deviceKey()] = device.get();
                        hosted.push_back(std::move(device));
                        break;
                    }

                    // A device the app can build and the engine cannot, said
                    // out loud. The stand-in below still has to be bound,
                    // because the executor refuses an unbound Device op, but a
                    // case that reached it is measuring a project the engine did
                    // not really render.
                    //
                    // Only for a device some catalog knows. An id nothing
                    // registered is not a device either engine runs -- the MIDI
                    // cases' instrument slot is one, and the incumbent puts a
                    // capture there rather than a plugin -- and reporting those
                    // would make every such case unmeasurable over an asymmetry
                    // that does not exist.
                    if (adapter::isRegisteredDevice(model->pluginId))
                        result.diagnostics.push_back("devices: no native device for " +
                                                     model->pluginId.toStdString());
                }

                // The stand-in passes signal because that is what the incumbent
                // does with a device it does not instantiate.
                auto device = std::make_unique<Passthrough>();
                device->prepare(context);
                bindings.devices[op.key.deviceKey()] = device.get();
                passthroughs.push_back(std::move(device));
                break;
            }

            default:
                break;
        }
    }

    // One tap per eligible track, at the op that is the track's chain input.
    // Bound by name rather than found by walking the plan: the key is the same
    // one the compiler emits, so this asks for a place rather than searching for
    // whatever happens to be standing in it.
    if (value.capturesMidi())
        for (const auto trackId : midiTracks) {
            auto tap = std::make_unique<ChainMidiTap>(value.sampleRate);
            const OpKey key{trackId,           INVALID_RACK_ID,        INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::TrackMidiInput, 0};
            bindings.midiTaps[key] = tap.get();
            taps[trackId] = std::move(tap);
        }

    // The op values and the parameter table together, out of one call, so the
    // two cannot be published out of step (#2117).
    PlanValues values;
    for (const auto& diagnostic :
         resolvePlanValues(plan, tracks, master, values, value.lanes, value.automationClips))
        result.diagnostics.push_back("values: " + diagnostic);

    // What the table could not honour: a link naming something the project does
    // not have, a lane over a target it does not carry, a cycle. Nothing is
    // refused for these, so a case with one would otherwise pass having dropped
    // the thing it exists to compare.
    if (values.params != nullptr)
        for (const auto& diagnostic : values.params->diagnostics)
            result.diagnostics.push_back("params: " + diagnostic);

    // Prepared against that table rather than against none: without it every
    // device is handed an empty window, and the gain device reads that as
    // unity, so the project would sound plausible and assert nothing.
    PlanExecutor executor;
    for (const auto& diagnostic :
         executor.prepare(plan, bindings, context, nullptr, values.params.get()))
        result.diagnostics.push_back("prepare: " + diagnostic);

    if (!executor.isPrepared()) {
        result.failure = "the executor refused the plan";
        return result;
    }

    // --- render --------------------------------------------------------------

    // What the pool primes this case's stretchers with, read where it is
    // actually true: the entries are provisioned as the transport approaches
    // them and retired once they have passed, so asking after the render is
    // asking an empty table. Positioning the pool at the range's start and
    // servicing it once is what a render's first block does anyway.
    voices.setPosition(tempo.beatToTime(value.startBeat));
    voices.service();
    {
        const ClipStreamFeed::Reader table(voices.feed());
        if (table)
            for (const auto& entry : table->entries)
                if (entry.stretcher != nullptr)
                    result.primingSamples = std::max(result.primingSamples, entry.preRollSamples);
    }

    OfflineRenderRequest request;
    request.startBeat = value.startBeat;
    request.endBeat = value.endBeat;
    request.blockSize = value.blockSize;

    BufferSink sink(value.channels);
    const auto rendered = renderOffline(executor, values, context, tempo, request, sink, &voices);

    if (rendered.refused) {
        result.failure = "the offline render refused the plan it was given";
        return result;
    }

    result.audio = std::move(sink.buffer);

    // What the pool primed each stretcher with, read back after the render so
    // that every entry it provisioned has been through it.
    for (const auto& [trackId, source] : audioSources)
        result.starvedVoices += source->starvedVoices();
    for (const auto& [trackId, source] : midiSources)
        result.droppedMidiEvents += source->droppedEvents();

    // Every eligible track gets an entry, including one whose tap never fired.
    // The incumbent puts a capture on every MIDI-consuming track and indexes the
    // result by track whether it heard anything or not, so a track with no clip
    // yet is an empty stream on both sides rather than a track only one leg saw.
    for (const auto& [trackId, tap] : taps) {
        auto& perTrack = result.midiByTrack[trackId];
        for (const auto& event : tap->captured) {
            perTrack.push_back(event);
            result.midi.push_back(event);
        }
    }

    // The flat stream is what the report prints, so it reads as a function of
    // time rather than as one track's timeline followed by another's.
    std::stable_sort(result.midi.begin(), result.midi.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    // One sentence, however many devices said it. A Drum Grid has a pad per
    // note and every pad holds the same sampler, so a device the engine cannot
    // build is a diagnostic repeated sixteen times that says nothing the first
    // one did not. Repetition also has to be counted by anything declaring the
    // diagnostic it expects (NullDiffCase::expectedDiagnostics), which would
    // make a case's declaration depend on how many pads a grid happens to have.
    std::stable_sort(result.diagnostics.begin(), result.diagnostics.end());
    result.diagnostics.erase(std::unique(result.diagnostics.begin(), result.diagnostics.end()),
                             result.diagnostics.end());

    return result;
}

}  // namespace magda::nulldiff
