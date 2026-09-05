#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/EventPlacement.hpp"
#include "clip/SessionPlayback.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/SourceReaders.hpp"
#include "launch/SessionLauncher.hpp"
#include "transport/TransportClock.hpp"

/**
 * A launched slot sounding (#2301), on the audio thread.
 *
 * A slot plays the arrangement's own material over a block moved onto the run's
 * origin, starting and stopping at the sample the event landed on.
 *
 * Everything rolls, and nothing advances a handle by hand: the launcher does it
 * once per block, before anything renders.
 */

using Catch::Approx;
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
using magda::engine::kSectionDeClickSamples;
using magda::engine::LaunchHandle;
using magda::engine::LaunchHandleFeed;
using magda::engine::LaunchHandleTable;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::Section;
using magda::engine::SessionSlotPlayback;
using magda::engine::SlotKey;
using magda::engine::SnapshotSpan;
using magda::engine::TempoMap;
using magda::engine::TrackClipPlayback;

namespace {

constexpr double kSampleRate = 48000.0;

/// A quarter of a beat at 120 bpm, so a beat is four blocks and a launch
/// quantized to the middle of a block lands on a nameable sample.
constexpr int kBlockSize = 6000;
constexpr double kBeatsPerBlock = 0.25;
constexpr double kSecondsPerBeat = 0.5;

constexpr magda::TrackId kTrack = 7;
constexpr int kScene = 0;

/// Every sample the same, so what is sounding and what is not is readable off
/// the output without dividing by what the material was doing.
class ConstantReader final : public magda::engine::AudioFileReader {
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

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample, 1.0f);
        return numSamples;
    }
};

/// Every sample the same known level, so which section reached the output is
/// readable off the value.
class LevelReader final : public magda::engine::AudioFileReader {
  public:
    explicit LevelReader(float level) : level_(level) {}

    std::int64_t lengthInSamples() const override {
        return 10000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample, level_);
        return numSamples;
    }

  private:
    float level_;
};

/// Sample n reads back as n, so where in the material a sample came from is
/// readable off its value rather than inferred.
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
                                      static_cast<float>(startSample + sample));
        return numSamples;
    }
};

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

/// The block at @p index of a transport rolling from zero. The monotonic faces
/// are the timeline faces here, because nothing in these cases loops or
/// locates; the ones that do build their own blocks.
BlockInfo blockAt(int index, bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {magda::engine::SamplePosition{index * kBlockSize},
                              magda::engine::SamplePosition{(index + 1) * kBlockSize}};
    block.playing = true;
    block.continuous = continuous;
    block.beats.start = index * kBeatsPerBlock;
    block.beats.end = (index + 1) * kBeatsPerBlock;
    block.seconds.start = block.beats.start * kSecondsPerBeat;
    block.seconds.end = block.beats.end * kSecondsPerBeat;
    block.monotonicBeats.start = block.beats.start;
    block.monotonicBeats.end = block.beats.end;
    block.monotonicSeconds.start = block.seconds.start;
    block.monotonicSeconds.end = block.seconds.end;
    return block;
}

/// A block of @p count samples beginning at @p startSample, at the one tempo.
/// For the cases that assert a render does not depend on how the callback was
/// cut up, where the block is smaller than the fixture's own.
BlockInfo smallBlockAt(std::int64_t startSample, int count, bool continuous = true) {
    BlockInfo block;
    block.numSamples = count;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {magda::engine::SamplePosition{startSample},
                              magda::engine::SamplePosition{startSample + count}};
    block.playing = true;
    block.continuous = continuous;
    block.seconds.start = static_cast<double>(startSample) / kSampleRate;
    block.seconds.end = static_cast<double>(startSample + count) / kSampleRate;
    block.beats.start = block.seconds.start / kSecondsPerBeat;
    block.beats.end = block.seconds.end / kSecondsPerBeat;
    block.monotonicBeats.start = block.beats.start;
    block.monotonicBeats.end = block.beats.end;
    block.monotonicSeconds.start = block.seconds.start;
    block.monotonicSeconds.end = block.seconds.end;
    return block;
}

/// The block covering @p startSeconds, on @p map rather than at one tempo. The
/// two faces are derived through the one map, the way the transport derives
/// them (RenderContext.hpp).
BlockInfo blockOnMap(const magda::engine::TempoMap& map, double startSeconds,
                     bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {
        magda::engine::SamplePosition{std::llround(startSeconds * kSampleRate)},
        magda::engine::SamplePosition{std::llround(startSeconds * kSampleRate) + kBlockSize}};
    block.playing = true;
    block.continuous = continuous;
    block.seconds.start = startSeconds;
    block.seconds.end = startSeconds + (kBlockSize / kSampleRate);
    block.beats.start = map.timeToBeat(block.seconds.start);
    block.beats.end = map.timeToBeat(block.seconds.end);
    block.monotonicBeats.start = block.beats.start;
    block.monotonicBeats.end = block.beats.end;
    block.monotonicSeconds.start = block.seconds.start;
    block.monotonicSeconds.end = block.seconds.end;
    block.tempo = &map;
    return block;
}

/// A span from its beats, with the seconds face 120 bpm gives it.
SnapshotSpan beats(double start, double end) {
    SnapshotSpan span;
    span.beats = {start, end};
    span.seconds = {start * kSecondsPerBeat, end * kSecondsPerBeat};
    return span;
}

AudioClipPlayback clipOver(magda::ClipId id, SnapshotSpan span) {
    AudioClipPlayback clip;
    clip.clipId = id;
    clip.span = span;
    clip.launchFadeSamples = 0;  // the ramp has its own tests

    AudioEventPlayback event;
    event.eventId = id;
    event.sourceId = id;
    event.filePath = "slot.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 1000.0;
    event.span = span;
    clip.events.push_back(std::move(event));

    return clip;
}

/// One track's session, its handle and its readers. The handle is owned here;
/// what a store does with handles has its own cases below.
struct AudioRig {
    AudioRig() {
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene}, .handle = &handle});
        handles.publish(std::make_shared<const LaunchHandleTable>(table));

        source.prepare(context());
        output.setSize(2, kBlockSize);
        output.clear();
    }

    /// A slot holding one clip @p lengthBeats long, read through @p reader.
    void give(magda::ClipId id, double lengthBeats,
              std::unique_ptr<magda::engine::AudioFileReader> reader) {
        SessionSlotPlayback slot;
        slot.sceneIndex = kScene;
        slot.lengthBeats = lengthBeats;
        slot.audio.push_back(clipOver(id, beats(0.0, lengthBeats)));

        const auto& event = slot.audio.front().events.front();
        auto stream = std::make_shared<PrefetchStream>(
            magda::engine::readThrough(std::move(reader),
                                       magda::engine::sourceReadFor(event, kSampleRate)),
            context(), magda::engine::PrefetchSettings{1024, 8});

        streamTable.entries.push_back(ClipStreamTable::Entry{kTrack, id, event.eventId, stream});
        lane.session.push_back(std::move(slot));
    }

    /// @p map is the one the blocks will carry, and the snapshot is stamped
    /// with its fingerprint. A source counts a snapshot compiled against
    /// another map, so a fixture that publishes one is a fixture that lies
    /// about its own setup (#2337). Absent for cases whose blocks carry none.
    void publish(const magda::engine::TempoMap* map = nullptr) {
        auto compiled = std::make_shared<ClipSnapshot>();
        compiled->tracks.push_back(lane);
        compiled->tempoFingerprint = map != nullptr ? map->fingerprint() : 0;
        clips.publish(std::move(compiled));
        streams.publish(std::make_shared<const ClipStreamTable>(streamTable));
    }

    /// Roll one block: the launcher first, then the source, as the session does.
    void render(int index, bool continuous = true) {
        renderBlock(blockAt(index, continuous));
    }

    /// The same, for a block a case assembled itself.
    void renderBlock(const BlockInfo& block) {
        fill();
        magda::engine::advanceLaunchHandles(handles, requests, block);
        source.render(block, juce::dsp::AudioBlock<float>(output));
    }

    void roll(int first, int last) {
        for (auto index = first; index <= last; ++index)
            render(index, index != first);
    }

    void fill() {
        auto worked = true;
        while (worked) {
            worked = false;
            for (const auto& entry : streamTable.entries)
                worked = entry.stream->fill() || worked;
        }
    }

    float at(int sample, int channel = 0) const {
        return output.getSample(channel, sample);
    }

    /// The loudest sample in the output, so "nothing sounded" is one assertion.
    float peak() const {
        return output.getMagnitude(0, kBlockSize);
    }

    TrackClipPlayback lane{kTrack, {}, {}};
    ClipSnapshotFeed clips;
    ClipStreamFeed streams;
    LaunchHandle handle;
    LaunchHandleTable table;
    LaunchHandleFeed handles;
    magda::engine::LaunchRequestQueue requests;
    ClipAudioSource source{kTrack, clips, streams, handles, Section::Session};
    ClipStreamTable streamTable;
    juce::AudioBuffer<float> output;
};

// --- MIDI --------------------------------------------------------------------

TempoMap tempoMap() {
    return TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
}

ClipInfo sessionMidiClip(magda::ClipId id, double lengthBeats, std::vector<MidiNote> notes) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Session;
    clip.sceneIndex = kScene;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, lengthBeats);
    clip.midiNotes = std::move(notes);
    return clip;
}

/// One message, and where it landed on the callback.
struct Captured {
    int block = 0;
    int sample = 0;
    juce::MidiMessage message;
};

