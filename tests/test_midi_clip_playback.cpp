#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/GrooveTemplate.hpp"
#include "clip/MidiClipCompiler.hpp"

/**
 * MIDI clip playback (#2039), on the audio thread.
 *
 * The spine of this file is `Recorder::hanging`. The slice is judged on one
 * property rather than on a message list: a note-off is never emitted for a note
 * the source did not start, and never withheld from one it did. So every test
 * that moves the transport asserts that property, and the ones that also care
 * about a particular message check it on top rather than instead.
 *
 * The rig rolls. A block that skipped to a distant one would be testing a
 * locate, which is a different thing and has its own tests below.
 */

using Catch::Approx;
using magda::ClipInfo;
using magda::MidiCCData;
using magda::MidiCurveType;
using magda::MidiNote;
using magda::MidiPitchBendData;
using magda::MidiPitchExpressionPoint;
using magda::engine::BlockInfo;
using magda::engine::ClipLane;
using magda::engine::ClipMidiSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::compileClipSnapshot;
using magda::engine::compileMidiEvents;
using magda::engine::GrooveTemplateSet;
using magda::engine::RenderContext;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 6000;  ///< a quarter of a beat at 120 bpm
constexpr double kBeatsPerBlock = 0.25;
constexpr magda::TrackId kTrack = 3;

TempoMap makeTempoMap(double bpm = 120.0) {
    return TempoMap({{0.0, bpm, 0.0f}}, {{0.0, 4, 4}});
}

ClipInfo makeMidiClip(magda::ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.view = magda::ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

MidiNote note(int number, double startBeat, double lengthBeats, int velocity = 100) {
    return MidiNote{number, velocity, startBeat, lengthBeats, 0, {}};
}

/// One message, and where it landed.
struct Captured {
    int block = 0;
    int sample = 0;
    juce::MidiMessage message;

    /// Where this landed on the timeline, in beats.
    double beat() const {
        return (block + static_cast<double>(sample) / kBlockSize) * kBeatsPerBlock;
    }
};

class Recorder {
  public:
    void add(int blockIndex, const juce::MidiBuffer& buffer) {
        for (const auto metadata : buffer)
            captured.push_back(
                Captured{blockIndex, metadata.samplePosition, metadata.getMessage()});
    }

    /**
     * @brief Notes still sounding after everything recorded, as "channel:note".
     *
     * The whole file's assertion. An unmatched note-off is reported too, with a
     * leading '-', because emitting one for a note nobody started is the other
     * half of the same invariant.
     */
    std::vector<std::string> hanging() const {
        // What a receiver actually holds, which is one bit per (channel, note)
        // rather than a count. A pitch struck twice is one sounding note and one
        // note-off silences it, which is exactly why the compile drops the first
        // of two overlapping note-offs.
        std::vector<std::string> sounding;
        std::vector<std::string> spurious;

        for (const auto& entry : captured) {
            const auto& message = entry.message;
            const auto key = std::to_string(message.getChannel()) + ":" +
                             std::to_string(message.getNoteNumber());
            const auto found = std::find(sounding.begin(), sounding.end(), key);

            if (message.isNoteOn()) {
                if (found == sounding.end())
                    sounding.push_back(key);
                continue;
            }

            if (!message.isNoteOff())
                continue;

            if (found == sounding.end())
                spurious.push_back("-" + key);
            else
                sounding.erase(found);
        }

        sounding.insert(sounding.end(), spurious.begin(), spurious.end());
        return sounding;
    }

    template <typename Predicate> std::vector<Captured> ofKind(Predicate&& predicate) const {
        std::vector<Captured> out;
        for (const auto& entry : captured)
            if (predicate(entry.message))
                out.push_back(entry);
        return out;
    }

    std::vector<Captured> noteOns() const {
        return ofKind([](const juce::MidiMessage& m) { return m.isNoteOn(); });
    }
    std::vector<Captured> noteOffs() const {
        return ofKind([](const juce::MidiMessage& m) { return m.isNoteOff(); });
    }
    std::vector<Captured> controllers() const {
        return ofKind([](const juce::MidiMessage& m) { return m.isController(); });
    }
    std::vector<Captured> pitchBends() const {
        return ofKind([](const juce::MidiMessage& m) { return m.isPitchWheel(); });
    }

    std::vector<Captured> captured;
};

/// The transport, the snapshot and the source, wired together.
class Rig {
  public:
    Rig() : source_(kTrack, feed_) {
        source_.prepare(RenderContext{kSampleRate, kBlockSize, 2});
    }

    void publish(std::vector<ClipInfo> clips, const GrooveTemplateSet& grooves = {},
                 double bpm = 120.0) {
        snapshot_ = std::make_shared<const ClipSnapshot>(compileClipSnapshot(
            {ClipLane{kTrack, std::move(clips)}}, {}, makeTempoMap(bpm), grooves));
        feed_.publish(snapshot_);
    }

    /// Roll from block @p first to block @p last inclusive. The first block of a
    /// run is discontinuous, which is what starting the transport is.
    void roll(int first, int last, Recorder& into) {
        for (auto index = first; index <= last; ++index) {
            juce::MidiBuffer buffer;
            source_.render(blockAt(index, index != first || rolling_), buffer);
            into.add(index, buffer);
            rolling_ = true;
        }
    }

    /// A jump: the next block is discontinuous wherever it lands.
    void locate(int blockIndex, Recorder& into) {
        juce::MidiBuffer buffer;
        source_.render(blockAt(blockIndex, false), buffer);
        into.add(blockIndex, buffer);
        rolling_ = true;
    }

    void stop(int blockIndex, Recorder& into) {
        auto block = blockAt(blockIndex, true);
        block.playing = false;
        block.endBeat = block.startBeat;
        block.endSeconds = block.startSeconds;

        juce::MidiBuffer buffer;
        source_.render(block, buffer);
        into.add(blockIndex, buffer);
        rolling_ = false;
    }

    static BlockInfo blockAt(int index, bool continuous) {
        BlockInfo block;
        block.numSamples = kBlockSize;
        block.playing = true;
        block.continuous = continuous;
        block.startBeat = index * kBeatsPerBlock;
        block.endBeat = (index + 1) * kBeatsPerBlock;
        block.startSeconds = block.startBeat * 0.5;
        block.endSeconds = block.endBeat * 0.5;
        return block;
    }

    ClipMidiSource& source() {
        return source_;
    }

  private:
    ClipSnapshotFeed feed_;
    std::shared_ptr<const ClipSnapshot> snapshot_;
    ClipMidiSource source_;
    bool rolling_ = false;
};

/// The fork's own "Basic 8th Swing", which displaces an off-beat by half of
/// 0.66 over a two-per-beat grid: 0.165 beats.
GrooveTemplateSet swingSet() {
    GrooveTemplateSet set;
    set.add(GrooveTemplateSet::Entry{"Swing", {0.0f, 0.66f}, 2, 2, true});
    return set;
}

/// The blocks one beat spans.
int blockOf(double beat) {
    return static_cast<int>(beat / kBeatsPerBlock);
}

}  // namespace

