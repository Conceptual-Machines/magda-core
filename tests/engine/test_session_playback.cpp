#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
 * The claim under test is that a slot is the arrangement's own playback over a
 * block whose beat axis has moved onto the run's origin: the same material
 * plays, at the sample the launch landed on rather than at the callback
 * boundary, and it stops at the sample the stop landed on.
 *
 * Everything rolls, and nothing here advances a handle by hand: the launcher
 * does it once per block, before anything renders, which is the property the
 * two sources depend on and one of the cases below.
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
using magda::engine::LaunchHandle;
using magda::engine::LaunchHandleFeed;
using magda::engine::LaunchHandleTable;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
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

/// The block covering @p startSeconds, on @p map rather than at one tempo. The
/// two faces are derived through the one map, the way the transport derives
/// them (RenderContext.hpp).
BlockInfo blockOnMap(const magda::engine::TempoMap& map, double startSeconds,
                     bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
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
    span.startBeat = start;
    span.endBeat = end;
    span.startSeconds = start * kSecondsPerBeat;
    span.endSeconds = end * kSecondsPerBeat;
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

/**
 * @brief One track's session, its handle and its readers, wired up.
 *
 * The handle is owned here rather than by a store, because what a store does
 * with handles is a separate question with its own cases below. What this rig
 * insists on is the ordering: the launcher advances, then the sources render.
 */
struct AudioRig {
    AudioRig() {
        table.entries.push_back(LaunchHandleTable::Entry{SlotKey{kTrack, kScene}, &handle});
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

    void publish() {
        auto compiled = std::make_shared<ClipSnapshot>();
        compiled->tracks.push_back(lane);
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
        magda::engine::advanceLaunchHandles(handles, block);
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
    ClipAudioSource source{kTrack, clips, streams, handles};
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
        table.entries.push_back(LaunchHandleTable::Entry{SlotKey{kTrack, kScene}, &handle});
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
            magda::engine::advanceLaunchHandles(handles, block);

            juce::MidiBuffer buffer;
            source.render(block, buffer);

            for (const auto metadata : buffer)
                captured.push_back(Captured{index, metadata.samplePosition, metadata.getMessage()});
        }
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
    ClipMidiSource source{kTrack, clips, handles};
    std::vector<Captured> captured;
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
        block, whole, magda::engine::RunOrigin{block.beats.start, block.seconds.start});

    // Beat zero of the material, at the seconds beat zero has.
    CHECK(material.beats.start == Approx(0.0));
    CHECK(material.beats.end == Approx(kBeatsPerBlock));
    CHECK(material.seconds.start == Approx(0.0));
    CHECK(material.seconds.end == Approx(kBeatsPerBlock * kSecondsPerBeat));

    // The samples did not move, which is what lets a MIDI event keep writing
    // into the callback's own buffer: a material beat resolves to the sample
    // the timeline beat it came from would have resolved to.
    CHECK(material.numSamples == block.numSamples);
    for (const auto beat : {0.0, 0.1, 0.2})
        CHECK(material.sampleForBeat(beat) == block.sampleForBeat(block.beats.start + beat));

    // A run that began here is not continuous with the last block, whatever the
    // transport was doing.
    CHECK_FALSE(material.continuous);

    // And the project's map does not come with it: both axes moved, by
    // different amounts, so the map would answer about the timeline. What is
    // left is the block's own line, which is the material's own line.
    CHECK(material.tempo == nullptr);
    CHECK(material.beatAtTime(material.seconds.start) == Approx(material.beats.start));
    CHECK(material.beatAtTime(material.seconds.end) == Approx(material.beats.end));
}

TEST_CASE("A run already under way is continuous with the block before it",
          "[engine][clip][session]") {
    const auto block = blockAt(40);

    // The run began four blocks ago, so this block is one beat into it.
    const magda::engine::RunOrigin origin{block.beats.start - 1.0,
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

    // Launched well down the timeline. A slot has no position, so where the
    // transport happens to be must not reach the material: sample n of the
    // file is what block one of the run plays.
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
    CHECK(rig.at(3000) == Approx(0.0f));
    CHECK(rig.at(kBlockSize - 1) == Approx(0.0f));

    rig.render(42);
    CHECK(rig.peak() == 0.0f);
}

TEST_CASE("The arrangement and the session are different sections of one track",
          "[engine][clip][session]") {
    // A source with no handles plays the arrangement and nothing else, which is
    // what makes the two ops on a track independent rather than alternatives.
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    ClipAudioSource arrangement{kTrack, rig.clips, rig.streams};
    arrangement.prepare(context());

    rig.handle.play(std::nullopt);

    const auto block = blockAt(0);
    magda::engine::advanceLaunchHandles(rig.handles, block);

    juce::AudioBuffer<float> out(2, kBlockSize);
    rig.fill();
    arrangement.render(block, juce::dsp::AudioBlock<float>(out));

    CHECK(out.getMagnitude(0, kBlockSize) == 0.0f);  // nothing is in the arrangement
}

// =============================================================================
// One advance per block
// =============================================================================

TEST_CASE("A handle is advanced once per block however many sources read it",
          "[engine][clip][session]") {
    LaunchHandle handle;
    LaunchHandleTable table;
    table.entries.push_back(LaunchHandleTable::Entry{SlotKey{kTrack, kScene}, &handle});

    LaunchHandleFeed feed;
    feed.publish(std::make_shared<const LaunchHandleTable>(table));

    handle.play(std::nullopt);
    magda::engine::advanceLaunchHandles(feed, blockAt(0));
    magda::engine::advanceLaunchHandles(feed, blockAt(1));

    // Two blocks of a quarter beat each. A handle a source advanced for itself
    // would have been moved twice as far, and a slot's audio and MIDI would
    // then be reading different parts of the material.
    const auto played = handle.playedRange();
    REQUIRE(played.has_value());
    CHECK(played->length() == Approx(2 * kBeatsPerBlock));

    // And what the sources read is what that last advance said.
    CHECK(handle.blockStatus().playing1);
    CHECK(handle.blockStatus().range1.start == Approx(kBeatsPerBlock));
}

TEST_CASE("A slot with no handle in the table is silent rather than wrong",
          "[engine][clip][session]") {
    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<ConstantReader>());
    rig.publish();

    // The table catches up with the snapshot on its own schedule, so a slot can
    // exist with nothing to launch it.
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

    // The note is at beat zero of the material, so it lands on the first sample
    // of the block the launch fired in and nowhere near beat zero of the
    // timeline, which is nine beats behind.
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

    // Half a beat in, and the note has three and a half to run. Nothing in the
    // material ends it: the stop is what owes the note-off.
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

/// A factory that builds nothing. Handles are the engine's own state, so a
/// store makes them whatever the host declines.
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

    const auto first = store.publishHandles(snapshotWithSlots({2, 0, 1}), feed);
    REQUIRE(first->entries.size() == 3);
    CHECK(first->entries[0].key.sceneIndex == 0);
    CHECK(first->entries[1].key.sceneIndex == 1);
    CHECK(first->entries[2].key.sceneIndex == 2);
    CHECK(first->find(SlotKey{kTrack, 1}) != nullptr);
    CHECK(first->find(SlotKey{kTrack, 9}) == nullptr);

    // Playing, and then an unrelated clip edit republishes the snapshot. The
    // handle has to be the same object: a new one would come up stopped, at the
    // beginning, and the clip would restart under a gesture nobody made.
    auto* playing = first->find(SlotKey{kTrack, 1});
    playing->play(std::nullopt);
    playing->advance(magda::engine::syncRangeFor(blockAt(0)));
    REQUIRE(playing->playState() == LaunchHandle::PlayState::playing);

    const auto second = store.publishHandles(snapshotWithSlots({0, 1, 2}), feed);

    // The same slots and the same handles, so nothing was swapped: a clip edit
    // that did not touch the session must not make the callback wait.
    CHECK(second == first);
    CHECK(second->find(SlotKey{kTrack, 1}) == playing);
    CHECK(playing->playState() == LaunchHandle::PlayState::playing);
}

TEST_CASE("A slot emptied and refilled does not come back playing", "[engine][clip][session]") {
    // Emptying a slot and filling it again is two clip edits and no structural
    // one, so nothing but the publish itself is in a position to retire the
    // handle. Left to a plan publish, the new clip would come up already
    // playing, at the old one's loop phase and played range.
    NoFactory factory;
    magda::engine::RuntimeStateStore store{factory};
    LaunchHandleFeed feed;

    store.publishHandles(snapshotWithSlots({0, 1}), feed);

    auto* playing = store.findHandle(SlotKey{kTrack, 0});
    REQUIRE(playing != nullptr);
    playing->play(std::nullopt);
    playing->advance(magda::engine::syncRangeFor(blockAt(0)));
    REQUIRE(playing->playState() == LaunchHandle::PlayState::playing);

    store.publishHandles(snapshotWithSlots({1}), feed);
    CHECK(store.findHandle(SlotKey{kTrack, 0}) == nullptr);

    store.publishHandles(snapshotWithSlots({0, 1}), feed);

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
    const magda::engine::TempoMap map({{0.0, 120.0, 0.0f}, {8.0, 60.0, 0.0f}}, {{0.0, 4, 4}});

    AudioRig rig;
    rig.give(1, 4.0, std::make_unique<CountingReader>());
    rig.publish();

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

/// 120 bpm to beat 4 and 60 bpm from there, which is the shape #2324 is about:
/// a run launched in the second half spans beats whose seconds, read off the
/// map, are worth half what they were.
magda::engine::TempoMap steppedTempo() {
    return magda::engine::TempoMap({{0.0, 120.0, 0.0f}, {4.0, 60.0, 0.0f}}, {{0.0, 4, 4}});
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

/// One callback of @p rig, cut into blocks by the clock the way the session
/// does it. The rig's buffer holds whatever the last segment rendered.
void callback(AudioRig& rig, magda::engine::TransportClock& clock,
              const magda::engine::TransportSnapshot& snapshot) {
    for (const auto& segment : clock.advance(snapshot, kSampleRate, kBlockSize))
        rig.renderBlock(segment.block);
}

}  // namespace

TEST_CASE("A launched slot plays straight through a loop wrap", "[engine][clip][session]") {
    // #2324's own example. The loop runs beats 4 to 8 at 60 bpm and everything
    // before beat 4 is at 120. A slot launched at beat 7 has one beat, which is
    // one second, of material behind it when the timeline goes back to 4.
    //
    // Reconstructing that through the map gives half a second: the virtual
    // origin projects to beat 3, which is in the 120 bpm half, and beat 3 to
    // beat 4 is worth half of what beat 7 to beat 8 was. The reader would then
    // be asked for a position it had already passed, which is a seek, which is
    // silence.
    const auto map = steppedTempo();

    auto snapshot = rollingFrom(map, 4.0);
    snapshot.loop = {true, 4.0, 8.0};

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<CountingReader>());
    rig.publish();

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

    // And over the wrap. One second of material has gone by, so this is where
    // the file is: the timeline went back four beats and the run did not.
    callback(rig, clock, snapshot);
    CHECK(rig.at(0) == Approx(static_cast<float>(kSampleRate)));

    callback(rig, clock, snapshot);
    CHECK(rig.at(0) == Approx(static_cast<float>(kSampleRate) + kBlockSize));
}

TEST_CASE("A launched slot keeps its place across a locate", "[engine][clip][session]") {
    // A slot is not on the timeline, so moving the cursor is not moving it. The
    // run measures itself in a domain the locate cannot touch.
    const auto map = steppedTempo();
    auto snapshot = rollingFrom(map, 4.0);

    AudioRig rig;
    rig.give(1, 8.0, std::make_unique<CountingReader>());
    rig.publish();

    magda::engine::TransportClock clock;

    rig.handle.play(std::nullopt);

    constexpr auto kRunBlocks = 4;
    for (auto i = 0; i < kRunBlocks; ++i)
        callback(rig, clock, snapshot);

    CHECK(rig.at(0) == Approx((kRunBlocks - 1) * kBlockSize));

    // Somebody drags the playhead to the top of the project, which is a jump
    // across the tempo step as well as a jump backwards.
    callback(rig, clock, rollingFrom(map, 0.0, 2));

    CHECK(rig.at(0) == Approx(kRunBlocks * kBlockSize));
}

TEST_CASE("Synced handles agree in beats and in seconds", "[engine][clip][session]") {
    // What makes a scene launch one event rather than N. Agreeing in beats
    // alone would put a scene's MIDI in the right bar and its audio somewhere
    // else, because the two are read against different faces of the run.
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
    REQUIRE(leader.playedMonotonicSecondsRange().has_value());
    REQUIRE(follower.playedMonotonicSecondsRange().has_value());

    CHECK(follower.playedMonotonicRange()->start == Approx(leader.playedMonotonicRange()->start));
    CHECK(follower.playedMonotonicRange()->end == Approx(leader.playedMonotonicRange()->end));
    CHECK(follower.playedMonotonicSecondsRange()->start ==
          Approx(leader.playedMonotonicSecondsRange()->start));
    CHECK(follower.playedMonotonicSecondsRange()->end ==
          Approx(leader.playedMonotonicSecondsRange()->end));

    // And they report the same origin to whatever renders them, on both axes.
    // One value rather than two, so agreeing in beats and not in seconds is
    // not a state this can be in.
    REQUIRE(follower.blockStatus().origin1.has_value());
    CHECK(*follower.blockStatus().origin1 == *leader.blockStatus().origin1);
}