struct MidiRig {
    MidiRig() {
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene}, .handle = &handle});
        handles.publish(std::make_shared<const LaunchHandleTable>(table));
        source.prepare(context());
    }

    void publish(std::vector<ClipInfo> session) {
        ClipLane lane;
        lane.trackId = kTrack;
        lane.session = std::move(session);

        auto compiled = std::make_shared<const ClipSnapshot>(
            magda::engine::compileClipSnapshot({lane}, {}, tempoMap(), {}));
        REQUIRE(compiled->diagnostics.empty());
        clips.publish(compiled);
    }

    void roll(int first, int last) {
        for (auto index = first; index <= last; ++index) {
            const auto block = blockAt(index, index != first);
            magda::engine::advanceLaunchHandles(handles, requests, block);

            juce::MidiBuffer buffer;
            source.render(block, buffer);
            panics.push_back(source.raisedAllNotesOff());

            for (const auto metadata : buffer)
                captured.push_back(Captured{index, metadata.samplePosition, metadata.getMessage()});
        }
    }

    /// The blocks this rig rolled where the source raised a panic (#2418).
    std::vector<int> panicBlocks() const {
        std::vector<int> blocks;
        for (std::size_t index = 0; index < panics.size(); ++index)
            if (panics[index])
                blocks.push_back(static_cast<int>(index));
        return blocks;
    }

    /// Notes still sounding after everything recorded, as "channel:note". The
    /// invariant every MIDI case here carries: a slot that stopped owes an off
    /// for everything it started.
    std::vector<std::string> hanging() const {
        std::vector<std::string> sounding;

        for (const auto& entry : captured) {
            const auto key = std::to_string(entry.message.getChannel()) + ":" +
                             std::to_string(entry.message.getNoteNumber());
            const auto found = std::find(sounding.begin(), sounding.end(), key);

            if (entry.message.isNoteOn() && found == sounding.end())
                sounding.push_back(key);
            else if (entry.message.isNoteOff() && found != sounding.end())
                sounding.erase(found);
        }

        return sounding;
    }

    std::vector<Captured> noteOns() const {
        std::vector<Captured> out;
        for (const auto& entry : captured)
            if (entry.message.isNoteOn())
                out.push_back(entry);
        return out;
    }

    std::vector<Captured> noteOffs() const {
        std::vector<Captured> out;
        for (const auto& entry : captured)
            if (entry.message.isNoteOff())
                out.push_back(entry);
        return out;
    }

    ClipSnapshotFeed clips;
    LaunchHandle handle;
    LaunchHandleTable table;
    LaunchHandleFeed handles;
    magda::engine::LaunchRequestQueue requests;
    ClipMidiSource source{kTrack, clips, handles, Section::Session};
    std::vector<Captured> captured;
    std::vector<bool> panics;
};

/// A clip on the timeline, from beat @p start, carrying @p notes.
ClipInfo arrangementMidiClip(magda::ClipId id, double start, double lengthBeats,
                             std::vector<MidiNote> notes) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(start, lengthBeats);
    clip.midiNotes = std::move(notes);
    return clip;
}

/// One track's MIDI in both sections, with a source for each (#2302).
struct MidiSwitchRig {
    /// @copydoc SwitchRig
    explicit MidiSwitchRig(bool publishHandles = true) {
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene}, .handle = &handle});
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene + 1}, .handle = &second});

        if (publishHandles)
            handles.publish(std::make_shared<const LaunchHandleTable>(table));
        arrangement.prepare(context());
        session.prepare(context());
    }

    void publish(std::vector<ClipInfo> arrangementClips, std::vector<ClipInfo> sessionClips) {
        ClipLane lane;
        lane.trackId = kTrack;
        lane.clips = std::move(arrangementClips);
        lane.session = std::move(sessionClips);

        auto compiled = std::make_shared<const ClipSnapshot>(
            magda::engine::compileClipSnapshot({lane}, {}, tempoMap(), {}));
        REQUIRE(compiled->diagnostics.empty());
        clips.publish(compiled);
    }

    /// Continuous across calls, not just within one: only the very first block
    /// this rig ever rolls is a discontinuity, or a case that rolls in two
    /// stages would chase its own notes in the middle.
    void roll(int first, int last) {
        for (auto index = first; index <= last; ++index) {
            const auto block = blockAt(index, rolled_);
            rolled_ = true;

            magda::engine::advanceLaunchHandles(handles, requests, block);

            capture(arrangement, block, index, fromArrangement);
            arrangementPanics.push_back(arrangement.raisedAllNotesOff());
            capture(session, block, index, fromSession);
        }
    }

    static std::vector<Captured> notesOn(const std::vector<Captured>& all) {
        std::vector<Captured> out;
        for (const auto& entry : all)
            if (entry.message.isNoteOn())
                out.push_back(entry);
        return out;
    }

    static std::vector<Captured> notesOff(const std::vector<Captured>& all) {
        std::vector<Captured> out;
        for (const auto& entry : all)
            if (entry.message.isNoteOff())
                out.push_back(entry);
        return out;
    }

    /// Notes the section left sounding, which is the invariant a hand-over has
    /// to keep: whatever it started, it owes an off for.
    static std::vector<std::string> hanging(const std::vector<Captured>& all) {
        std::vector<std::string> sounding;

        for (const auto& entry : all) {
            const auto key = std::to_string(entry.message.getChannel()) + ":" +
                             std::to_string(entry.message.getNoteNumber());
            const auto found = std::find(sounding.begin(), sounding.end(), key);

            if (entry.message.isNoteOn() && found == sounding.end())
                sounding.push_back(key);
            else if (entry.message.isNoteOff() && found != sounding.end())
                sounding.erase(found);
        }

        return sounding;
    }

    ClipSnapshotFeed clips;
    LaunchHandle handle;

    /// The scene below, for the cases where one slot hands over to another.
    LaunchHandle second;

    LaunchHandleTable table;
    LaunchHandleFeed handles;
    magda::engine::LaunchRequestQueue requests;

    ClipMidiSource arrangement{kTrack, clips, handles, Section::Arrangement};
    ClipMidiSource session{kTrack, clips, handles, Section::Session};

    /// One entry per block rolled, for the discontinuity a device is owed when
    /// the arrangement is handed its track back (#2418).
    std::vector<bool> arrangementPanics;

    std::vector<Captured> fromArrangement;
    std::vector<Captured> fromSession;

  private:
    bool rolled_ = false;

    static void capture(ClipMidiSource& source, const BlockInfo& block, int index,
                        std::vector<Captured>& into) {
        juce::MidiBuffer buffer;
        source.render(block, buffer);

        for (const auto metadata : buffer)
            into.push_back(Captured{index, metadata.samplePosition, metadata.getMessage()});
    }
};

}  // namespace

// =============================================================================
// The block, moved onto the material
// =============================================================================

TEST_CASE("A session block is the block with its beats moved onto the run",
          "[engine][clip][session]") {
    const auto block = blockAt(40);  // timeline beat 10, seconds 5

    magda::engine::BeatRange whole{block.beats.start, block.beats.end};
    const auto material = magda::engine::materialBlock(
        block, whole, magda::engine::MaterialOrigin{block.beats.start, block.seconds.start});

    // Beat zero of the material, at the seconds beat zero has.
    CHECK(material.beats.start == Approx(0.0));
    CHECK(material.beats.end == Approx(kBeatsPerBlock));
    CHECK(material.seconds.start == Approx(0.0));
    CHECK(material.seconds.end == Approx(kBeatsPerBlock * kSecondsPerBeat));

    // The samples did not move, so a MIDI event still writes into the
    // callback's own buffer.
    CHECK(material.numSamples == block.numSamples);
    for (const auto beat : {0.0, 0.1, 0.2})
        CHECK(material.eventForBeat(beat) == block.eventForBeat(block.beats.start + beat));

    // A run that began here is not continuous with the last block, whatever the
    // transport was doing.
    CHECK_FALSE(material.continuous);

    // Both faces of the block agree with the axes it was given, which is what
    // every reader asks it for first.
    CHECK(material.beatAtTime(material.seconds.start) == Approx(material.beats.start));
    CHECK(material.beatAtTime(material.seconds.end) == Approx(material.beats.end));
}

TEST_CASE("A session block answers about beats through the map, not its own line",
          "[engine][clip][session]") {
    // 120 held to beat 4, which is two seconds in, then 60. Two changes at the
    // one beat, which is how a step is written rather than a ramp to it
    // (TempoMap.hpp). A block ending on the step, so anything asked about past
    // its end is asked across it.
    const magda::engine::TempoMap map({{0.0, 120.0, 0.0f}, {4.0, 120.0, 0.0f}, {4.0, 60.0, 0.0f}},
                                      {{0.0, 4, 4}});
    const auto block = blockOnMap(map, 2.0 - (kBlockSize / kSampleRate));

    const magda::engine::MaterialOrigin origin{block.beats.start, block.seconds.start};
    const magda::engine::BeatRange whole{block.beats.start, block.beats.end};
    const auto material = magda::engine::materialBlock(block, whole, origin);

    // The map comes with the block, and the shift it was moved by comes with
    // it too, which is what keeps the map answerable here.
    CHECK(material.tempo == &map);
    CHECK(material.materialOrigin == origin);

    // A cell boundary past the end of the block, which is where ClipVoice reads
    // (RenderContext.hpp): the grid is anchored to the event rather than to the
    // block, so a cell runs on past it.
    const auto beyond = material.seconds.end + 0.05;
    const auto expected = map.timeToBeat(beyond + origin.seconds) - origin.beat;

    CHECK(material.beatAtTime(beyond) == Approx(expected));

    // And that is not what this block's own two ends say, which is the whole
    // point: they carry the slope it happened to have before the step.
    const auto straightLine =
        material.beats.start +
        ((beyond - material.seconds.start) / material.seconds.length()) * material.beats.length();

    CHECK(straightLine != Approx(expected));
}

TEST_CASE("A run already under way is continuous with the block before it",
          "[engine][clip][session]") {
    const auto block = blockAt(40);

    // The run began four blocks ago, so this block is one beat into it.
    const magda::engine::MaterialOrigin origin{block.beats.start - 1.0,
                                               block.seconds.start - kSecondsPerBeat};
    const magda::engine::BeatRange whole{block.beats.start, block.beats.end};
    const auto material = magda::engine::materialBlock(block, whole, origin);

    CHECK(material.beats.start == Approx(1.0));
    CHECK(material.seconds.start == Approx(kSecondsPerBeat));
    CHECK(material.continuous);
}

// =============================================================================
// Audio
// =============================================================================

