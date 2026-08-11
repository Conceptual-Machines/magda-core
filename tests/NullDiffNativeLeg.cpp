#include "NullDiffNativeLeg.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>

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

/// Stands where a synth would, and records what reaches it. The comparison
/// point for every MIDI case, and the reason a MIDI case has a device at all:
/// a plan compiles no ClipMidi op for a track whose chain consumes no MIDI.
class MidiCapture final : public EngineDevice {
  public:
    explicit MidiCapture(double sampleRate) : sampleRate_(sampleRate) {}

    void process(DeviceBlock& block) override {
        // Silence out. What a synth would make of these messages is not what is
        // being compared, and printing anything would only invite somebody to
        // compare it.
        block.audio.clear();

        if (block.midiIn == nullptr)
            return;

        // Where the block sits on the timeline, so a captured message carries a
        // position in the render rather than one inside a callback. Taken from
        // the block's own seconds, which the clock derives from a sample count
        // rather than accumulating, so this recovers that count exactly and
        // keeps doing so through the tail, where the timeline stands still.
        const auto blockStart =
            static_cast<std::int64_t>(std::llround(block.block.startSeconds * sampleRate_));

        for (const auto message : *block.midiIn) {
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
    std::map<DeviceId, std::unique_ptr<MidiCapture>> captures;

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
                auto device = std::make_unique<MidiCapture>(value.sampleRate);
                device->prepare(context);
                bindings.devices[op.key.deviceId] = device.get();
                captures[op.key.deviceId] = std::move(device);
                break;
            }

            default:
                break;
        }
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

    for (const auto& [trackId, source] : audioSources)
        result.starvedVoices += source->starvedVoices();
    for (const auto& [trackId, source] : midiSources)
        result.droppedMidiEvents += source->droppedEvents();

    for (const auto& [deviceId, capture] : captures)
        for (const auto& event : capture->captured)
            result.midi.push_back(event);

    return result;
}

}  // namespace magda::nulldiff
