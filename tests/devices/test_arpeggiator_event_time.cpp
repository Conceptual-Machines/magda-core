#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <vector>

#include "devices/ArpeggiatorTestRig.hpp"

// The arp reads its input where the host put it rather than all at the top of
// the block (#2415), so a note is closed by the event that closed it and the
// notes a step plays are the ones held when that step sounds.

namespace {
using magda::test::at;
using magda::test::DeviceMidiBuffer;
using Arp = magda::test::Arp;
using Rig = magda::test::ArpRig;

constexpr double kBlockSeconds = 0.05;
/// Nine tenths of the way in: far enough that a block-start approximation
/// cuts the note by most of a block.
constexpr double kLateInBlock = 0.045;

juce::MidiMessage on(int note, int velocity = 91) {
    return juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity));
}

juce::MidiMessage off(int note) {
    return juce::MidiMessage::noteOff(1, note);
}

/// One output event on the timeline the phrase was written on, so partitions
/// that cut it into different blocks are comparable.
struct TimedEvent {
    double seconds = 0.0;
    juce::MidiMessage message;
};

struct PhraseEvent {
    double seconds = 0.0;
    juce::MidiMessage message;
};

/// Runs @p phrase over [0, totalSeconds) with the block lengths cycled from
/// @p blockLengths, and returns what the arp played on the phrase's own
/// timeline.
std::vector<TimedEvent> render(const std::vector<PhraseEvent>& phrase, double totalSeconds,
                               const std::vector<double>& blockLengths) {
    Rig rig;
    std::vector<TimedEvent> played;
    double blockStart = 0.0;
    size_t nextEvent = 0;
    size_t nextLength = 0;

    while (blockStart < totalSeconds - 1.0e-12) {
        const double length =
            std::min(blockLengths[nextLength++ % blockLengths.size()], totalSeconds - blockStart);

        std::vector<juce::MidiMessage> input;
        while (nextEvent < phrase.size() && phrase[nextEvent].seconds < blockStart + length) {
            input.push_back(at(phrase[nextEvent].seconds - blockStart, phrase[nextEvent].message));
            ++nextEvent;
        }

        const auto midi = rig.run(blockStart, input, true, false, length);
        for (int index = 0; index < midi.size(); ++index)
            played.push_back(
                {blockStart + midi.message(index).getTimeStamp(), midi.message(index)});

        blockStart += length;
    }

    return played;
}

/// Every pitch alternates on, off, on: no voice left sounding, none started
/// twice, and none cut by a note-off that arrives after its note-on.
void checkNotesBalance(const std::vector<TimedEvent>& played) {
    std::map<int, int> sounding;
    for (const auto& event : played) {
        const int note = event.message.getNoteNumber();
        if (event.message.isNoteOn()) {
            INFO("note " << note << " at " << event.seconds);
            CHECK(sounding[note] == 0);
            ++sounding[note];
        } else if (event.message.isNoteOff()) {
            INFO("note " << note << " at " << event.seconds);
            CHECK(sounding[note] == 1);
            --sounding[note];
        }
    }
    for (const auto& [note, count] : sounding) {
        INFO("note " << note);
        CHECK(count == 0);
    }
}

/// A rig whose output is kept on the timeline the blocks were run on.
struct Recorder {
    Rig rig;
    std::vector<TimedEvent> played;

    explicit Recorder(bool latch = false) : rig(latch) {}

    void run(double start, const std::vector<juce::MidiMessage>& input = {},
             double seconds = kBlockSeconds) {
        const auto midi = rig.run(start, input, true, false, seconds);
        for (int index = 0; index < midi.size(); ++index)
            played.push_back({start + midi.message(index).getTimeStamp(), midi.message(index)});
    }
};

std::string describe(const std::vector<TimedEvent>& played) {
    juce::String text;
    for (const auto& event : played)
        text << juce::String(event.seconds, 6) << " " << event.message.getDescription() << "\n";
    return text.toStdString();
}

}  // namespace

