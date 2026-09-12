#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/host/LiveMidiCollector.hpp"
#include "magda/daw/engine/host/LiveMidiRouting.hpp"

/// @file What the callback makes of the queue, per slot (#2590).

namespace {

namespace host = magda::daw::engine_host;

magda::TrackInfo monitoring(magda::TrackId id) {
    magda::TrackInfo track;
    track.id = id;
    track.inputMonitor = magda::InputMonitorMode::In;
    track.midiInputDevice = "all";
    return track;
}

juce::MidiMessage noteOn(int note) {
    return juce::MidiMessage::noteOn(1, note, 1.0f);
}

/// The notes @p streams carries for @p source, which is what one track hears.
std::vector<int> notesFor(std::span<const magda::engine::LiveMidiStream> streams, int source) {
    std::vector<int> notes;
    for (const auto& stream : streams) {
        if (stream.source != source || stream.events == nullptr)
            continue;

        for (const auto metadata : *stream.events)
            if (metadata.getMessage().isNoteOn())
                notes.push_back(metadata.getMessage().getNoteNumber());
    }
    return notes;
}

/// Queues @p note under whatever @p trackId's audition is right now, the way a
/// producer resolves it before pushing.
void play(host::LiveMidiQueue& queue, host::LiveMidiSources& sources, magda::TrackId trackId,
          int note) {
    const auto source = sources.auditionSourceFor(trackId);
    queue.push(source, sources.slotFor(source), noteOn(note));
}

}  // namespace

TEST_CASE("A note reaches the source it was played under", "[live-midi-collector][2590]") {
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);
    host::LiveMidiQueue queue;
    host::LiveMidiCollector collector;
    collector.prepare();

    REQUIRE(routing.resolve({monitoring(1), monitoring(2)}) != nullptr);
    const auto first = sources.auditionSourceFor(1);
    const auto second = sources.auditionSourceFor(2);

    play(queue, sources, 1, 60);
    play(queue, sources, 2, 64);

    const auto streams = collector.collect(queue, sources);
    CHECK(notesFor(streams, first) == std::vector<int>{60});
    CHECK(notesFor(streams, second) == std::vector<int>{64});
}

TEST_CASE("A note queued by a track that has gone reaches nobody", "[live-midi-collector][2590]") {
    // The push resolved a slot the model has since given to another track. The
    // event names both, so the two can be seen not to agree.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);
    host::LiveMidiQueue queue;
    host::LiveMidiCollector collector;
    collector.prepare();

    REQUIRE(routing.resolve({monitoring(1)}) != nullptr);
    const auto gone = sources.auditionSourceFor(1);
    play(queue, sources, 1, 60);

    // The track goes, and the next one takes the slot it left.
    REQUIRE(routing.resolve({monitoring(2)}) != nullptr);
    const auto taken = sources.auditionSourceFor(2);
    REQUIRE(sources.slotFor(taken) != host::LiveMidiSources::kNoSlot);
    REQUIRE(taken != gone);

    const auto streams = collector.collect(queue, sources);
    CHECK(notesFor(streams, gone).empty());
    CHECK(notesFor(streams, taken).empty());
    CHECK(collector.dropped() == 1);
}

TEST_CASE("A slot that changes hands mid-callback keeps one owner's notes",
          "[live-midi-collector][2590]") {
    // Both were queued while their sender held the slot, and the handover
    // happened before the callback read either. One buffer cannot be two
    // tracks', and labelling it with the second would play the first's notes
    // on the second, so the first through keeps it.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);
    host::LiveMidiQueue queue;
    host::LiveMidiCollector collector;
    collector.prepare();

    REQUIRE(routing.resolve({monitoring(1)}) != nullptr);
    const auto first = sources.auditionSourceFor(1);
    const auto slot = sources.slotFor(first);
    play(queue, sources, 1, 60);

    REQUIRE(routing.resolve({monitoring(2)}) != nullptr);
    const auto second = sources.auditionSourceFor(2);
    REQUIRE(sources.slotFor(second) == slot);
    play(queue, sources, 2, 64);

    const auto streams = collector.collect(queue, sources);

    // The registry says the slot is the second track's, so the first track's
    // note is refused; what it must never be is handed over under either name.
    CHECK(notesFor(streams, first).empty());
    CHECK(notesFor(streams, second) == std::vector<int>{64});
    CHECK(collector.dropped() == 1);
}

TEST_CASE("A source the room had no slot for is dropped and counted",
          "[live-midi-collector][2590]") {
    host::LiveMidiSources sources;
    host::LiveMidiQueue queue;
    host::LiveMidiCollector collector;
    collector.prepare();

    for (auto i = 0; i < host::LiveMidiSources::kSlots; ++i)
        sources.auditionSourceFor(static_cast<magda::TrackId>(i + 1));

    const auto crowded = sources.auditionSourceFor(host::LiveMidiSources::kSlots + 1);
    REQUIRE(sources.slotFor(crowded) == host::LiveMidiSources::kNoSlot);

    play(queue, sources, host::LiveMidiSources::kSlots + 1, 60);

    const auto streams = collector.collect(queue, sources);
    CHECK(streams.empty());
    CHECK(collector.dropped() == 1);
}

TEST_CASE("A buffer carries nothing from the block before it", "[live-midi-collector][2590]") {
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);
    host::LiveMidiQueue queue;
    host::LiveMidiCollector collector;
    collector.prepare();

    REQUIRE(routing.resolve({monitoring(1)}) != nullptr);
    const auto source = sources.auditionSourceFor(1);

    play(queue, sources, 1, 60);
    REQUIRE(notesFor(collector.collect(queue, sources), source) == std::vector<int>{60});

    CHECK(collector.collect(queue, sources).empty());
}
