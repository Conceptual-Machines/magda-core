#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "ClipCallback.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/EventPlacement.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "io/SourceReaders.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_sample_rule.cpp
 * @brief One rule for every instant (#2741): the sample it falls in.
 *
 * Positions here sit a fraction into a sample on purpose, because a whole
 * sample cannot tell floor from nearest.
 */

using magda::ClipInfo;
using magda::MidiNote;
using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::BlockInfo;
using magda::engine::ClipAudioSource;
using magda::engine::ClipLane;
using magda::engine::ClipMidiSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::ClipStreamFeed;
using magda::engine::ClipStreamTable;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::SnapshotSpan;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kSamplesPerBeat = 24000.0;  ///< 120 bpm
constexpr magda::TrackId kTrack = 5;
constexpr magda::ClipId kAudioClip = 1;
constexpr magda::ClipId kMidiClip = 2;

/// Source sample n reads back as n + 1, so a slip shows in the value and the
/// first sample of the material is not silence.
class CountingReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 10000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample + 1));
        return numSamples;
    }
};

BlockInfo blockFrom(std::int64_t startSample, int count) {
    BlockInfo block;
    block.numSamples = count;
    block.sampleRate = kSampleRate;
    block.playing = true;
    block.continuous = startSample > 0;
    block.seconds = {static_cast<double>(startSample) / kSampleRate,
                     static_cast<double>(startSample + count) / kSampleRate};
    block.beats = {static_cast<double>(startSample) / kSamplesPerBeat,
                   static_cast<double>(startSample + count) / kSamplesPerBeat};
    block.monotonicBeats = block.beats;
    block.monotonicSeconds = block.seconds;
    block.monotonicSamples = {magda::engine::SamplePosition{startSample},
                              magda::engine::SamplePosition{startSample + count}};
    return block;
}

/// From one position to another, in samples of the timeline.
SnapshotSpan samples(double start, double end) {
    SnapshotSpan span;
    span.seconds = {start / kSampleRate, end / kSampleRate};
    span.beats = {start / kSamplesPerBeat, end / kSamplesPerBeat};
    return span;
}

/// An audio clip whose material begins at @p eventStart and is heard over @p heard.
AudioClipPlayback audioClip(double eventStart, SnapshotSpan heard) {
    AudioClipPlayback clip;
    clip.clipId = kAudioClip;
    clip.span = heard;
    clip.launchFadeSamples = 0;

    AudioEventPlayback event;
    event.eventId = kAudioClip;
    event.sourceId = kAudioClip;
    event.filePath = "count.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 1000.0;
    event.span = samples(eventStart, heard.seconds.end * kSampleRate);
    clip.events.push_back(std::move(event));
    return clip;
}

/// A MIDI clip at @p startSample holding one note at its own start.
ClipInfo midiClip(double startSample) {
    ClipInfo clip;
    clip.id = kMidiClip;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(startSample / kSamplesPerBeat, 1.0);
    clip.midiNotes = {MidiNote{60, 100, 0.0, 0.25, 0, {}}};
    return clip;
}

/// One track's audio and MIDI, rendered a block at a time.
struct Rig {
    explicit Rig(int blockSize) : blockSize_(blockSize) {
        const RenderContext context{kSampleRate, blockSize, 2};
        audio.prepare(context);
        midi.prepare(context);
    }

