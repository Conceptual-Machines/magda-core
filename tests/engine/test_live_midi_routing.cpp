#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "magda/daw/engine/host/LiveMidiRouting.hpp"

/// @file The model's input routing as one snapshot (#2592).

namespace {

namespace host = magda::daw::engine_host;

const juce::MidiDeviceInfo kKeystep{"Keystep", "keystep-identifier"};
const juce::MidiDeviceInfo kPush{"Push", "push-identifier"};

magda::TrackInfo monitoring(magda::TrackId id, const juce::String& device) {
    magda::TrackInfo track;
    track.id = id;
    track.inputMonitor = magda::InputMonitorMode::In;
    track.midiInputDevice = device;
    return track;
}

const magda::engine::TrackLiveMidi& entryFor(const magda::engine::LiveRouting& routing,
                                             magda::TrackId id) {
    const auto* found = routing.find(id);
    REQUIRE(found != nullptr);
    return *found;
}

}  // namespace

TEST_CASE("A monitoring track's route resolves to the devices it names", "[live-routing]") {
    host::LiveMidiSources sources;
    sources.registerAvailableDevices({kKeystep, kPush});
    host::LiveMidiRouting routing(sources);

    SECTION("An all route is every connected device") {
        const auto snapshot = routing.resolve({monitoring(1, "all")});
        REQUIRE(snapshot != nullptr);

        CHECK(entryFor(*snapshot, 1).sources.size() == 2);
    }

    SECTION("An empty field is None, not every input") {
        const auto snapshot = routing.resolve({monitoring(1, "")});
        REQUIRE(snapshot != nullptr);

        // Read as "all", a track switched to None played every keyboard.
        CHECK(entryFor(*snapshot, 1).sources.empty());
    }

    SECTION("A named device is that device alone") {
        const auto snapshot = routing.resolve({monitoring(1, kKeystep.identifier)});
        REQUIRE(snapshot != nullptr);

        const auto& entry = entryFor(*snapshot, 1);
        REQUIRE(entry.sources.size() == 1);
        CHECK(entry.sources.front() == sources.sourceFor(kKeystep.identifier));
    }

    SECTION("A track route names no device: the plan carries it as an edge") {
        const auto snapshot = routing.resolve({monitoring(1, "track:7")});
        REQUIRE(snapshot != nullptr);

        CHECK(entryFor(*snapshot, 1).sources.empty());
    }

    SECTION("A track that is not monitoring hears none of them") {
        auto track = monitoring(1, "all");
        track.inputMonitor = magda::InputMonitorMode::Off;

        const auto snapshot = routing.resolve({track});
        REQUIRE(snapshot != nullptr);

        CHECK(entryFor(*snapshot, 1).sources.empty());
        CHECK(entryFor(*snapshot, 1).audition == sources.auditionSourceFor(1));
    }

    SECTION("An unarmed Auto track is the UI's activity light, not an audible input") {
        auto track = monitoring(1, "all");
        track.inputMonitor = magda::InputMonitorMode::Auto;

        const auto snapshot = routing.resolve({track});
        REQUIRE(snapshot != nullptr);

        CHECK(track.receivesLiveMidiInput());
        CHECK(entryFor(*snapshot, 1).sources.empty());
    }
}

TEST_CASE("Every track's audition is its own", "[live-routing]") {
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    const auto snapshot = routing.resolve({monitoring(1, ""), monitoring(2, "")});
    REQUIRE(snapshot != nullptr);

    CHECK(entryFor(*snapshot, 1).audition != entryFor(*snapshot, 2).audition);
}

TEST_CASE("A snapshot is addressed by track id whatever order the model is in", "[live-routing]") {
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    // Arrangement order, which find()'s binary search would read as missing.
    const auto snapshot =
        routing.resolve({monitoring(9, ""), monitoring(3, ""), monitoring(6, "")});
    REQUIRE(snapshot != nullptr);

    CHECK(snapshot->find(3) != nullptr);
    CHECK(snapshot->find(6) != nullptr);
    CHECK(snapshot->find(9) != nullptr);
    CHECK(snapshot->find(4) == nullptr);
}