// =============================================================================
// The compile
// =============================================================================

TEST_CASE("Notes compile to paired edges", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.0, 1.0));
    clip.midiNotes.push_back(note(64, 1.0, 2.0));

    const auto list = compileMidiEvents(clip, 0.0);
    REQUIRE(list.events.size() == 4);

    CHECK(list.events[0].isNoteOn());
    CHECK(list.events[0].data1 == 60);
    CHECK(list.events[0].endsAt == 1);
    CHECK(list.events[1].isNoteOff());
    CHECK(list.events[1].beat == Approx(1.0));

    CHECK(list.longestNoteBeats == Approx(2.0));
    CHECK_FALSE(list.mpe);
}

TEST_CASE("A pitch struck again before it ends loses the first note-off", "[engine][clip][midi]") {
    // The fork's `useNoteUp = false`. Emitting the off would cut the second note
    // short; what ends such a note is the pass or the span it sits in.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.0, 2.0));
    clip.midiNotes.push_back(note(60, 1.0, 1.0));

    const auto list = compileMidiEvents(clip, 0.0);

    auto ons = 0;
    auto offs = 0;
    for (const auto& event : list.events) {
        ons += event.isNoteOn() ? 1 : 0;
        offs += event.isNoteOff() ? 1 : 0;
    }

    CHECK(ons == 2);
    CHECK(offs == 1);
    CHECK(list.events[0].endsAt == -1);
}

TEST_CASE("Controllers land before notes at the same instant", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 1.0, 1.0));
    clip.midiCCData.push_back(MidiCCData{74, 40, 1.0, MidiCurveType::Step, 0.0, {}, {}});

    const auto list = compileMidiEvents(clip, 0.0);
    REQUIRE(list.events.size() == 3);

    // A bank or program change has to reach the synth before the note it
    // configures, which is the fork's ordering too.
    CHECK(list.events[0].kind() == 0xb0u);
    CHECK(list.events[1].isNoteOn());
}

// =============================================================================
// Densification
// =============================================================================