TEST_CASE("A slot nobody launched renders silence", "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    rig.roll(0, 8);

    CHECK(rig.peak() == 0.0f);
    CHECK(rig.handle.playState() == LaunchHandle::PlayState::stopped);
    CHECK(rig.source.starvedVoices() == 0);
}

TEST_CASE("A launched slot plays its material from the origin", "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<CountingReader>());
    rig.publish();

    // Launched well down the timeline: a slot has no position, so where the
    // transport is must not reach the material.
    rig.roll(0, 39);
    rig.handle.play(std::nullopt);
    rig.render(40);

    CHECK(rig.handle.playState() == LaunchHandle::PlayState::playing);
    CHECK(rig.at(0) == Approx(0.0f));
    CHECK(rig.at(1) == Approx(1.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(kBlockSize - 1));

    // And the next block continues it rather than restarting it.
    rig.render(41);
    CHECK(rig.at(0) == Approx(kBlockSize));
}

TEST_CASE("A launch quantized inside a block starts on its beat, not the boundary",
          "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    rig.roll(0, 39);

    // Block 40 runs from monotonic beat 10 to 10.25. Half way through it is
    // beat 10.125, which is sample 3000 of the callback.
    rig.handle.play(10.125);
    rig.render(40);

    CHECK(rig.at(0) == Approx(0.0f));
    CHECK(rig.at(2999) == Approx(0.0f));
    CHECK(rig.at(3000) == Approx(1.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(1.0f));
}

TEST_CASE("A stop quantized inside a block ends on its beat", "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    rig.roll(0, 39);
    rig.handle.play(std::nullopt);
    rig.render(40);
    REQUIRE(rig.at(0) == Approx(1.0f));

    rig.handle.stop(10.375);  // half way through block 41
    rig.render(41);

    CHECK(rig.at(0) == Approx(1.0f));
    CHECK(rig.at(2999) == Approx(1.0f));

    // Past the beat the material is over, and what is left is the ramp that
    // takes the step out of it rather than the material going on (#2302).
    CHECK(rig.at(3000 + kSectionDeClickSamples - 1) == Approx(0.0f).margin(1e-5));
    CHECK(rig.at(3000 + kSectionDeClickSamples) == Approx(0.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(0.0f));

    rig.render(42);
    CHECK(rig.peak() == 0.0f);
}

/**
 * One track with material in both sections and a source for each, sharing the
 * one handle feed (#2302).
 *
 * The arrangement runs the whole time; the session's slot is launched by the
 * case. What is asserted is which of them reaches the output, and how the
 * hand-over between them sounds.
 */
struct SwitchRig {
    /// @p publishHandles false leaves the feed as it is before the store has
    /// ever published, which the contract permits and which a track's sources
    /// have to survive.
    explicit SwitchRig(bool publishHandles = true) {
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene}, .handle = &handle});
        table.entries.push_back(
            LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene + 1}, .handle = &second});

        if (publishHandles)
            handles.publish(std::make_shared<const LaunchHandleTable>(table));

        arrangement.prepare(context());
        session.prepare(context());

        arrangementOut.setSize(2, kBlockSize);
        sessionOut.setSize(2, kBlockSize);
        arrangementOut.clear();
        sessionOut.clear();
    }

    /// A clip on the timeline, sounding @p level from beat zero onwards.
    void giveArrangement(magda::ClipId id, float level) {
        lane.audio.push_back(clipOver(id, beats(0.0, 1000.0)));
        addStream(lane.audio.back(), level);
    }

    /// A slot in @p scene, @p lengthBeats long, sounding @p level.
    void giveSlot(magda::ClipId id, double lengthBeats, float level, int scene = kScene) {
        SessionSlotPlayback slot;
        slot.sceneIndex = scene;
        slot.lengthBeats = lengthBeats;
        slot.audio.push_back(clipOver(id, beats(0.0, lengthBeats)));
        lane.session.push_back(std::move(slot));
        addStream(lane.session.back().audio.front(), level);
    }

    void publish() {
        auto compiled = std::make_shared<ClipSnapshot>();
        compiled->tracks.push_back(lane);
        clips.publish(std::move(compiled));
        streams.publish(std::make_shared<const ClipStreamTable>(streamTable));
    }

    /// Roll one block through the launcher and both sources, in the order the
    /// session does it: every handle advanced before anything renders.
    void render(int index, bool continuous = true) {
        const auto block = blockAt(index, continuous);

        fill();
        magda::engine::advanceLaunchHandles(handles, requests, block);
        arrangement.render(block, juce::dsp::AudioBlock<float>(arrangementOut));
        session.render(block, juce::dsp::AudioBlock<float>(sessionOut));
    }

    void roll(int first, int last) {
        for (auto index = first; index <= last; ++index)
            render(index, index != first);
    }

    /// The session alone, over a block the case assembled itself.
    void renderSession(const BlockInfo& block) {
        fill();
        magda::engine::advanceLaunchHandles(handles, requests, block);
        session.render(block, juce::dsp::AudioBlock<float>(sessionOut)
                                  .getSubBlock(0, static_cast<std::size_t>(block.numSamples)));
    }

    void fill() {
        auto worked = true;
        while (worked) {
            worked = false;
            for (const auto& entry : streamTable.entries)
                worked = entry.stream->fill() || worked;
        }
    }

    float arrangementAt(int sample) const {
        return arrangementOut.getSample(0, sample);
    }
    float sessionAt(int sample) const {
        return sessionOut.getSample(0, sample);
    }

    float arrangementPeak() const {
        return arrangementOut.getMagnitude(0, kBlockSize);
    }
    float sessionPeak() const {
        return sessionOut.getMagnitude(0, kBlockSize);
    }

    /// What the track sounds, which is the sum of its two sections: the one
    /// assertion that says a switch did not leave a hole or double the signal.
    float trackAt(int sample) const {
        return arrangementAt(sample) + sessionAt(sample);
    }

    TrackClipPlayback lane{kTrack, {}, {}};
    ClipSnapshotFeed clips;
    ClipStreamFeed streams;
    LaunchHandle handle;

    /// The scene below, for the cases where one slot hands over to another.
    LaunchHandle second;

    LaunchHandleTable table;
    LaunchHandleFeed handles;
    magda::engine::LaunchRequestQueue requests;

    ClipAudioSource arrangement{kTrack, clips, streams, handles, Section::Arrangement};
    ClipAudioSource session{kTrack, clips, streams, handles, Section::Session};

    ClipStreamTable streamTable;
    juce::AudioBuffer<float> arrangementOut;
    juce::AudioBuffer<float> sessionOut;

  private:
    void addStream(const AudioClipPlayback& clip, float level) {
        const auto& event = clip.events.front();

        auto stream = std::make_shared<PrefetchStream>(
            magda::engine::readThrough(std::make_unique<LevelReader>(level),
                                       magda::engine::sourceReadFor(event, kSampleRate)),
            context(), magda::engine::PrefetchSettings{1024, 8});

        streamTable.entries.push_back(
            ClipStreamTable::Entry{kTrack, clip.clipId, event.eventId, stream});
    }
};

TEST_CASE("The arrangement and the session are different sections of one track",
          "[engine][clip][session]") {
    // A source with no handles plays the arrangement and nothing else.
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    ClipAudioSource arrangement{kTrack, rig.clips, rig.streams};
    arrangement.prepare(context());

    rig.handle.play(std::nullopt);

    const auto block = blockAt(0);
    magda::engine::advanceLaunchHandles(rig.handles, rig.requests, block);

    juce::AudioBuffer<float> out(2, kBlockSize);
    rig.fill();
    arrangement.render(block, juce::dsp::AudioBlock<float>(out));

    CHECK(out.getMagnitude(0, kBlockSize) == 0.0f);  // nothing is in the arrangement
}

// =============================================================================
// Handing a track from one section to the other (#2302)
// =============================================================================

TEST_CASE("A launch takes the track off its arrangement, at the sample it lands on",
          "[engine][clip][session][section]") {
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 0.5f);
    rig.publish();

    // Two blocks of arrangement, then a launch quantized to the beat that falls
    // halfway through the third.
    rig.roll(0, 1);
    REQUIRE(rig.arrangementAt(0) == Approx(1.0f));
    REQUIRE(rig.sessionPeak() == 0.0f);

    rig.handle.play(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto launchSample = kBlockSize / 2;

    SECTION("the arrangement sounds up to the launch and not past it") {
        CHECK(rig.arrangementAt(launchSample - 1) == Approx(1.0f));
        CHECK(rig.arrangementAt(launchSample + kSectionDeClickSamples) == Approx(0.0f));
    }

    SECTION("the session begins there") {
        CHECK(rig.sessionAt(launchSample - 1) == Approx(0.0f));
        CHECK(rig.sessionAt(launchSample) == Approx(0.5f));
    }

    SECTION("and the block after it is the session alone") {
        rig.render(3);
        CHECK(rig.arrangementPeak() == 0.0f);
        CHECK(rig.sessionAt(0) == Approx(0.5f));
    }
}

TEST_CASE("The arrangement is carried down rather than cut off",
          "[engine][clip][session][section]") {
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 0.0f);  // silent, so only the arrangement's edge is read
    rig.publish();

    rig.roll(0, 1);
    rig.handle.play(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto launchSample = kBlockSize / 2;

    SECTION("the first sample past the launch is where the signal was") {
        CHECK(rig.arrangementAt(launchSample) == Approx(1.0f));
    }

    SECTION("and it reaches zero over the ramp, monotonically") {
        for (auto sample = launchSample + 1; sample < launchSample + kSectionDeClickSamples;
             ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.arrangementAt(sample) <= rig.arrangementAt(sample - 1));
        }

        CHECK(rig.arrangementAt(launchSample + kSectionDeClickSamples - 1) ==
              Approx(0.0f).margin(1e-5));
    }
}