TEST_CASE("A routing that has not moved is not republished", "[live-routing]") {
    host::LiveMidiSources sources;
    sources.registerAvailableDevices({kKeystep});
    host::LiveMidiRouting routing(sources);

    const std::vector<magda::TrackInfo> tracks{monitoring(1, "all")};

    REQUIRE(routing.resolve(tracks) != nullptr);

    CHECK(routing.resolve(tracks) == nullptr);

    auto muted = tracks;
    muted.front().inputMonitor = magda::InputMonitorMode::Off;
    CHECK(routing.resolve(muted) != nullptr);
}

TEST_CASE("A source a track loses is counted, so the input can panic for it", "[live-routing]") {
    host::LiveMidiSources sources;
    sources.registerAvailableDevices({kKeystep});
    host::LiveMidiRouting routing(sources);

    const std::vector<magda::TrackInfo> tracks{monitoring(1, "all")};

    const auto first = routing.resolve(tracks);
    REQUIRE(first != nullptr);
    const auto before = entryFor(*first, 1).sourcesLost;

    SECTION("A device unplugged leaves every all route") {
        sources.registerAvailableDevices({});

        const auto after = routing.resolve(tracks);
        REQUIRE(after != nullptr);

        CHECK(entryFor(*after, 1).sources.empty());
        CHECK(entryFor(*after, 1).sourcesLost != before);
    }

    SECTION("Monitoring switched off takes the route with it") {
        auto idle = tracks;
        idle.front().inputMonitor = magda::InputMonitorMode::Off;

        const auto after = routing.resolve(idle);
        REQUIRE(after != nullptr);

        CHECK(entryFor(*after, 1).sourcesLost != before);
    }

    SECTION("A device arriving takes nothing away, so a held chord keeps sounding") {
        sources.registerAvailableDevices({kKeystep, kPush});

        const auto after = routing.resolve(tracks);
        REQUIRE(after != nullptr);

        CHECK(entryFor(*after, 1).sources.size() == 2);
        CHECK(entryFor(*after, 1).sourcesLost == before);
    }

    SECTION("A new project starts over: track 1 there never heard this one's device") {
        routing.reset();
        sources.registerAvailableDevices({});

        const auto after = routing.resolve(tracks);
        REQUIRE(after != nullptr);

        CHECK(entryFor(*after, 1).sourcesLost == 0);
    }
}

TEST_CASE("A track that is gone gives its callback slot back", "[live-routing][2590]") {
    // The room the callback has is a project size, not a session's history:
    // one slot is taken for every track that reads MIDI, so without this an
    // evening of building and deleting tracks runs it out and the callback
    // drops what it cannot place.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);
    const auto room = sources.freeSlots();

    REQUIRE(routing.resolve({monitoring(1, "all"), monitoring(2, "all"), monitoring(3, "all")}) !=
            nullptr);
    const auto second = sources.auditionSourceFor(2);
    CHECK(sources.freeSlots() == room - 3);

    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);
    CHECK(sources.freeSlots() == room - 1);
    CHECK(sources.slotFor(second) == host::LiveMidiSources::kNoSlot);

    // A slot comes back; the id never does, so nothing a producer resolved
    // before the track went can be mistaken for the track that follows it.
    const auto next = sources.auditionSourceFor(4);
    CHECK(next != second);
    CHECK(sources.slotFor(next) != host::LiveMidiSources::kNoSlot);
    CHECK(sources.freeSlots() == room - 2);
}