TEST_CASE("Every densified message changes the value", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiCCData.push_back(MidiCCData{74, 0, 0.0, MidiCurveType::Linear, 0.0, {}, {}});
    clip.midiCCData.push_back(MidiCCData{74, 127, 4.0, MidiCurveType::Linear, 0.0, {}, {}});

    // A millisecond at 120 bpm.
    const auto floorBeats = 0.001 * 120.0 / 60.0;
    const auto list = compileMidiEvents(clip, floorBeats);

    auto previous = -1;
    auto previousBeat = -1.0;
    auto count = 0;

    for (const auto& event : list.events) {
        if (event.kind() != 0xb0u)
            continue;

        ++count;
        CHECK(static_cast<int>(event.data2) != previous);
        if (previous >= 0)
            CHECK(event.beat - previousBeat >= Approx(floorBeats).margin(1e-9));

        previous = event.data2;
        previousBeat = event.beat;
    }

    // 128 distinct values over two seconds, none of them closer than the floor,
    // which the floor never reaches here.
    CHECK(count == 128);
}

TEST_CASE("A curve that barely moves sends almost nothing", "[engine][clip][midi]") {
    // The case the 1/16-beat grid got wrong in one direction: eight bars of ramp
    // through a single unit was 512 near-identical messages.
    auto clip = makeMidiClip(1, 0.0, 32.0);
    clip.midiCCData.push_back(MidiCCData{1, 64, 0.0, MidiCurveType::Linear, 0.0, {}, {}});
    clip.midiCCData.push_back(MidiCCData{1, 65, 32.0, MidiCurveType::Linear, 0.0, {}, {}});

    const auto list = compileMidiEvents(clip, 0.001 * 120.0 / 60.0);
    CHECK(list.events.size() == 2);
}

TEST_CASE("A fast pitch bend is bounded by the floor, not by fourteen bits",
          "[engine][clip][midi]") {
    // The other direction: at 120 bpm a hundred milliseconds is a fifth of a
    // beat, which the grid resolved with three points. Emitting every distinct
    // value would be 16384 messages and 147 KB against a 4096-byte port.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiPitchBendData.push_back(MidiPitchBendData{0, 0.0, MidiCurveType::Linear, 0.0, {}, {}});
    clip.midiPitchBendData.push_back(
        MidiPitchBendData{16383, 0.2, MidiCurveType::Linear, 0.0, {}, {}});

    const auto floorBeats = 0.001 * 120.0 / 60.0;
    const auto list = compileMidiEvents(clip, floorBeats);

    CHECK(list.events.size() > 50);
    CHECK(list.events.size() <= 105);

    for (std::size_t i = 1; i < list.events.size(); ++i)
        CHECK(list.events[i].beat - list.events[i - 1].beat >= Approx(floorBeats).margin(1e-9));
}

TEST_CASE("A constant segment sends one message", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiCCData.push_back(MidiCCData{7, 100, 0.0, MidiCurveType::Linear, 0.0, {}, {}});
    clip.midiCCData.push_back(MidiCCData{7, 100, 4.0, MidiCurveType::Linear, 0.0, {}, {}});

    const auto list = compileMidiEvents(clip, 0.001 * 120.0 / 60.0);
    CHECK(list.events.size() == 1);
}

TEST_CASE("Tension bends the curve the way the editor draws it", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiCCData.push_back(MidiCCData{74, 0, 0.0, MidiCurveType::Linear, 1.0, {}, {}});
    clip.midiCCData.push_back(MidiCCData{74, 127, 4.0, MidiCurveType::Linear, 0.0, {}, {}});

    const auto list = compileMidiEvents(clip, 0.001 * 120.0 / 60.0);

    // Halfway through a tension of 1.0 is t^3, so an eighth of the range rather
    // than half of it: the same formula CurveSnapshot::evaluate uses.
    const auto halfway = std::find_if(list.events.begin(), list.events.end(),
                                      [](const auto& event) { return event.beat >= 2.0; });
    REQUIRE(halfway != list.events.end());
    CHECK(static_cast<int>(halfway->data2) == Approx(127.0 * 0.125).margin(2.0));
}

TEST_CASE("A pitch-bend list entirely at rest is skipped", "[engine][clip][midi]") {
    // A stream of no-op wheel messages is pointless and deadlocks fragile synths
    // (#1193).
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiPitchBendData.push_back(
        MidiPitchBendData{8192, 0.0, MidiCurveType::Step, 0.0, {}, {}});
    clip.midiPitchBendData.push_back(
        MidiPitchBendData{8192, 2.0, MidiCurveType::Step, 0.0, {}, {}});

    CHECK(compileMidiEvents(clip, 0.0).events.empty());
}