TEST_CASE("Arpeggiator holds a latched note until the input that replaces it",
          "[arpeggiator][midi][event-time]") {
    Rig rig(true);
    rig.startNote();

    // The key comes up at the top of the block and the replacement lands nine
    // tenths of the way through it. Both are the same buffer, and the note the
    // replacement ends must last until the replacement.
    auto midi = rig.run(kBlockSeconds, {off(60), at(kLateInBlock, on(67))});

    REQUIRE(midi.size() == 1);
    CHECK(midi.message(0).isNoteOff());
    CHECK(midi.message(0).getNoteNumber() == 60);
    CHECK(midi.message(0).getTimeStamp() == Catch::Approx(kLateInBlock));
}

TEST_CASE("Arpeggiator holds its note until the panic that stops it",
          "[arpeggiator][midi][event-time]") {
    for (int controller : {120, 123}) {
        INFO("controller " << controller);
        Rig rig;
        rig.startNote();

        auto midi =
            rig.run(kBlockSeconds,
                    {at(kLateInBlock, juce::MidiMessage::controllerEvent(1, controller, 0))});

        // The reset travels on to whatever plays the notes (#2417), and the
        // arp closes what it was sounding behind it, both where it happened.
        REQUIRE(midi.size() == 2);
        CHECK(midi.message(0).isController());
        CHECK(midi.message(0).getTimeStamp() == Catch::Approx(kLateInBlock));
        CHECK(midi.message(1).isNoteOff());
        CHECK(midi.message(1).getNoteNumber() == 60);
        CHECK(midi.message(1).getTimeStamp() == Catch::Approx(kLateInBlock));
    }
}

TEST_CASE("Arpeggiator closes a replaced pitch before it plays it again",
          "[arpeggiator][midi][event-time]") {
    Recorder recorder(true);
    recorder.rig.setRate(Arp::Rate::ThirtySecond);
    recorder.run(0.0, {on(60)});

    // The same pitch, released and pressed again inside one block: the voice
    // it replaces has to be closed before the retrigger sounds, or the note-off
    // it was owed arrives on top of the new note.
    recorder.run(kBlockSeconds, {off(60), at(kLateInBlock, on(60))});
    for (int block = 2; block < 8; ++block)
        recorder.run(block * kBlockSeconds);
    // Stopped rather than left mid-phrase, so the timeline balances.
    recorder.run(8 * kBlockSeconds, {juce::MidiMessage::allNotesOff(1)});

    INFO(describe(recorder.played));
    checkNotesBalance(recorder.played);

    const double replacedAt = kBlockSeconds + kLateInBlock;
    const auto closed =
        std::find_if(recorder.played.begin(), recorder.played.end(), [&](const TimedEvent& event) {
            return event.message.isNoteOff() && event.seconds == Catch::Approx(replacedAt);
        });
    REQUIRE(closed != recorder.played.end());
    CHECK(closed->message.getNoteNumber() == 60);

    const auto retriggered =
        std::find_if(closed, recorder.played.end(),
                     [](const TimedEvent& event) { return event.message.isNoteOn(); });
    REQUIRE(retriggered != recorder.played.end());
    CHECK(retriggered->message.getNoteNumber() == 60);
    CHECK(retriggered->seconds > replacedAt);
}

TEST_CASE("Arpeggiator answers every replacement in one block where it happened",
          "[arpeggiator][midi][event-time]") {
    Rig rig;
    rig.startNote();

    // Three chords in one buffer, each one ending the last.
    auto midi = rig.run(kBlockSeconds,
                        {at(0.01, off(60)), at(0.02, on(64)), at(0.03, off(64)), at(0.04, on(67))});

    REQUIRE(midi.size() == 1);
    CHECK(midi.message(0).isNoteOff());
    CHECK(midi.message(0).getNoteNumber() == 60);
    CHECK(midi.message(0).getTimeStamp() == Catch::Approx(0.01));
}