TEST_CASE("A slot answers to the id that holds it now", "[live-routing][2590]") {
    // What the callback asks of an event that outlived its track: the id and
    // the slot have to agree, or the note belongs to nobody.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);
    const auto gone = sources.auditionSourceFor(1);
    const auto slot = sources.slotFor(gone);
    REQUIRE(slot != host::LiveMidiSources::kNoSlot);
    CHECK(sources.ownerOfSlot(slot) == gone);

    REQUIRE(routing.resolve({}) != nullptr);
    CHECK(sources.ownerOfSlot(slot) == host::LiveMidiSources::kNoSource);

    const auto taken = sources.auditionSourceFor(2);
    REQUIRE(sources.slotFor(taken) == slot);
    CHECK(sources.ownerOfSlot(slot) == taken);
    CHECK(sources.ownerOfSlot(slot) != gone);
}

TEST_CASE("A track that stays keeps the slot it had", "[live-routing][2590]") {
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    REQUIRE(routing.resolve({monitoring(1, "all"), monitoring(2, "all")}) != nullptr);
    const auto first = sources.auditionSourceFor(1);
    const auto slot = sources.slotFor(first);

    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);

    CHECK(sources.auditionSourceFor(1) == first);
    CHECK(sources.slotFor(first) == slot);
}

TEST_CASE("An id that waited for room gets a slot when one comes back", "[live-routing][2590]") {
    // A project bigger than the room, cut down to one that fits: the tracks
    // that went without must not be the ones that stay silent.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    std::vector<magda::TrackInfo> crowded;
    for (auto i = 0; i < host::LiveMidiSources::kSlots + 4; ++i)
        crowded.push_back(monitoring(static_cast<magda::TrackId>(i + 1), "all"));

    REQUIRE(routing.resolve(crowded) != nullptr);
    const auto last = sources.auditionSourceFor(host::LiveMidiSources::kSlots + 4);
    CHECK(sources.slotFor(last) == host::LiveMidiSources::kNoSlot);

    REQUIRE(routing.resolve({monitoring(host::LiveMidiSources::kSlots + 4, "all")}) != nullptr);
    CHECK(sources.slotFor(last) != host::LiveMidiSources::kNoSlot);
    CHECK(sources.ownerOfSlot(sources.slotFor(last)) == last);
}

TEST_CASE("A track swap does not leave the tracks that stay without room", "[live-routing][2590]") {
    // resolve() gives the room back before it asks for any, so replacing a
    // project's tracks wholesale is not a project twice the size.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    const auto half = host::LiveMidiSources::kSlots * 3 / 4;
    const auto projectOf = [half](int from) {
        std::vector<magda::TrackInfo> tracks;
        for (auto i = 0; i < half; ++i)
            tracks.push_back(monitoring(static_cast<magda::TrackId>(from + i), "all"));
        return tracks;
    };

    REQUIRE(routing.resolve(projectOf(1)) != nullptr);
    REQUIRE(routing.resolve(projectOf(1000)) != nullptr);

    for (auto i = 0; i < half; ++i) {
        const auto source = sources.auditionSourceFor(static_cast<magda::TrackId>(1000 + i));
        CHECK(sources.slotFor(source) != host::LiveMidiSources::kNoSlot);
    }
}

TEST_CASE("A project past the room gets ids with nowhere to put them", "[live-routing][2590]") {
    // Not silence the callback cannot explain: the id is still the model's, so
    // the route resolves and the drop is counted where the trace prints it.
    host::LiveMidiSources sources;

    std::vector<int> taken;
    for (auto i = 0; i < host::LiveMidiSources::kSlots; ++i)
        taken.push_back(sources.auditionSourceFor(static_cast<magda::TrackId>(i + 1)));

    CHECK(sources.freeSlots() == 0);

    const auto crowded = sources.auditionSourceFor(host::LiveMidiSources::kSlots + 1);
    CHECK(crowded != host::LiveMidiSources::kNoSource);
    CHECK(sources.slotFor(crowded) == host::LiveMidiSources::kNoSlot);
}