// =============================================================================
// MPE
// =============================================================================

TEST_CASE("Overlapping expressive notes get their own channels", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);

    auto first = note(60, 0.0, 2.0);
    first.pitchExpression = {MidiPitchExpressionPoint{0.0, 0.0},
                             MidiPitchExpressionPoint{2.0, 2.0}};
    auto second = note(64, 0.5, 2.0);

    clip.midiNotes.push_back(first);
    clip.midiNotes.push_back(second);

    const auto list = compileMidiEvents(clip, 0.001 * 120.0 / 60.0);
    REQUIRE(list.mpe);

    std::vector<int> channels;
    for (const auto& event : list.events)
        if (event.isNoteOn())
            channels.push_back(event.channel());

    REQUIRE(channels.size() == 2);
    CHECK(channels[0] != channels[1]);
    for (const auto channel : channels)
        CHECK((channel >= 2 && channel <= 16));

    // Expression rides the note's own channel, densified like any other curve
    // rather than on the sync layer's 1/16 grid.
    auto bends = 0;
    for (const auto& event : list.events)
        if (event.kind() == 0xe0u) {
            ++bends;
            CHECK(event.channel() == channels[0]);
        }

    CHECK(bends > 16);
}

// =============================================================================
// The fold
// =============================================================================

TEST_CASE("A looped clip repeats its notes and ends each pass", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiNotes.push_back(note(60, 0.0, 1.0));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 2.0;

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(8.0) - 1, recorder);

    // Four passes of a two-beat loop under an eight-beat clip.
    CHECK(recorder.noteOns().size() == 4);
    CHECK(recorder.hanging().empty());

    const auto ons = recorder.noteOns();
    for (std::size_t i = 0; i < ons.size(); ++i)
        CHECK(ons[i].beat() == Approx(static_cast<double>(i) * 2.0).margin(1e-9));
}

TEST_CASE("A note running past the loop end is cut there", "[engine][clip][midi]") {
    // The fork's unrolling rule, read the other way round: its copy of the
    // sequence clips the note to the pass and keeps what is left.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.0, 3.0));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 2.0;

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    const auto offs = recorder.noteOffs();
    REQUIRE(offs.size() == 2);

    // On the pass boundary, nudged back one sample so it is not lost to the
    // block that starts there. The fork does the same by hand; here the clamp in
    // BlockInfo::sampleForBeat does it without being asked.
    CHECK(offs[0].beat() == Approx(2.0).margin(kBeatsPerBlock / kBlockSize + 1e-9));
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A note hanging over the loop start is struck at it", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiNotes.push_back(note(60, 1.5, 1.0));  // straddles the loop start at 2
    clip.loopEnabled = true;
    clip.loopStartBeats = 2.0;
    clip.loopLengthBeats = 2.0;

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    // Struck at the pass start with what is left of it, on every pass.
    CHECK(recorder.noteOns().size() >= 2);
    CHECK(recorder.hanging().empty());
}

TEST_CASE("midiOffset phases the loop", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 1.0, 0.5));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 2.0;
    clip.midiOffset = 1.0;

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    // The note sits one beat into a two-beat loop, and the phase moves the read
    // one beat on, so it sounds at the clip's start.
    const auto ons = recorder.noteOns();
    REQUIRE(!ons.empty());
    CHECK(ons.front().beat() == Approx(0.0).margin(1e-9));
    CHECK(recorder.hanging().empty());
}

TEST_CASE("trimOffset moves the origin of a clip that does not loop", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 2.0, 1.0));
    clip.midiTrimOffset = 2.0;

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    const auto ons = recorder.noteOns();
    REQUIRE(ons.size() == 1);
    CHECK(ons.front().beat() == Approx(0.0).margin(1e-9));
    CHECK(recorder.hanging().empty());
}

TEST_CASE("Turning looping off does not shorten the clip", "[engine][clip][midi]") {
    // MidiClip::disableLooping truncates the clip to one loop length, which is
    // why flattening a looped MIDI clip today plays only its first cycle.
    // Nothing here reads a loop as a length: the span is the length.
    auto clip = makeMidiClip(1, 0.0, 8.0);
    for (auto beat = 0.0; beat < 8.0; beat += 2.0)
        clip.midiNotes.push_back(note(60, beat, 1.0));

    clip.loopEnabled = false;
    clip.loopLengthBeats = 2.0;  // a leftover from before it was flattened

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(8.0) - 1, recorder);

    CHECK(recorder.noteOns().size() == 4);
    CHECK(recorder.hanging().empty());
}