TEST_CASE("A stopped slot goes on holding the track", "[engine][clip][session][section]") {
    // The hand-back is not the slot stopping. Silence after a stop is the
    // session still holding the track, exactly as it is in the incumbent.
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 0.5f);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 2);
    REQUIRE(rig.sessionAt(0) == Approx(0.5f));
    REQUIRE(rig.arrangementPeak() == 0.0f);

    rig.handle.stop(std::nullopt);
    rig.render(3);
    rig.render(4);

    CHECK(rig.sessionPeak() == 0.0f);
    CHECK(rig.arrangementPeak() == 0.0f);  // and the arrangement has not crept back
}

TEST_CASE("Releasing the section gives the track back to its arrangement",
          "[engine][clip][session][section]") {
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 0.5f);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 2);
    REQUIRE(rig.arrangementPeak() == 0.0f);

    rig.handle.releaseSection();
    rig.render(3);

    SECTION("the session stops with it") {
        // Its material is gone; what is left in this block is the ramp taking
        // the step out of the sample it stopped on.
        CHECK(rig.sessionAt(0) == Approx(0.5f));
        CHECK(rig.sessionAt(kSectionDeClickSamples - 1) == Approx(0.0f).margin(1e-5));
        CHECK(rig.sessionAt(kSectionDeClickSamples) == Approx(0.0f));
        CHECK(rig.sessionAt(kBlockSize - 1) == Approx(0.0f));

        rig.render(4);
        CHECK(rig.sessionPeak() == 0.0f);
    }

    SECTION("and the arrangement comes back where the timeline is, not where it left off") {
        // It kept rendering all along, so it resumes at the sample the timeline
        // says rather than at the one it was on when it lost the track.
        CHECK(rig.arrangementAt(kSectionDeClickSamples) == Approx(1.0f));
    }

    SECTION("stepping up out of silence over the ramp") {
        CHECK(rig.arrangementAt(0) == Approx(0.0f).margin(1e-5));

        for (auto sample = 1; sample < kSectionDeClickSamples; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.arrangementAt(sample) >= rig.arrangementAt(sample - 1));
        }
    }
}

TEST_CASE("A slot stopping mid-block carries its own last sample down",
          "[engine][clip][session][section]") {
    SwitchRig rig;
    rig.giveArrangement(1, 0.0f);
    rig.giveSlot(2, 4.0, 0.5f);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(0) == Approx(0.5f));

    rig.handle.stop(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto stopSample = kBlockSize / 2;

    CHECK(rig.sessionAt(stopSample - 1) == Approx(0.5f));
    CHECK(rig.sessionAt(stopSample) == Approx(0.5f));
    CHECK(rig.sessionAt(stopSample + kSectionDeClickSamples - 1) == Approx(0.0f).margin(1e-5));

    for (auto sample = stopSample + 1; sample < stopSample + kSectionDeClickSamples; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.sessionAt(sample) <= rig.sessionAt(sample - 1));
    }
}

TEST_CASE("One slot handing over to another carries the outgoing one down",
          "[engine][clip][session][section]") {
    // The incoming clip's own start ramp corrects its own edge and knows
    // nothing about the one it replaced, so without this the track sum steps
    // from the old signal straight to the new (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 0.0f);
    rig.giveSlot(2, 4.0, 0.8f, kScene);
    rig.giveSlot(3, 4.0, -0.3f, kScene + 1);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(0) == Approx(0.8f));

    // The first stops and the second starts on the same beat, half way through
    // the block: the swap a scene launch makes on a track already playing.
    const auto at = kBeatsPerBlock * 2.5;
    rig.handle.stop(at);
    rig.second.play(at);
    rig.render(2);

    const auto swapSample = kBlockSize / 2;

    SECTION("the outgoing level carries on under the incoming one") {
        // 0.8 carried on, with the incoming clip's own opening sample on top of
        // it. That clip begins at its own start, so it is not de-clicked and
        // must not be: the step it makes is its attack (ClipVoice.cpp, #2040).
        CHECK(rig.sessionAt(swapSample - 1) == Approx(0.8f));
        CHECK(rig.sessionAt(swapSample) == Approx(0.8f - 0.3f));
    }

    SECTION("and decays out from under it, leaving the incoming clip alone") {
        CHECK(rig.sessionAt(swapSample + kSectionDeClickSamples) == Approx(-0.3f));

        for (auto sample = swapSample + 1; sample < swapSample + kSectionDeClickSamples; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.sessionAt(sample) <= rig.sessionAt(sample - 1));
        }
    }

    SECTION("so the swap adds no step beyond the incoming clip's own attack") {
        // The whole point. Without the outgoing ramp the sum jumps 0.8 to -0.3
        // in one sample, which is 1.1 of step where the material only ever
        // asked for 0.3.
        for (auto sample = 1; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(std::abs(rig.sessionAt(sample) - rig.sessionAt(sample - 1)) <= 0.3f + 1e-5f);
        }
    }
}

TEST_CASE("A slot swap on a block boundary carries the outgoing one down too",
          "[engine][clip][session][section]") {
    // The boundary-aligned version of the swap above. The handle reports an
    // event on sample zero as a block that was never playing, so the outgoing
    // slot's stop is invisible in the shape of the split; and the incoming
    // slot's own first half does report as playing, so a rule reading either
    // one off beforeEvent gets both of them wrong (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 0.0f);
    rig.giveSlot(2, 400.0, 0.8f, kScene);
    rig.giveSlot(3, 400.0, -0.3f, kScene + 1);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(kBlockSize - 1) == Approx(0.8f));

    // Both requests before the same advance, both as soon as possible, so both
    // events land on the first sample of the next block.
    rig.handle.stop(std::nullopt);
    rig.second.play(std::nullopt);
    rig.render(2);

    SECTION("the outgoing level is carried down from where the last block left it") {
        CHECK(rig.sessionAt(0) == Approx(0.8f - 0.3f));
        CHECK(rig.sessionAt(kSectionDeClickSamples) == Approx(-0.3f));

        for (auto sample = 1; sample < kSectionDeClickSamples; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.sessionAt(sample) <= rig.sessionAt(sample - 1));
        }
    }

    SECTION("so the swap adds no step beyond the incoming clip's own attack") {
        // Without it the seam jumps 0.8 to -0.3 between two callbacks.
        for (auto sample = 1; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(std::abs(rig.sessionAt(sample) - rig.sessionAt(sample - 1)) <= 0.3f + 1e-5f);
        }
    }
}

TEST_CASE("A slot stopping under one that keeps sounding is still ramped",
          "[engine][clip][session][section]") {
    // The case one ramp over the track's sum could not do, and the reason the
    // ramp belongs to the voice: the outgoing slot's own 0.5 is carried down
    // while the other slot's 0.5 goes on sounding through the same samples,
    // rather than the stop being passed over because something else was still
    // playing (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 0.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.giveSlot(3, 400.0, 0.5f, kScene + 1);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.second.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(0) == Approx(1.0f));

    rig.handle.stop(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto stopSample = kBlockSize / 2;

    SECTION("the outgoing half decays rather than vanishing") {
        CHECK(rig.sessionAt(stopSample - 1) == Approx(1.0f));
        CHECK(rig.sessionAt(stopSample) == Approx(1.0f));

        for (auto sample = stopSample + 1; sample < stopSample + kSectionDeClickSamples; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.sessionAt(sample) <= rig.sessionAt(sample - 1));
        }
    }

    SECTION("leaving exactly the slot that is still playing") {
        CHECK(rig.sessionAt(stopSample + kSectionDeClickSamples) == Approx(0.5f));
        CHECK(rig.sessionAt(kBlockSize - 1) == Approx(0.5f));
    }

    SECTION("and the track never steps") {
        for (auto sample = 1; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(std::abs(rig.sessionAt(sample) - rig.sessionAt(sample - 1)) < 0.05f);
        }
    }
}

TEST_CASE("A session stop sounds the same however the callback is cut up",
          "[engine][clip][session][section]") {
    // Block size is an I/O batching concept and never a precision one
    // (RenderContext.hpp). The ramp after a stop is carried across blocks for
    // exactly this reason, so a stop rendered in one block and the same stop
    // rendered in eight-sample blocks have to agree sample for sample
    // (#2344 review).
    const auto tailOf = [](int blockSize) {
        SwitchRig rig;
        rig.giveArrangement(1, 0.0f);
        rig.giveSlot(2, 400.0, 0.5f);
        rig.publish();

        rig.handle.play(std::nullopt);

        // Up to the stop in whole blocks, so both runs start it from the same
        // place, then onwards in blocks of the size under test.
        rig.roll(0, 1);

        std::vector<float> tail;
        auto sample = static_cast<std::int64_t>(2 * kBlockSize);

        // Stopped as soon as possible, so it lands on the first sample of the
        // block after this one whatever that block's size is.
        rig.handle.stop(std::nullopt);

        while (static_cast<int>(tail.size()) < kSectionDeClickSamples * 2) {
            rig.renderSession(smallBlockAt(sample, blockSize));

            for (auto index = 0; index < blockSize; ++index)
                tail.push_back(rig.sessionAt(index));

            sample += blockSize;
        }

        return tail;
    };

    const auto whole = tailOf(kSectionDeClickSamples * 2);
    const auto chopped = tailOf(8);

    for (auto sample = 0; sample < kSectionDeClickSamples * 2; ++sample) {
        INFO("sample " << sample);
        REQUIRE(chopped[static_cast<std::size_t>(sample)] ==
                Approx(whole[static_cast<std::size_t>(sample)]).margin(1e-6));
    }

    // A decay to zero rather than a staircase restarting at every seam.
    CHECK(chopped[0] == Approx(0.5f));
    CHECK(chopped[kSectionDeClickSamples - 1] == Approx(0.0f).margin(1e-5));

    for (auto sample = 1; sample < kSectionDeClickSamples; ++sample) {
        INFO("sample " << sample);
        REQUIRE(chopped[static_cast<std::size_t>(sample)] <=
                chopped[static_cast<std::size_t>(sample) - 1]);
    }
}