    void publish(AudioClipPlayback clip, std::optional<ClipInfo> midiClip = std::nullopt) {
        auto snapshot = std::make_shared<ClipSnapshot>();

        if (midiClip) {
            ClipLane lane;
            lane.trackId = kTrack;
            lane.clips = {*midiClip};
            *snapshot = magda::engine::compileClipSnapshot(
                {lane}, {}, TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}}));
            REQUIRE(snapshot->diagnostics.empty());
            REQUIRE(snapshot->tracks.size() == 1);
        } else {
            snapshot->tracks.push_back({kTrack, {}, {}});
        }

        const auto& event = clip.events.front();
        stream_ = std::make_shared<PrefetchStream>(
            magda::engine::readThrough(std::make_unique<CountingReader>(),
                                       magda::engine::sourceReadFor(event, kSampleRate)),
            RenderContext{kSampleRate, blockSize_, 2}, magda::engine::PrefetchSettings{4096, 8});
        stream_->seek(0);

        ClipStreamTable table;
        table.entries.push_back(
            ClipStreamTable::Entry{kTrack, clip.clipId, event.eventId, stream_});
        streams.publish(std::make_shared<const ClipStreamTable>(table));

        snapshot->tracks.front().audio.push_back(std::move(clip));
        clips.publish(std::move(snapshot));
    }

    /// The first @p length samples of the timeline, and the note-ons in them.
    void render(int length) {
        output.assign(static_cast<std::size_t>(length), 0.0f);
        noteOns.clear();

        juce::AudioBuffer<float> buffer(2, blockSize_);
        for (auto start = 0; start < length; start += blockSize_) {
            const auto count = std::min(blockSize_, length - start);
            const auto block = blockFrom(start, count);

            while (stream_->fill()) {
            }

            buffer.clear();
            juce::MidiBuffer messages;
            {
                const magda::test::ClipBlock pinned(clips, block);
                audio.render(block, juce::dsp::AudioBlock<float>(buffer).getSubBlock(
                                        0, static_cast<std::size_t>(count)));
                midi.render(block, messages);
            }

            std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + count,
                      output.begin() + start);
            for (const auto metadata : messages)
                if (metadata.getMessage().isNoteOn())
                    noteOns.push_back(start + metadata.samplePosition);
        }
    }

    /// The first sample of the timeline the audio sounds on.
    int firstSounding() const {
        const auto found =
            std::find_if(output.begin(), output.end(), [](float value) { return value != 0.0f; });
        return static_cast<int>(found - output.begin());
    }

    ClipSnapshotFeed clips;
    ClipStreamFeed streams;
    ClipAudioSource audio{kTrack, clips, streams};
    ClipMidiSource midi{kTrack, clips};
    std::vector<float> output;
    std::vector<int> noteOns;

  private:
    int blockSize_ = 0;
    std::shared_ptr<PrefetchStream> stream_;
};

}  // namespace

TEST_CASE("A MIDI note and an audio clip on the same fractional beat start on the same sample",
          "[engine][samples][2741]") {
    // 488.6 into the second block, and 511.9 into the third: its last sample,
    // which nearest would have sent to the next block for the audio alone.
    for (const auto start : {1000.6, 1535.9}) {
        Rig rig(512);
        rig.publish(audioClip(start, samples(start, start + 2000.0)), midiClip(start));
        rig.render(4096);

        const auto expected = static_cast<int>(std::floor(start));
        REQUIRE(rig.noteOns.size() == 1);
        CHECK(rig.noteOns.front() == expected);
        CHECK(rig.firstSounding() == expected);
        CHECK(rig.output[static_cast<std::size_t>(expected)] == 1.0f);
    }
}

TEST_CASE("An audio clip on fractional positions renders the same at every block size",
          "[engine][samples][2741]") {
    // The material begins 1000.6 in, the clip is heard from 1030.3, and a hole
    // runs from 2000.7 to 2100.2: three fractions, none of them the same.
    const auto render = [](int blockSize) {
        auto clip = audioClip(1000.6, samples(1030.3, 9000.45));
        clip.silenced.push_back(samples(2000.7, 2100.2));

        Rig rig(blockSize);
        rig.publish(std::move(clip));
        rig.render(10000);
        return rig.output;
    };

    const auto reference = render(512);

    SECTION("each source sample sounds in the sample its own instant falls in") {
        // Source n sits at 1000.6 + n, so sample 1030 plays source 30.
        CHECK(reference[1029] == 0.0f);
        CHECK(reference[1030] == 31.0f);
        CHECK(reference[1999] == 1000.0f);
        CHECK(reference[2000] == 0.0f);
        CHECK(reference[2099] == 0.0f);
        CHECK(reference[2100] == 1101.0f);
        CHECK(reference[8999] == 8000.0f);
        CHECK(reference[9000] == 0.0f);
    }

    SECTION("and never twice across a block boundary") {
        for (auto sample = 1031; sample < 9000; ++sample)
            if (sample != 2100 && (sample < 2000 || sample >= 2100))
                REQUIRE(reference[static_cast<std::size_t>(sample)] ==
                        reference[static_cast<std::size_t>(sample) - 1] + 1.0f);
    }

    SECTION("at any block size") {
        for (const auto blockSize : {64, 96, 4096})
            CHECK(render(blockSize) == reference);
    }
}

namespace {

using magda::engine::DeviceBlock;
using magda::engine::NoteFractions;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanOp;
using magda::engine::PortRef;
using magda::engine::SignalKind;

/// Middle C at sample 5, 0.7 into it, loud.
class FractionalNote final : public magda::engine::EngineMidiSource {
  public:
    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        out.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 5);
    }

    void renderWithFractions(const BlockInfo& block, juce::MidiBuffer& out,
                             NoteFractions& fractions) override {
        render(block, out);
        fractions.add(5, 1, 60, 0.7f);
    }
};