// =============================================================================
// Lifetime, which is the slice
// =============================================================================

TEST_CASE("Nothing is left hanging by anything the transport does", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 16.0);
    clip.midiNotes.push_back(note(60, 0.0, 8.0));  // long enough to straddle everything
    clip.midiNotes.push_back(note(67, 1.0, 6.0));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 4.0;

    // Every section ends by stopping, because that is what a transport does and
    // because a roll that simply stops being asked for blocks is not one: notes
    // sounding when the last block was rendered are still sounding, correctly.
    Rig rig;
    rig.publish({clip});
    Recorder recorder;

    SECTION("rolling through wraps") {
        rig.roll(0, blockOf(16.0) - 1, recorder);
    }

    SECTION("a locate forwards") {
        rig.roll(0, blockOf(2.0), recorder);
        rig.locate(blockOf(9.5), recorder);
        rig.roll(blockOf(9.5) + 1, blockOf(16.0) - 1, recorder);
    }

    SECTION("a locate backwards") {
        rig.roll(0, blockOf(9.5), recorder);
        rig.locate(blockOf(1.5), recorder);
        rig.roll(blockOf(1.5) + 1, blockOf(4.0), recorder);
    }

    SECTION("a stop mid-note") {
        rig.roll(0, blockOf(2.0), recorder);
    }

    SECTION("a swap that deletes the clip") {
        rig.roll(0, blockOf(2.0), recorder);
        rig.publish({});
        rig.roll(blockOf(2.0) + 1, blockOf(3.0), recorder);
    }

    SECTION("a swap that moves the note") {
        rig.roll(0, blockOf(2.0), recorder);

        auto moved = clip;
        moved.midiNotes.front() = note(72, 0.0, 8.0);
        rig.publish({moved});

        rig.roll(blockOf(2.0) + 1, blockOf(6.0), recorder);
    }

    SECTION("a span that ends mid-note") {
        auto shortened = clip;
        shortened.setPlacementBeats(0.0, 3.0);
        shortened.loopEnabled = false;
        rig.publish({shortened});
        rig.roll(0, blockOf(6.0), recorder);
    }

    rig.stop(blockOf(20.0), recorder);
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A note that begins and ends inside one block is ended by its own off",
          "[engine][clip][midi]") {
    // Walking note-offs as a separate phase from note-ons put a note's off
    // before its own on whenever both fell in one callback: the off found
    // nothing active, was skipped, and the note stayed sounding until a
    // boundary. MidiBuffer can sort the output; it cannot repair the active
    // list. So note edges are walked in list order.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.5, 0.05));  // a fifth of a block long

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    // Only as far as the note, so a span end cannot cover for a missing off.
    rig.roll(0, blockOf(1.0), recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    REQUIRE(recorder.noteOffs().size() == 1);
    CHECK(recorder.noteOffs().front().beat() > recorder.noteOns().front().beat());
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A note grooved past its loop pass does not outlive it", "[engine][clip][midi]") {
    // The walk's own filter is the block, not the pass, so a note near the
    // window end whose groove pushes it past the wrap would be emitted after the
    // pass-end note-offs had already run. In sample order the off precedes the
    // on and the receiver keeps a note the active list has forgotten.
    // The loop length is deliberately not a whole number of blocks, so the wrap
    // falls inside a block rather than on its edge: with the wrap on a boundary
    // the block's own range filter hides the escape, and the bug does not
    // reproduce. The note is then within the swing's reach of that wrap, and
    // 1.35 grooves to about 1.466, past the pass end at 1.4.
    auto clip = makeMidiClip(1, 0.0, 6.0);
    clip.midiNotes.push_back(note(60, 1.35, 0.02));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 1.4;
    clip.grooveTemplate = "Swing";
    clip.grooveStrength = 1.0f;

    Rig rig;
    rig.publish({clip}, swingSet());

    Recorder recorder;
    rig.roll(0, blockOf(6.0) - 1, recorder);

    // On message order rather than on the bitset, because a looped clip
    // re-strikes the same pitch next pass and the bitset would resolve.
    auto sounding = 0;
    for (const auto& entry : recorder.captured) {
        if (entry.message.isNoteOn())
            ++sounding;
        else if (entry.message.isNoteOff())
            --sounding;

        CHECK(sounding >= 0);
        CHECK(sounding <= 1);
    }

    CHECK(recorder.hanging().empty());
}

TEST_CASE("A clip covered in the middle ends its notes at the hole's edge",
          "[engine][clip][midi]") {
    auto under = makeMidiClip(1, 0.0, 16.0);
    under.midiNotes.push_back(note(60, 0.0, 1.0));
    under.midiNotes.push_back(note(62, 5.0, 1.0));   // inside the hole
    under.midiNotes.push_back(note(64, 12.0, 1.0));  // after it

    auto over = makeMidiClip(2, 4.0, 4.0);
    over.stackOrder = 1;

    Rig rig;
    rig.publish({under, over});

    Recorder recorder;
    rig.roll(0, blockOf(16.0) - 1, recorder);

    // A note starting inside a hole does not sound; MIDI plays around a hole
    // rather than being cut by it.
    std::vector<int> sounded;
    for (const auto& entry : recorder.noteOns())
        sounded.push_back(entry.message.getNoteNumber());

    CHECK(std::find(sounded.begin(), sounded.end(), 62) == sounded.end());
    CHECK(std::find(sounded.begin(), sounded.end(), 60) != sounded.end());
    CHECK(std::find(sounded.begin(), sounded.end(), 64) != sounded.end());
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A note ringing into a hole keeps ringing and still ends", "[engine][clip][midi]") {
    auto under = makeMidiClip(1, 0.0, 16.0);
    under.midiNotes.push_back(note(60, 3.0, 3.0));  // starts before the hole, ends inside it

    auto over = makeMidiClip(2, 4.0, 4.0);
    over.stackOrder = 1;

    Rig rig;
    rig.publish({under, over});

    Recorder recorder;
    rig.roll(0, blockOf(10.0), recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    REQUIRE(recorder.noteOffs().size() == 1);

    // The off lands where the note ends, inside the hole. A hole is a reason
    // for a note not to start and never a reason for one not to end.
    CHECK(recorder.noteOffs().front().beat() == Approx(6.0).margin(kBeatsPerBlock));
    CHECK(recorder.hanging().empty());
}

// =============================================================================
// Chase
// =============================================================================

TEST_CASE("Locating into a note strikes it", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiNotes.push_back(note(60, 0.0, 6.0));

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.locate(blockOf(3.0), recorder);

    // Seeking into a sustained pad has to sound, or it is silence until the
    // next note. The fork does the same (getNotesOnAtTime).
    REQUIRE(recorder.noteOns().size() == 1);
    CHECK(recorder.noteOns().front().message.getNoteNumber() == 60);

    rig.roll(blockOf(3.0) + 1, blockOf(8.0), recorder);
    CHECK(recorder.hanging().empty());
}

TEST_CASE("Locating past a controller sets it", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiCCData.push_back(MidiCCData{74, 0, 0.0, MidiCurveType::Linear, 0.0, {}, {}});
    clip.midiCCData.push_back(MidiCCData{74, 127, 4.0, MidiCurveType::Linear, 0.0, {}, {}});

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.locate(blockOf(2.0), recorder);

    const auto controllers = recorder.controllers();
    REQUIRE(!controllers.empty());

    // Halfway up a linear ramp. Because the compile emits on value change, the
    // last event before the instant IS the current value rather than a grid
    // point up to a sixteenth of a beat stale.
    CHECK(static_cast<int>(controllers.front().message.getControllerValue()) ==
          Approx(63.0).margin(2.0));
}

TEST_CASE("Locating into an expressive note reconstructs its bend on its own channel",
          "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 8.0);
    auto expressive = note(60, 0.0, 6.0);
    expressive.pitchExpression = {MidiPitchExpressionPoint{0.0, 0.0},
                                  MidiPitchExpressionPoint{6.0, 12.0}};
    clip.midiNotes.push_back(expressive);

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.locate(blockOf(3.0), recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    const auto channel = recorder.noteOns().front().message.getChannel();
    CHECK(channel >= 2);  // a member channel, not the master

    // The bend is a controller stream like any other, so the chase delivers it
    // structurally: halfway up a six-beat glide to twelve semitones is six, and
    // the wheel is centre plus half of TE's fixed 48-semitone range times that.
    const auto bends = recorder.pitchBends();
    REQUIRE(!bends.empty());
    CHECK(bends.front().message.getChannel() == channel);
    CHECK(bends.front().message.getPitchWheelValue() ==
          Approx(8192.0 + 8191.0 * 6.0 / 48.0).margin(64.0));

    rig.roll(blockOf(3.0) + 1, blockOf(8.0), recorder);
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A locate asks where a note sounds, not where it was written", "[engine][clip][midi]") {
    // notesSoundingAt reads raw beats while playback grooves both edges, so a
    // locate landing between a note's written onset and its swung one would
    // start it early.
    auto clip = makeMidiClip(1, 0.0, 8.0);
    clip.midiNotes.push_back(note(60, 0.5, 2.0));
    clip.grooveTemplate = "Swing";
    clip.grooveStrength = 1.0f;

    Rig rig;
    rig.publish({clip}, swingSet());

    Recorder recorder;
    // Onto the written onset, which the swing has already moved off. The chase
    // runs at the block's start and the walk covers the rest of it, so reading
    // raw beats would strike the note twice: once at 0.5 because the raw list
    // says it is sounding, and once at its swung 0.665.
    rig.locate(blockOf(0.5), recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    CHECK(recorder.noteOns().front().beat() ==
          Approx(0.5 + 0.5 * 0.66 / 2.0).margin(kBeatsPerBlock / kBlockSize + 1e-9));

    rig.roll(blockOf(0.5) + 1, blockOf(8.0), recorder);
    CHECK(recorder.hanging().empty());
}

TEST_CASE("A reused MPE channel does not inherit the last note's bend", "[engine][clip][midi]") {
    // Once any note has expression every note takes a member channel, so a plain
    // note can land on one an expressive note left bent. Fifteen expressive
    // notes first, because there are fifteen member channels and reuse only
    // begins once every one of them has been used.
    auto clip = makeMidiClip(1, 0.0, 64.0);

    for (auto i = 0; i < 15; ++i) {
        auto bent = note(40 + i, i * 1.0, 0.5);
        bent.pitchExpression = {MidiPitchExpressionPoint{0.0, 0.0},
                                MidiPitchExpressionPoint{0.5, 12.0}};
        clip.midiNotes.push_back(bent);
    }

    clip.midiNotes.push_back(note(64, 20.0, 1.0));  // plain, onto a channel left bent

    const auto list = compileMidiEvents(clip, 0.001 * 120.0 / 60.0);
    REQUIRE(list.mpe);

    // Find the plain note's on, and the last wheel written to its channel before
    // it. It has to be centre, or the note plays a whole tone sharp.
    std::size_t plainOn = 0;
    for (std::size_t i = 0; i < list.events.size(); ++i)
        if (list.events[i].isNoteOn() && list.events[i].data1 == 64)
            plainOn = i;

    REQUIRE(plainOn > 0);

    auto wheel = -1;
    for (std::size_t i = 0; i < plainOn; ++i) {
        const auto& event = list.events[i];
        if (event.kind() == 0xe0u && event.channel() == list.events[plainOn].channel())
            wheel = event.data1 | (event.data2 << 7);
    }

    CHECK(wheel == 8192);
}

TEST_CASE("Locating into a hole strikes nothing", "[engine][clip][midi]") {
    auto under = makeMidiClip(1, 0.0, 16.0);
    under.midiNotes.push_back(note(60, 0.0, 10.0));

    auto over = makeMidiClip(2, 4.0, 4.0);
    over.stackOrder = 1;

    Rig rig;
    rig.publish({under, over});

    Recorder recorder;
    rig.locate(blockOf(6.0), recorder);

    CHECK(recorder.noteOns().empty());
    CHECK(recorder.hanging().empty());
}

// =============================================================================
// Groove
// =============================================================================

TEST_CASE("Groove moves a note and leaves a controller alone", "[engine][clip][midi]") {
    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.5, 0.25));
    clip.midiCCData.push_back(MidiCCData{74, 40, 0.5, MidiCurveType::Step, 0.0, {}, {}});
    clip.grooveTemplate = "Swing";
    clip.grooveStrength = 1.0f;

    Rig rig;
    rig.publish({clip}, swingSet());

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    REQUIRE(recorder.controllers().size() == 1);

    // The off-beat is late by half of 0.66 over a two-per-beat grid; the
    // controller is where it was written.
    CHECK(recorder.noteOns().front().beat() ==
          Approx(0.5 + 0.5 * 0.66 / 2.0).margin(kBeatsPerBlock));
    CHECK(recorder.controllers().front().beat() == Approx(0.5).margin(1e-9));
    CHECK(recorder.hanging().empty());
}

TEST_CASE("Groove is anchored to the project grid, not to the clip", "[engine][clip][midi]") {
    // A clip dragged half a beat does not take its swing with it: the fork adds
    // the clip's content start before grooving and subtracts the clip's start
    // after (MidiNote::getPlaybackBeats).
    const auto displacementOf = [](double clipStart) {
        auto clip = makeMidiClip(1, clipStart, 4.0);
        clip.midiNotes.push_back(note(60, 0.0, 0.25));
        clip.grooveTemplate = "Swing";
        clip.grooveStrength = 1.0f;

        Rig rig;
        rig.publish({clip}, swingSet());
        Recorder recorder;
        rig.roll(blockOf(clipStart), blockOf(clipStart + 4.0) - 1, recorder);

        REQUIRE(recorder.noteOns().size() == 1);
        return recorder.noteOns().front().beat() - clipStart;
    };

    constexpr auto oneSample = kBeatsPerBlock / kBlockSize;

    // On the beat, the grid step's own lateness is zero, so nothing moves.
    CHECK(displacementOf(1.0) == Approx(0.0).margin(oneSample + 1e-9));

    // Half a beat later the note sits on the swung step instead: late by half of
    // 0.66 over a two-per-beat grid.
    CHECK(displacementOf(1.5) == Approx(0.5 * 0.66 / 2.0).margin(oneSample + 1e-9));
}

TEST_CASE("An odd-length loop grooves each pass differently", "[engine][clip][midi]") {
    // The case that decided groove runs at emit time rather than at compile
    // time. A 1.5-beat loop under a per-beat template starts its second pass on
    // the off-beat, so timeline anchoring and alike-passes cannot both hold.
    auto clip = makeMidiClip(1, 0.0, 6.0);
    clip.midiNotes.push_back(note(60, 0.0, 0.25));
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 1.5;
    clip.grooveTemplate = "Swing";
    clip.grooveStrength = 1.0f;

    Rig rig;
    rig.publish({clip}, swingSet());

    Recorder recorder;
    rig.roll(0, blockOf(6.0) - 1, recorder);

    const auto ons = recorder.noteOns();
    REQUIRE(ons.size() == 4);

    // Passes start at 0, 1.5, 3 and 4.5. The first and third sit on a beat and
    // do not move; the second and fourth sit on an off-beat and are swung.
    constexpr auto oneSample = kBeatsPerBlock / kBlockSize;
    constexpr auto swung = 0.5 * 0.66 / 2.0;

    CHECK(ons[0].beat() == Approx(0.0).margin(oneSample + 1e-9));
    CHECK(ons[2].beat() == Approx(3.0).margin(oneSample + 1e-9));
    CHECK(ons[1].beat() == Approx(1.5 + swung).margin(oneSample + 1e-9));
    CHECK(ons[3].beat() == Approx(4.5 + swung).margin(oneSample + 1e-9));

    CHECK(recorder.hanging().empty());
}

TEST_CASE("A grooved note-off never precedes its own note-on", "[engine][clip][midi]") {
    GrooveTemplateSet set;
    // Extreme enough to move the two edges of a short note past each other.
    set.add(GrooveTemplateSet::Entry{"Wild", {0.0f, 1.0f}, 2, 2, false});

    auto clip = makeMidiClip(1, 0.0, 4.0);
    clip.midiNotes.push_back(note(60, 0.45, 0.1));
    clip.grooveTemplate = "Wild";
    clip.grooveStrength = 1.0f;

    Rig rig;
    rig.publish({clip}, set);

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    REQUIRE(recorder.noteOns().size() == 1);
    REQUIRE(recorder.noteOffs().size() == 1);
    CHECK(recorder.noteOffs().front().beat() >= recorder.noteOns().front().beat());
    CHECK(recorder.hanging().empty());
}

// =============================================================================
// The budget
// =============================================================================

TEST_CASE("An overflowing block reports rather than growing the buffer", "[engine][clip][midi]") {
    // Far more than the port holds: about 450 short messages fit.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    for (auto i = 0; i < 900; ++i)
        clip.midiNotes.push_back(note(36 + (i % 60), 0.0 + i * 0.0001, 0.05));

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    // One block past the span's end, because an off the full block could not fit
    // is owed to the next one rather than dropped.
    rig.roll(0, blockOf(4.0), recorder);

    CHECK(rig.source().droppedEvents() > 0);
    // What did go out is still paired, which is the point: drops fall on
    // note-ons, and a note-on that is dropped never comes to owe an off.
    CHECK(recorder.hanging().empty());
}

TEST_CASE("Offs owed by a full block arrive in the next one", "[engine][clip][midi]") {
    // Every note ends at the same instant, so one block owes hundreds of offs.
    auto clip = makeMidiClip(1, 0.0, 4.0);
    for (auto i = 0; i < 120; ++i)
        clip.midiNotes.push_back(note(20 + i, 0.0, 1.0));

    Rig rig;
    rig.publish({clip});

    Recorder recorder;
    rig.roll(0, blockOf(4.0) - 1, recorder);

    CHECK(recorder.hanging().empty());
}