TEST_CASE("A stop inside another slot's unfinished ramp starts from its own level",
          "[engine][clip][session][section]") {
    // Slot A hands over to B near a block end, A's ramp carries into the next
    // block, and B stops on the boundary after that. With one ramp over the
    // track's sum, B's tail would have started from A's anchor or from a
    // half decayed correction, whichever the aggregate happened to hold. Two
    // voices, two ramps, and neither can reach the other (#2344 review).
    constexpr int kSmall = 16;  // so a 32-sample ramp spans blocks

    SwitchRig rig;
    rig.giveArrangement(1, 0.0f);
    rig.giveSlot(2, 400.0, 0.8f, kScene);
    rig.giveSlot(3, 400.0, -0.3f, kScene + 1);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(kBlockSize - 1) == Approx(0.8f));

    std::vector<float> tail;
    auto at = static_cast<std::int64_t>(2 * kBlockSize);

    const auto step = [&] {
        rig.renderSession(smallBlockAt(at, kSmall));
        for (auto index = 0; index < kSmall; ++index)
            tail.push_back(rig.sessionAt(index));
        at += kSmall;
    };

    // A out, B in, on the same sample.
    rig.handle.stop(std::nullopt);
    rig.second.play(std::nullopt);
    step();

    // B out while A's ramp is still running.
    rig.second.stop(std::nullopt);
    step();

    // And on, until both ramps are spent.
    for (auto index = 0; index < 6; ++index)
        step();

    SECTION("B's own level is what its tail starts from") {
        // A's ramp is 16 samples in and worth 0.8 * cos-decay; B contributes
        // its own -0.3 carried down from where it was. Neither is the other's.
        REQUIRE(tail[kSmall] < tail[kSmall - 1] + 1e-5f);
        REQUIRE(tail[kSmall] > -0.3f);
    }

    SECTION("and nothing steps anywhere across either seam") {
        for (std::size_t sample = 1; sample < tail.size(); ++sample) {
            INFO("sample " << sample);
            REQUIRE(std::abs(tail[sample] - tail[sample - 1]) <= 0.3f + 1e-5f);
        }
    }

    SECTION("ending in silence once both are spent") {
        CHECK(tail.back() == Approx(0.0f).margin(1e-5));
    }
}

TEST_CASE("A release and a relaunch in one block leave the arrangement the gap between",
          "[engine][clip][session][section]") {
    // The block a final state cannot describe. The session held the track, Back
    // to Arrangement gives it up at sample zero, and another slot takes it back
    // part way through: the arrangement owns everything in between and sounded
    // none of it while ownership was one answer per block (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.giveSlot(3, 400.0, 0.5f, kScene + 1);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.arrangementPeak() == 0.0f);

    // Both before the same advance: the release applies on the block's first
    // sample, the launch on its own.
    rig.handle.releaseSection();
    rig.second.play(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto relaunch = kBlockSize / 2;

    SECTION("the arrangement sounds the gap") {
        CHECK(rig.arrangementAt(kSectionDeClickSamples) == Approx(1.0f));
        CHECK(rig.arrangementAt(relaunch - 1) == Approx(1.0f));
    }

    SECTION("and loses the track again at the relaunch") {
        CHECK(rig.arrangementAt(relaunch + kSectionDeClickSamples) == Approx(0.0f));
        CHECK(rig.arrangementAt(kBlockSize - 1) == Approx(0.0f));
    }

    SECTION("the session is silent across the gap and back for the rest") {
        CHECK(rig.sessionAt(kSectionDeClickSamples) == Approx(0.0f).margin(1e-5));
        CHECK(rig.sessionAt(relaunch) == Approx(0.5f));
        CHECK(rig.sessionAt(kBlockSize - 1) == Approx(0.5f));
    }
}

TEST_CASE("A release and a relaunch in one block put the arrangement's MIDI in the gap",
          "[engine][clip][session][section]") {
    // The same block, on the other source: they read one fold, so they are on
    // the same side of both switches.
    MidiSwitchRig rig;
    rig.publish({arrangementMidiClip(1, 0.0, 16.0, {MidiNote{64, 100, 0.55, 0.2, 0, {}}})},
                {sessionMidiClip(2, 16.0, {MidiNote{67, 100, 0.0, 16.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.notesOn(rig.fromArrangement).empty());

    // Beat 0.5 to 0.75 is block 2; the arrangement's note is at 0.55, inside the
    // gap, and the relaunch is at 0.625 which is half way through.
    rig.handle.releaseSection();
    rig.second.play(kBeatsPerBlock * 2.5);
    rig.roll(2, 2);

    SECTION("the note in the gap sounds") {
        const auto ons = rig.notesOn(rig.fromArrangement);
        REQUIRE(ons.size() == 1);
        CHECK(ons.front().message.getNoteNumber() == 64);
        CHECK(ons.front().sample < kBlockSize / 2);
    }

    SECTION("and is ended when the session takes the track back") {
        CHECK(rig.hanging(rig.fromArrangement).empty());
    }
}

TEST_CASE("A launch arriving after a release keeps the track it was holding",
          "[engine][clip][session][section]") {
    // One handle, both requests, before the same advance. The stop and the
    // hand-back are one request, so the launch replaces both: split across two
    // fields it replaced only the stop, and the track was handed to the
    // arrangement while the slot went on sounding (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.sessionAt(0) == Approx(0.5f));
    REQUIRE(rig.arrangementPeak() == 0.0f);

    // Back to Arrangement, then a launch queued for a beat away, both before
    // the next block.
    rig.handle.releaseSection();
    rig.handle.play(kBeatsPerBlock * 8.0);
    rig.render(2);

    SECTION("the slot goes on sounding") {
        CHECK(rig.sessionAt(0) == Approx(0.5f));
        CHECK(rig.sessionAt(kBlockSize - 1) == Approx(0.5f));
    }

    SECTION("and the arrangement stays off it, so the track sounds one section") {
        CHECK(rig.arrangementPeak() == 0.0f);

        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.trackAt(sample) == Approx(0.5f));
        }
    }
}

TEST_CASE("A release on its own still hands the track back", "[engine][clip][session][section]") {
    // The other side of the same rule: nothing replaced it, so it applies.
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.publish();

    rig.handle.play(std::nullopt);
    rig.roll(0, 1);
    REQUIRE(rig.arrangementPeak() == 0.0f);

    rig.handle.releaseSection();
    rig.render(2);

    CHECK(rig.arrangementAt(kSectionDeClickSamples) == Approx(1.0f));
    CHECK(rig.sessionAt(kSectionDeClickSamples) == Approx(0.0f).margin(1e-5));
}

TEST_CASE("A slot swap on a boundary leaves no ghost of the arrangement",
          "[engine][clip][session][section]") {
    // The arrangement owns nothing in this block: one slot is released and
    // another launches on the same sample, so its span is zero samples long.
    // Expressed as endpoints plus flags that reads as "handed back, then lost
    // at zero", and the stop ramp decayed whatever the arrangement last
    // pushed, which was from before the session ever took the track
    // (#2344 review).
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.giveSlot(3, 400.0, 0.5f, kScene + 1);
    rig.publish();

    // The arrangement sounds first, so there is a stale sample to leak.
    rig.roll(0, 1);
    REQUIRE(rig.arrangementAt(0) == Approx(1.0f));

    // Then the session takes the track and keeps it for a while.
    rig.handle.play(std::nullopt);
    rig.roll(2, 4);
    REQUIRE(rig.arrangementPeak() == 0.0f);
    REQUIRE(rig.sessionAt(0) == Approx(0.5f));

    // Release one slot and launch the other, both as soon as possible, so both
    // land on this block's first sample.
    rig.handle.releaseSection();
    rig.second.play(std::nullopt);
    rig.render(5);

    SECTION("the arrangement stays silent") {
        CHECK(rig.arrangementPeak() == 0.0f);
    }

    SECTION("and the track is the two slots swapping, and nothing else") {
        // The outgoing slot's own ramp is there and belongs there: its voice
        // carries its 0.5 down while the incoming slot's 0.5 sounds under it.
        // What must not be there is a third thing.
        CHECK(rig.sessionAt(0) == Approx(1.0f));
        CHECK(rig.sessionAt(kSectionDeClickSamples) == Approx(0.5f));
        CHECK(rig.sessionAt(kBlockSize - 1) == Approx(0.5f));

        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.trackAt(sample) == Approx(rig.sessionAt(sample)));
        }
    }
}

TEST_CASE("A track whose handles have not been published yet plays its arrangement",
          "[engine][clip][session][section]") {
    // The feed is null until the store publishes, which the contract permits
    // and which is a session whose slots have no handles yet rather than an
    // error. Nothing can hold a track then, so the arrangement has all of it:
    // a fallback that said otherwise would silence every arrangement on the
    // blocks before the first publish (#2344 review).
    SwitchRig rig{false};
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 400.0, 0.5f, kScene);
    rig.publish();

    // Requested anyway, which changes nothing: no table means no handle the
    // sources can reach.
    rig.handle.play(std::nullopt);
    rig.roll(0, 2);

    CHECK(rig.arrangementAt(0) == Approx(1.0f));
    CHECK(rig.arrangementAt(kBlockSize - 1) == Approx(1.0f));
    CHECK(rig.sessionPeak() == 0.0f);
}