/// Middle C at sample 5, quiet, and nothing said about where in the sample,
/// which is what a hosted plugin's output is.
class PlainNoteDevice final : public magda::engine::EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        if (block.midiOut != nullptr)
            block.midiOut->addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(20)),
                                    5);
    }
};

/// Each note-on's velocity and the fraction it arrived with.
class FractionProbe final : public magda::engine::EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        magda::engine::NoteOccurrences occurrences;
        for (const auto metadata : *block.midiIn)
            if (const auto message = metadata.getMessage(); message.isNoteOn())
                arrived.push_back(
                    {message.getVelocity(),
                     block.midiInFractions->at(
                         metadata.samplePosition, message.getChannel(), message.getNoteNumber(),
                         occurrences.next(metadata.samplePosition, message.getChannel(),
                                          message.getNoteNumber()))});
    }

    std::vector<std::pair<int, float>> arrived;
};

PlanOp opOf(OpKind kind, OpRole role, magda::DeviceId device, std::vector<PortRef> inputs,
            std::vector<magda::engine::PortDesc> outputs) {
    PlanOp op;
    op.kind = kind;
    op.key.trackId = 1;
    op.key.deviceId = device;
    op.key.role = role;
    op.inputs = std::move(inputs);
    op.outputs = std::move(outputs);
    return op;
}

}  // namespace

TEST_CASE("A device that says nothing about fractions puts its notes on their samples",
          "[engine][samples][2741]") {
    // Its note-on merged ahead of a clip's on the same pitch and sample: without
    // an entry of its own, it would take the clip's fraction and leave the
    // clip's note none.
    magda::engine::RenderPlan plan;
    plan.ops.push_back(opOf(OpKind::ClipMidi, OpRole::ClipMidi, 0, {}, {SignalKind::Midi}));
    plan.ops.push_back(opOf(OpKind::Device, OpRole::DeviceProcess, 8,
                            {PortRef{}, PortRef{}, PortRef{}},
                            {SignalKind::Audio, SignalKind::Midi}));
    plan.ops.push_back(opOf(OpKind::MergeMidi, OpRole::ChainMidiMerge, 8,
                            {PortRef{1, 1}, PortRef{0, 0}}, {SignalKind::Midi}));
    plan.ops.push_back(opOf(OpKind::Device, OpRole::DeviceProcess, 9,
                            {PortRef{1, 0}, PortRef{2, 0}, PortRef{}}, {SignalKind::Audio}));
    plan.ops.push_back(opOf(OpKind::Output, OpRole::HardwareOutput, 0, {PortRef{3, 0}}, {}));
    plan.outputOps = {4};
    magda::engine::bakeScheduling(plan);

    FractionalNote clip;
    PlainNoteDevice plugin;
    FractionProbe probe;
    magda::engine::PlanBindings bindings;
    bindings.clipMidi[1] = &clip;
    bindings.devices[magda::engine::DeviceKey{8}] = &plugin;
    bindings.devices[magda::engine::DeviceKey{9}] = &probe;

    magda::engine::PlanExecutor executor;
    const auto messages = executor.prepare(plan, bindings, RenderContext{kSampleRate, 64, 2});
    for (const auto& message : messages)
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(messages.empty());

    juce::AudioBuffer<float> output(2, 64);
    executor.process(magda::engine::PlanValues{}, blockFrom(0, 64), output);

    using Arrival = std::pair<int, float>;
    CHECK(probe.arrived == std::vector<Arrival>{{20, 0.0f}, {100, 0.7f}});
}

TEST_CASE("A note-on's occurrence is counted however many share its sample",
          "[engine][samples][2741]") {
    // Past any fixed table: every pitch on every channel, then one pitch
    // struck again and again, all on one sample.
    magda::engine::NoteOccurrences occurrences;
    occurrences.restart();

    for (int channel = 1; channel <= 16; ++channel)
        for (int note = 0; note < 128; ++note)
            REQUIRE(occurrences.next(7, channel, note) == 0);

    for (int again = 1; again < 600; ++again)
        REQUIRE(occurrences.next(7, 1, 60) == again);

    // A new sample, and a new walk, each start afresh.
    CHECK(occurrences.next(8, 1, 60) == 0);
    occurrences.restart();
    CHECK(occurrences.next(8, 1, 60) == 0);
}
