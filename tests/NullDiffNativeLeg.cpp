#include "NullDiffNativeLeg.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

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
#include "plan/PlanCompiler.hpp"
#include "transport/TempoMap.hpp"

namespace magda::nulldiff {

using namespace magda::engine;

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

NativeRender renderNative(const Case& value) {
    NativeRender result;

    const RenderContext context{value.sampleRate, value.blockSize, value.channels};
    const auto tempo = tempoMapFor(value);

    // --- what the track plays ------------------------------------------------

    std::vector<ClipLane> lanes;
    for (const auto& track : value.tracks) {
        ClipLane lane;
        lane.trackId = track.id;
        for (const auto& clip : value.clips)
            if (clip.trackId == track.id)
                lane.clips.push_back(clip);
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

    CompileOptions options;
    options.deviceMeters = false;
    const auto plan = compileRenderPlan(value.tracks, value.master, options);
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

    // The tracks the corpus compares MIDI for, which is the same question the
    // incumbent asks when it decides where to put a capture. Asked once, of the
    // model, through the compiler's own predicate, rather than inferred from the
    // graph afterwards.
    std::set<TrackId> midiTracks;
    for (const auto& track : value.tracks)
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
                // Transparent, always. A device stand-in exists because the
                // executor refuses a plan with an unbound Device op, not because
                // the corpus wants to hear what a device would do, and it no
                // longer decides anything about where MIDI is observed. The
                // incumbent instantiates none of the model's devices either, so
                // passing signal is also what it does.
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

    PlanValues values;
    for (const auto& diagnostic : resolvePlanValues(plan, value.tracks, value.master, values))
        result.diagnostics.push_back("values: " + diagnostic);

    PlanExecutor executor;
    for (const auto& diagnostic : executor.prepare(plan, bindings, context))
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

    return result;
}

}  // namespace magda::nulldiff