TEST_CASE("Arpeggiator plays a step from the notes held when it sounds",
          "[arpeggiator][midi][event-time]") {
    Rig rig;
    rig.setRate(Arp::Rate::ThirtySecond);
    rig.startNote();

    // A second pitch arrives after this block's steps have already sounded, so
    // it belongs to the steps that follow it and to none of the ones before.
    const auto midi = rig.run(kBlockSeconds, {at(kLateInBlock, on(72))});
    for (int index = 0; index < midi.size(); ++index) {
        INFO("event " << index);
        if (midi.message(index).isNoteOn() && midi.message(index).getTimeStamp() < kLateInBlock)
            CHECK(midi.message(index).getNoteNumber() == 60);
    }

    bool playedTheNewPitch = false;
    for (int block = 2; block < 10 && !playedTheNewPitch; ++block) {
        const auto next = rig.run(block * kBlockSeconds);
        for (int index = 0; index < next.size(); ++index)
            playedTheNewPitch |=
                next.message(index).isNoteOn() && next.message(index).getNoteNumber() == 72;
    }
    CHECK(playedTheNewPitch);
}

TEST_CASE("Arpeggiator reads a host's sub-block offset once, for everything it emits",
          "[arpeggiator][midi][event-time]") {
    Rig rig;
    rig.startNote();

    // A host that hands the device a sub-block says where its buffer starts.
    // The offset belongs to every time the arp reads, so the reset it forwards
    // and the note-off it answers with land together rather than a sub-block
    // apart.
    rig.midiTimeOffset = 0.01;
    const double offsetInBlock = rig.midiTimeOffset + 0.005;
    auto midi = rig.run(kBlockSeconds, {at(0.005, juce::MidiMessage::allNotesOff(1))});

    REQUIRE(midi.size() == 2);
    CHECK(midi.message(0).isController());
    CHECK(midi.message(0).getTimeStamp() == Catch::Approx(offsetInBlock));
    CHECK(midi.message(1).isNoteOff());
    CHECK(midi.message(1).getNoteNumber() == 60);
    CHECK(midi.message(1).getTimeStamp() == Catch::Approx(offsetInBlock));
}

TEST_CASE("Arpeggiator plays the same phrase however the host cuts its callbacks",
          "[arpeggiator][midi][event-time]") {
    // Held, released and replaced at times that fall inside a block for one
    // partition and on a boundary for another.
    const std::vector<PhraseEvent> phrase{
        {0.0, on(60)},   {0.0, on(64)},   {0.313, off(60)}, {0.313, off(64)},
        {0.517, on(67)}, {0.911, on(71)}, {1.244, off(67)}, {1.244, off(71)},
    };
    constexpr double kTotalSeconds = 2.0;

    const auto reference = render(phrase, kTotalSeconds, {0.05});
    INFO(describe(reference));
    CHECK_FALSE(reference.empty());
    checkNotesBalance(reference);

    const std::vector<std::vector<double>> partitions{
        {64.0 / 48000.0},                              // small
        {2048.0 / 48000.0},                            // large
        {0.011, 0.0007, 0.043, 0.0031, 0.128, 0.019},  // irregular
    };

    for (const auto& blockLengths : partitions) {
        const auto played = render(phrase, kTotalSeconds, blockLengths);
        INFO("partition of " << blockLengths.size() << ", first block " << blockLengths.front());
        INFO(describe(played));
        checkNotesBalance(played);
        REQUIRE(played.size() == reference.size());
        for (size_t index = 0; index < played.size(); ++index) {
            INFO("event " << index);
            CHECK(played[index].seconds == Catch::Approx(reference[index].seconds).margin(1.0e-9));
            CHECK(played[index].message.getNoteNumber() ==
                  reference[index].message.getNoteNumber());
            CHECK(played[index].message.isNoteOn() == reference[index].message.isNoteOn());
        }
    }
}