TEST_CASE("The same track's MIDI plays its arrangement too", "[engine][clip][session][section]") {
    MidiSwitchRig rig{false};
    rig.publish({arrangementMidiClip(1, 0.0, 16.0, {MidiNote{64, 100, 0.3, 1.0, 0, {}}})},
                {sessionMidiClip(2, 16.0, {MidiNote{67, 100, 0.0, 16.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 4);

    const auto ons = rig.notesOn(rig.fromArrangement);
    REQUIRE(ons.size() == 1);
    CHECK(ons.front().message.getNoteNumber() == 64);
    CHECK(rig.notesOn(rig.fromSession).empty());
}

TEST_CASE("A track never sounds both of its sections", "[engine][clip][session][section]") {
    // The property the switch exists for. The two sections are summed into one
    // track input, so a sample both contribute material to is a track playing
    // itself twice.
    //
    // The hand-over ramp is the one exception and is not one of those: what
    // overlaps there is the outgoing section's last sample decaying, which is
    // the step being taken out rather than a second signal.
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 1.0f);
    rig.publish();

    rig.roll(0, 1);
    rig.handle.play(kBeatsPerBlock * 2.5);

    const auto launchSample = kBlockSize / 2;

    for (auto index = 2; index <= 6; ++index) {
        rig.render(index);

        for (auto sample = 0; sample < kBlockSize; ++sample) {
            // Everything except the ramp that follows the launch.
            if (index == 2 && sample >= launchSample &&
                sample < launchSample + kSectionDeClickSamples)
                continue;

            INFO("block " << index << " sample " << sample);
            REQUIRE(rig.trackAt(sample) <= 1.0f + 1e-5f);
        }
    }
}

TEST_CASE("The arrangement's MIDI sounds up to the hand-over and owes note-offs there",
          "[engine][clip][session][section]") {
    MidiSwitchRig rig;

    // An arrangement note that begins before the launch and would run well past
    // it, and a slot with a note of its own.
    rig.publish({arrangementMidiClip(1, 0.0, 8.0, {MidiNote{60, 100, 0.25, 4.0, 0, {}}})},
                {sessionMidiClip(2, 4.0, {MidiNote{67, 100, 0.0, 2.0, 0, {}}})});

    rig.roll(0, 1);
    REQUIRE(rig.notesOn(rig.fromArrangement).size() == 1);
    REQUIRE(rig.notesOn(rig.fromArrangement).front().message.getNoteNumber() == 60);

    // A launch quantized to the middle of the next block.
    rig.handle.play(kBeatsPerBlock * 2.5);
    rig.roll(2, 4);

    const auto launchSample = kBlockSize / 2;

    SECTION("the arrangement's note ends where the session takes the track") {
        const auto offs = rig.notesOff(rig.fromArrangement);
        REQUIRE(offs.size() == 1);
        CHECK(offs.front().block == 2);
        CHECK(offs.front().sample == launchSample);
        CHECK(offs.front().message.getNoteNumber() == 60);
    }

    SECTION("and nothing of the arrangement's is left sounding") {
        CHECK(rig.hanging(rig.fromArrangement).empty());
    }

    SECTION("the session's note starts there") {
        const auto ons = rig.notesOn(rig.fromSession);
        REQUIRE(ons.size() == 1);
        CHECK(ons.front().block == 2);
        CHECK(ons.front().sample == launchSample);
        CHECK(ons.front().message.getNoteNumber() == 67);
    }

    SECTION("and the arrangement emits nothing at all afterwards") {
        const auto afterward = std::count_if(rig.fromArrangement.begin(), rig.fromArrangement.end(),
                                             [](const Captured& entry) { return entry.block > 2; });
        CHECK(afterward == 0);
    }
}

TEST_CASE("A note due before the hand-over still sounds", "[engine][clip][session][section]") {
    // The arrangement's MIDI runs up to the switch the way its audio renders up
    // to it: a note due a sample before the launch is one the listener was
    // going to hear.
    MidiSwitchRig rig;
    rig.publish({arrangementMidiClip(1, 0.0, 8.0, {MidiNote{62, 100, 0.6, 0.1, 0, {}}})},
                {sessionMidiClip(2, 4.0, {})});

    rig.roll(0, 1);
    REQUIRE(rig.notesOn(rig.fromArrangement).empty());

    // Beat 0.625 is the middle of block 2; the note is at 0.6, just inside it.
    rig.handle.play(kBeatsPerBlock * 2.5);
    rig.roll(2, 2);

    const auto ons = rig.notesOn(rig.fromArrangement);
    REQUIRE(ons.size() == 1);
    CHECK(ons.front().message.getNoteNumber() == 62);
    CHECK(ons.front().sample < kBlockSize / 2);
    CHECK(rig.hanging(rig.fromArrangement).empty());
}

TEST_CASE("Releasing the section brings the arrangement's MIDI back",
          "[engine][clip][session][section]") {
    MidiSwitchRig rig;
    rig.publish({arrangementMidiClip(1, 0.0, 8.0, {MidiNote{64, 100, 2.0, 1.0, 0, {}}})},
                {sessionMidiClip(2, 8.0, {MidiNote{67, 100, 0.0, 8.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 4);
    REQUIRE(rig.notesOn(rig.fromSession).size() == 1);

    rig.handle.releaseSection();
    rig.roll(5, 12);  // through beat 2, where the arrangement's note is

    SECTION("the session's note is ended by the release") {
        CHECK(rig.hanging(rig.fromSession).empty());
    }

    SECTION("and the arrangement plays what the timeline has reached") {
        const auto ons = rig.notesOn(rig.fromArrangement);
        REQUIRE(ons.size() == 1);
        CHECK(ons.front().message.getNoteNumber() == 64);
    }
}

TEST_CASE("A launched slot raises a panic on the block it starts",
          "[engine][clip][session][2418]") {
    // A discontinuity the transport never moved for. A device downstream is
    // holding what the slot before it played and has nothing else to hear it
    // from: the flag beside the port is what says so (#2418).
    MidiRig rig;
    rig.publish({sessionMidiClip(1, 4.0, {MidiNote{60, 100, 0.0, 1.0, 0, {}}})});

    rig.roll(0, 3);
    CHECK(rig.panicBlocks().empty());

    rig.handle.play(std::nullopt);
    rig.roll(4, 8);

    // The block the launch fires in, and no block after it: a slot that goes on
    // playing is not a discontinuity every block.
    CHECK(rig.panicBlocks() == std::vector<int>{4});
}

TEST_CASE("Handing a track back to its arrangement raises a panic",
          "[engine][clip][session][2418]") {
    // The mirror of the launch above, and the same reason the hand-back sets
    // BlockInfo::continuous false for the chase below it.
    MidiSwitchRig rig;
    rig.publish({arrangementMidiClip(1, 0.0, 16.0, {MidiNote{64, 100, 0.5, 12.0, 0, {}}})},
                {sessionMidiClip(2, 16.0, {MidiNote{67, 100, 0.0, 16.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 4);
    const auto beforeRelease = rig.arrangementPanics;
    CHECK(std::count(beforeRelease.begin(), beforeRelease.end(), true) == 0);

    rig.handle.releaseSection();
    rig.roll(5, 8);

    std::vector<int> raised;
    for (std::size_t index = 0; index < rig.arrangementPanics.size(); ++index)
        if (rig.arrangementPanics[index])
            raised.push_back(static_cast<int>(index));

    CHECK(raised == std::vector<int>{5});
}

TEST_CASE("A hand-back inside a sustained note strikes it rather than waiting",
          "[engine][clip][session][section]") {
    // The hand-back is a discontinuity even though the transport never moved:
    // the arrangement's notes were ended at the hand-over and the timeline ran
    // on underneath. Releasing into the middle of a pad has to strike it, the
    // way locating into one does, or the track is silent until the next onset
    // (#2344 review).
    MidiSwitchRig rig;
    rig.publish({arrangementMidiClip(1, 0.0, 16.0, {MidiNote{64, 100, 0.5, 12.0, 0, {}}})},
                {sessionMidiClip(2, 16.0, {MidiNote{67, 100, 0.0, 16.0, 0, {}}})});

    // Launch while the arrangement's note is already sounding, so the hand-over
    // ends it and the timeline runs well past its onset.
    rig.roll(0, 2);
    REQUIRE(rig.notesOn(rig.fromArrangement).size() == 1);

    rig.handle.play(std::nullopt);
    rig.roll(3, 12);
    REQUIRE(rig.hanging(rig.fromArrangement).empty());  // ended at the hand-over

    const auto before = rig.notesOn(rig.fromArrangement).size();

    // Beat 3 by now, which is inside the note and nowhere near its onset.
    rig.handle.releaseSection();
    rig.roll(13, 13);

    const auto ons = rig.notesOn(rig.fromArrangement);
    REQUIRE(ons.size() == before + 1);
    CHECK(ons.back().block == 13);
    CHECK(ons.back().message.getNoteNumber() == 64);
}

TEST_CASE("The hand-over overlap is the ramp and no longer", "[engine][clip][session][section]") {
    // What the outgoing section adds past its own ramp is nothing at all, which
    // is what says the de-click is a correction to an edge rather than a fade
    // the two sections share.
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 1.0f);
    rig.publish();

    rig.roll(0, 1);
    rig.handle.play(kBeatsPerBlock * 2.5);
    rig.render(2);

    const auto launchSample = kBlockSize / 2;

    CHECK(rig.arrangementAt(launchSample + kSectionDeClickSamples - 1) ==
          Approx(0.0f).margin(1e-5));
    CHECK(rig.arrangementAt(launchSample + kSectionDeClickSamples) == Approx(0.0f));
    CHECK(rig.arrangementAt(kBlockSize - 1) == Approx(0.0f));

    // And the incoming one is at full level throughout, never attenuated by the
    // hand-over: a launch is not a gain fade (FadeCurves.hpp).
    for (auto sample = launchSample; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.sessionAt(sample) == Approx(1.0f));
    }
}

TEST_CASE("A launch on the first sample of a block is the same switch",
          "[engine][clip][session][section]") {
    // Which side of a callback boundary the launch fell on is not something the
    // hand-over should be able to hear: the step comes off the block before.
    SwitchRig rig;
    rig.giveArrangement(1, 1.0f);
    rig.giveSlot(2, 4.0, 0.0f);
    rig.publish();

    rig.roll(0, 1);
    rig.handle.play(kBeatsPerBlock * 2.0);
    rig.render(2);

    CHECK(rig.arrangementAt(0) == Approx(1.0f));
    CHECK(rig.arrangementAt(kSectionDeClickSamples - 1) == Approx(0.0f).margin(1e-5));
    CHECK(rig.arrangementAt(kSectionDeClickSamples) == Approx(0.0f));
}

// =============================================================================
// One advance per block
// =============================================================================

TEST_CASE("A handle is advanced once per block however many sources read it",
          "[engine][clip][session]") {
    LaunchHandle handle;
    LaunchHandleTable table;
    table.entries.push_back(
        LaunchHandleTable::Entry{.key = SlotKey{kTrack, kScene}, .handle = &handle});

    LaunchHandleFeed feed;
    feed.publish(std::make_shared<const LaunchHandleTable>(table));
    magda::engine::LaunchRequestQueue requests;

    handle.play(std::nullopt);
    magda::engine::advanceLaunchHandles(feed, requests, blockAt(0));
    magda::engine::advanceLaunchHandles(feed, requests, blockAt(1));

    // Two blocks of a quarter beat each. Advanced per source it would have
    // moved twice as far, and a slot's audio and MIDI would disagree.
    const auto played = handle.playedRange();
    REQUIRE(played.has_value());
    CHECK(played->length() == Approx(2 * kBeatsPerBlock));

    // And what the sources read is what that last advance said.
    CHECK(handle.blockStatus().beforeEvent.playing());
    CHECK(handle.blockStatus().beforeEvent.range.start == Approx(kBeatsPerBlock));
}

TEST_CASE("A slot with no handle in the table is silent rather than wrong",
          "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    // The table catches up on its own schedule, so a slot can exist with
    // nothing to launch it.
    rig.handles.publish(std::make_shared<const LaunchHandleTable>());
    rig.handle.play(std::nullopt);

    rig.roll(0, 4);
    CHECK(rig.peak() == 0.0f);
}

// =============================================================================
// MIDI
// =============================================================================

TEST_CASE("A launched slot plays its MIDI from the origin", "[engine][clip][session][midi]") {
    MidiRig rig;
    rig.publish({sessionMidiClip(1, 4.0, {MidiNote{60, 100, 0.0, 1.0, 0, {}}})});

    rig.roll(0, 39);
    rig.handle.play(std::nullopt);
    rig.roll(40, 48);

    const auto ons = rig.noteOns();
    REQUIRE(ons.size() == 1);

    // Beat zero of the material, so it lands on the first sample of the block
    // the launch fired in, not nine beats back at beat zero of the timeline.
    CHECK(ons.front().block == 40);
    CHECK(ons.front().sample == 0);
    CHECK(ons.front().message.getNoteNumber() == 60);

    // One beat long, which is four blocks on.
    const auto offs = rig.noteOffs();
    REQUIRE(offs.size() == 1);
    CHECK(offs.front().block == 44);
    CHECK(rig.hanging().empty());
}

TEST_CASE("Stopping a slot ends the notes it started", "[engine][clip][session][midi]") {
    MidiRig rig;
    rig.publish({sessionMidiClip(1, 4.0, {MidiNote{60, 100, 0.0, 4.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 2);
    REQUIRE(rig.noteOns().size() == 1);

    // Half a beat in, with three and a half to run: nothing in the material
    // ends it, so the stop owes the note-off.
    rig.handle.stop(std::nullopt);
    rig.roll(3, 6);

    const auto offs = rig.noteOffs();
    REQUIRE(offs.size() == 1);
    CHECK(offs.front().block == 3);
    CHECK(rig.hanging().empty());
}

TEST_CASE("A slot nobody launched plays no MIDI", "[engine][clip][session][midi]") {
    MidiRig rig;
    rig.publish({sessionMidiClip(1, 4.0, {MidiNote{60, 100, 0.0, 1.0, 0, {}}})});

    rig.roll(0, 16);

    CHECK(rig.captured.empty());
}

// =============================================================================
// Handles across a publish
// =============================================================================

namespace {

/// A factory that builds nothing: handles are the engine's own state.
class NoFactory final : public magda::engine::RuntimeStateFactory {};

ClipSnapshot snapshotWithSlots(std::vector<int> scenes) {
    ClipSnapshot snapshot;
    TrackClipPlayback track;
    track.trackId = kTrack;

    for (const auto scene : scenes) {
        SessionSlotPlayback slot;
        slot.sceneIndex = scene;
        slot.lengthBeats = 4.0;
        slot.audio.push_back(clipOver(static_cast<magda::ClipId>(100 + scene), beats(0.0, 4.0)));
        track.session.push_back(std::move(slot));
    }

    snapshot.tracks.push_back(std::move(track));
    return snapshot;
}

}  // namespace

TEST_CASE("A handle table names every slot, in order, and keeps the handles it had",
          "[engine][clip][session]") {
    NoFactory factory;
    magda::engine::RuntimeStateStore store{factory};
    LaunchHandleFeed feed;
    magda::engine::LaunchRequestQueue requests;

    const auto first = store.publishHandles(snapshotWithSlots({2, 0, 1}), feed, requests);
    REQUIRE(first->entries.size() == 3);
    CHECK(first->entries[0].key.sceneIndex == 0);
    CHECK(first->entries[1].key.sceneIndex == 1);
    CHECK(first->entries[2].key.sceneIndex == 2);
    CHECK(first->find(SlotKey{kTrack, 1}) != nullptr);
    CHECK(first->find(SlotKey{kTrack, 9}) == nullptr);

    // An unrelated clip edit republishes the snapshot. The handle has to be the
    // same object, or the clip restarts under a gesture nobody made.
    auto* playing = first->find(SlotKey{kTrack, 1});
    playing->play(std::nullopt);
    playing->advance(magda::engine::syncRangeFor(blockAt(0)));
    REQUIRE(playing->playState() == LaunchHandle::PlayState::playing);

    const auto second = store.publishHandles(snapshotWithSlots({0, 1, 2}), feed, requests);

    // Same slots, same handles: a clip edit that did not touch the session must
    // not make the callback wait.
    CHECK(second == first);
    CHECK(second->find(SlotKey{kTrack, 1}) == playing);
    CHECK(playing->playState() == LaunchHandle::PlayState::playing);
}

TEST_CASE("A slot emptied and refilled does not come back playing", "[engine][clip][session]") {
    // Two clip edits and no structural one, so only the publish itself can
    // retire the handle. Left to a plan publish the new clip comes up playing.
    NoFactory factory;
    magda::engine::RuntimeStateStore store{factory};
    LaunchHandleFeed feed;
    magda::engine::LaunchRequestQueue requests;

    store.publishHandles(snapshotWithSlots({0, 1}), feed, requests);

    auto* playing = store.findHandle(SlotKey{kTrack, 0});
    REQUIRE(playing != nullptr);
    playing->play(std::nullopt);
    playing->advance(magda::engine::syncRangeFor(blockAt(0)));
    REQUIRE(playing->playState() == LaunchHandle::PlayState::playing);

    store.publishHandles(snapshotWithSlots({1}), feed, requests);
    CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);

    store.publishHandles(snapshotWithSlots({0, 1}), feed, requests);

    auto* refilled = store.findHandle(SlotKey{kTrack, 0});
    REQUIRE(refilled != nullptr);
    CHECK(refilled->playState() == LaunchHandle::PlayState::stopped);
}

TEST_CASE("A slot launched where the tempo differs reads forward, not twice",
          "[engine][clip][session]") {
    // A slot is compiled at the origin, so its beats are the origin's beats.
    // Its seconds are not the origin's seconds: they are how long the run has
    // been going, because that is what a reader consumes material at. Putting
    // the material beat back through the tempo map instead would hand the block
    // a seconds span half its sample span in a section at half the tempo, and
    // the next block would ask for a position this one had already read past.
    const magda::engine::TempoMap map({{0.0, 120.0, 0.0f}, {8.0, 120.0, 0.0f}, {8.0, 60.0, 0.0f}},
                                      {{0.0, 4, 4}});

    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<CountingReader>());
    rig.publish(&map);

    // Beat 8 is four seconds in, and everything from there runs at half the
    // tempo the slot was compiled at.
    const auto launchSeconds = map.beatToTime(12.0);

    rig.renderBlock(blockOnMap(map, launchSeconds - (kBlockSize / kSampleRate), false));
    rig.handle.play(std::nullopt);

    rig.renderBlock(blockOnMap(map, launchSeconds));
    CHECK(rig.at(0) == Approx(0.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(kBlockSize - 1));

    // One block on, and one block further into the file. Under a conversion
    // through the origin's tempo this would be sample 3000, and half the block
    // would sound twice.
    rig.renderBlock(blockOnMap(map, launchSeconds + (kBlockSize / kSampleRate)));
    CHECK(rig.at(0) == Approx(kBlockSize));

    rig.renderBlock(blockOnMap(map, launchSeconds + (2 * kBlockSize / kSampleRate)));
    CHECK(rig.at(0) == Approx(2 * kBlockSize));
}

// =============================================================================
// The clocks that do not go back
// =============================================================================

namespace {

/// 120 bpm to beat 4 and 60 bpm from there: #2324's shape. Two changes at the
/// one beat, because a single change ramps to its bpm rather than stepping to
/// it and the halves either side of beat 4 are the whole point (TempoMap.hpp).
magda::engine::TempoMap steppedTempo() {
    return magda::engine::TempoMap({{0.0, 120.0, 0.0f}, {4.0, 120.0, 0.0f}, {4.0, 60.0, 0.0f}},
                                   {{0.0, 4, 4}});
}

/// Playing from @p fromBeat on @p map, with the metronome off.
magda::engine::TransportSnapshot rollingFrom(const magda::engine::TempoMap& map, double fromBeat,
                                             std::uint64_t generation = 1) {
    magda::engine::TransportSnapshot snapshot;
    snapshot.tempo = map;
    snapshot.request.generation = generation;
    snapshot.request.playing = true;
    snapshot.request.positionBeat = fromBeat;
    return snapshot;
}

/// One callback, cut into blocks by the clock. The rig's buffer holds the last
/// segment rendered.
void callback(AudioRig& rig, magda::engine::TransportClock& clock,
              const magda::engine::TransportSnapshot& snapshot) {
    for (const auto& segment : clock.advance(snapshot, kSampleRate, kBlockSize))
        rig.renderBlock(segment.block);
}

}  // namespace

// =============================================================================
// The map a snapshot was compiled for
// =============================================================================

TEST_CASE("A snapshot compiled against another tempo map is counted",
          "[engine][clip][session][tempo]") {
    // A snapshot carries the fingerprint of the map its seconds came through.
    // One that is not the transport's was compiled against a tempo that has
    // since changed, and every second in it is wrong by however much the map
    // moved.
    //
    // It still plays. What it does not do is pass unnoticed: the publish is
    // meant to swap the map and the snapshot compiled for it together, and a
    // count above zero is how anyone finds out that it did not (#2337).
    const auto map = steppedTempo();
    const magda::engine::TempoMap other({{0.0, 90.0, 0.0f}}, {{0.0, 4, 4}});

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<ConstantReader>());
    rig.publish(&other);

    rig.handle.play(std::nullopt);
    rig.renderBlock(blockOnMap(map, map.beatToTime(4.0)));

    CHECK(rig.source.staleSnapshots() == 1);
}

TEST_CASE("A snapshot that is the map's is not counted", "[engine][clip][session][tempo]") {
    // The other half: the count is the fingerprint's doing rather than
    // something every block does.
    const auto map = steppedTempo();

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<ConstantReader>());
    rig.publish(&map);

    rig.handle.play(std::nullopt);
    rig.renderBlock(blockOnMap(map, map.beatToTime(4.0)));

    CHECK(rig.peak() == Approx(1.0f));
    CHECK(rig.source.staleSnapshots() == 0);
}

TEST_CASE("A stale snapshot does not cost the notes it started", "[engine][clip][session][tempo]") {
    // Counted, not cut off. A mismatch is a publish-ordering bug, and taking
    // the notes away to report it would put a hole in the middle of a set to
    // say so.
    MidiRig rig;
    rig.publish({sessionMidiClip(1, 4.0, {MidiNote{60, 100, 0.0, 4.0, 0, {}}})});

    rig.handle.play(std::nullopt);
    rig.roll(0, 2);

    REQUIRE(rig.noteOns().size() == 1);
    REQUIRE(rig.hanging().size() == 1);

    const magda::engine::TempoMap other({{0.0, 90.0, 0.0f}}, {{0.0, 4, 4}});
    auto block = blockAt(3);
    block.tempo = &other;

    juce::MidiBuffer buffer;
    rig.source.render(block, buffer);

    CHECK(rig.source.staleSnapshots() == 1);

    // Still sounding: nothing was ended to report the mismatch.
    auto offs = 0;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOff())
            ++offs;

    CHECK(offs == 0);
}

TEST_CASE("A launch inside a block that spans a tempo step names one instant",
          "[engine][clip][session]") {
    // The invariant #2330 is about, asserted on a block the clock would no
    // longer hand out: built by hand so it spans the step, which is exactly the
    // shape that used to produce an origin with two faces of different moments.
    //
    // The cut and this are two answers to the same leak, and only this one
    // closes the class: whatever a block spans, an instant inside it is worked
    // out once, in samples, and every face derived from that.
    const auto map = steppedTempo();  // 120 to beat 4, then 60

    // Half a block either side of the step, at 120 throughout its first half.
    const auto from = map.beatToTime(4.0) - (kBlockSize / kSampleRate / 2.0);

    magda::engine::BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.continuous = true;
    block.seconds = {from, from + (kBlockSize / kSampleRate)};
    block.beats = {map.timeToBeat(block.seconds.start), map.timeToBeat(block.seconds.end)};
    block.monotonicBeats = block.beats;
    block.monotonicSeconds = block.seconds;
    block.tempo = &map;

    LaunchHandle handle;
    handle.play(map.timeToBeat(map.beatToTime(4.0)));  // the step itself

    const auto status = handle.advance(magda::engine::syncRangeFor(block));

    REQUIRE(status.afterEvent.has_value());
    REQUIRE(status.afterEvent->origin.has_value());

    const auto origin = *status.afterEvent->origin;

    // The two faces of a fresh run's origin are the two faces of one sample.
    // Projected separately they are not: the beat lands where the block's
    // straight line puts it and the seconds where its other line does, and
    // across a step those are different moments.
    CHECK(map.timeToBeat(origin.seconds) == Approx(origin.beat));

    // Which is what the material needs: second zero of the run is beat zero of
    // it, so an auto-tempo reader starts at the head of its file.
    const auto material = magda::engine::materialBlock(block, status.afterEvent->range, origin);
    CHECK(material.beatAtTime(0.0) == Approx(0.0));
}

TEST_CASE("A slot launched where a block steps tempo starts at its own first sample",
          "[engine][clip][session]") {
    // The launch and the tempo step want the same instant inside one callback,
    // and the block is not cut at the step (#2340). The launcher used to name
    // the split in monotonic beats and project it onto the timeline and the
    // seconds axes separately, each a straight line across the block, and those
    // two lines disagree across a step: beat 4 projects to 2.020833 s where the
    // map puts it at 2, so the run's origin was not one instant and material
    // second zero sat a fiftieth of a beat past material beat zero.
    //
    // What that cost was the head of the clip. The split is worked out once, in
    // samples, and every face taken from that sample (#2336), so an uncut block
    // hands the run its own first sample either way.
    const auto map = steppedTempo();  // 120 to beat 4, then 60

    // Half a block before the step, so both the step and the launch fall inside
    // the callback rather than on its edge.
    auto snapshot = rollingFrom(map, 4.0 - (kBeatsPerBlock / 2.0));

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<CountingReader>());
    rig.publish(&map);

    magda::engine::TransportClock clock;

    // Monotonic beat 0.125 is timeline beat 4: the step itself, and half a
    // block into the callback.
    rig.handle.play(kBeatsPerBlock / 2.0);

    callback(rig, clock, snapshot);

    // Nothing before the launch sample, and the material's own samples from it.
    // CountingReader reads sample n back as n, so a clip that skipped its head
    // would say so here.
    constexpr auto kLaunchSample = kBlockSize / 2;

    CHECK(rig.at(kLaunchSample - 1) == Approx(0.0f));
    CHECK(rig.at(kLaunchSample) == Approx(0.0f));
    CHECK(rig.at(kLaunchSample + 1000) == Approx(1000.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(kLaunchSample - 1.0f));
}

TEST_CASE("A launched slot plays straight through a loop wrap", "[engine][clip][session]") {
    // #2324's own example. The loop is beats 4 to 8 at 60 bpm, everything
    // before beat 4 is at 120, and a slot launched at beat 7 has one second of
    // material behind it at the wrap. Through the map that reads as half a
    // second: the virtual origin projects to beat 3, in the 120 bpm half.
    const auto map = steppedTempo();

    auto snapshot = rollingFrom(map, 4.0);
    snapshot.loop = {true, 4.0, 8.0};

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<CountingReader>());
    rig.publish(&map);

    magda::engine::TransportClock clock;

    // Beat 4 to beat 7 at 60 bpm: three seconds, and a block is an eighth of
    // one. Nothing is launched yet.
    constexpr auto kBlocksPerSecond = static_cast<int>(kSampleRate) / kBlockSize;
    for (auto i = 0; i < 3 * kBlocksPerSecond; ++i)
        callback(rig, clock, snapshot);

    REQUIRE(rig.peak() == 0.0f);

    // Launched on the next block, which is beat 7, and played to the loop end.
    rig.handle.play(std::nullopt);
    for (auto i = 0; i < kBlocksPerSecond; ++i)
        callback(rig, clock, snapshot);

    CHECK(rig.at(0) == Approx(static_cast<float>(kSampleRate) - kBlockSize));

    // Over the wrap: the timeline went back four beats and the run did not.
    callback(rig, clock, snapshot);
    CHECK(rig.at(0) == Approx(static_cast<float>(kSampleRate)));

    callback(rig, clock, snapshot);
    CHECK(rig.at(0) == Approx(static_cast<float>(kSampleRate) + kBlockSize));
}

TEST_CASE("A launched slot keeps its place across a locate", "[engine][clip][session]") {
    // A slot is not on the timeline, so moving the cursor is not moving it.
    const auto map = steppedTempo();
    auto snapshot = rollingFrom(map, 4.0);

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<CountingReader>());
    rig.publish(&map);

    magda::engine::TransportClock clock;

    rig.handle.play(std::nullopt);

    constexpr auto kRunBlocks = 4;
    for (auto i = 0; i < kRunBlocks; ++i)
        callback(rig, clock, snapshot);

    CHECK(rig.at(0) == Approx((kRunBlocks - 1) * kBlockSize));

    // A locate to the top of the project, across the tempo step.
    callback(rig, clock, rollingFrom(map, 0.0, 2));

    CHECK(rig.at(0) == Approx(kRunBlocks * kBlockSize));
}

TEST_CASE("Synced handles agree in beats and in samples", "[engine][clip][session]") {
    // What makes a scene one event rather than N. Beats alone would put a
    // scene's MIDI in the right bar and its audio somewhere else.
    LaunchHandle leader;
    LaunchHandle follower;

    leader.play(std::nullopt);
    for (auto i = 0; i < 4; ++i)
        leader.advance(magda::engine::syncRangeFor(blockAt(i)));

    // Three blocks in, and joining rather than starting.
    follower.playSynced(leader, std::nullopt);
    follower.advance(magda::engine::syncRangeFor(blockAt(4)));
    leader.advance(magda::engine::syncRangeFor(blockAt(4)));

    REQUIRE(leader.playedMonotonicRange().has_value());
    REQUIRE(follower.playedMonotonicRange().has_value());
    REQUIRE(leader.playedSampleRange().has_value());
    REQUIRE(follower.playedSampleRange().has_value());

    CHECK(follower.playedMonotonicRange()->start == Approx(leader.playedMonotonicRange()->start));
    CHECK(follower.playedMonotonicRange()->end == Approx(leader.playedMonotonicRange()->end));

    // The same sample, not the same second: two runs that began on one sample
    // began together whatever the tempo does afterwards (#2336).
    CHECK(follower.playedSampleRange()->start == leader.playedSampleRange()->start);
    CHECK(follower.playedSampleRange()->end == leader.playedSampleRange()->end);

    // And the same origin to whatever renders them, on both axes.
    REQUIRE(follower.blockStatus().beforeEvent.origin.has_value());
    CHECK(*follower.blockStatus().beforeEvent.origin == *leader.blockStatus().beforeEvent.origin);
}
