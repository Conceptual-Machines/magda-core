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

TEST_CASE("A track that is gone gives its audition id back", "[live-routing][2590]") {
    // The callback has room for a project's worth of sources, not a session's:
    // one is taken for every track that reads MIDI, so a counter that only
    // climbs turns an evening of editing into silence it cannot explain.
    std::atomic<std::uint64_t> drains{0};
    host::LiveMidiSources sources;
    sources.observeDrains(drains);
    host::LiveMidiRouting routing(sources);

    REQUIRE(routing.resolve({monitoring(1, "all"), monitoring(2, "all"), monitoring(3, "all")}) !=
            nullptr);
    const auto second = sources.auditionSourceFor(2);
    const auto third = sources.auditionSourceFor(3);

    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);
    CHECK(sources.waitingToBeReused() == 2);

    // Not before the callback has consumed whatever was queued under them.
    const auto waiting = sources.auditionSourceFor(4);
    CHECK(waiting != second);
    CHECK(waiting != third);

    drains.fetch_add(2, std::memory_order_relaxed);

    const auto reused = sources.auditionSourceFor(5);
    CHECK((reused == second || reused == third));
    CHECK(sources.waitingToBeReused() == 1);
}

TEST_CASE("A host with no callback to wait on reuses nothing", "[live-routing][2590]") {
    // The default, and what a test or a headless tool gets: no counter to
    // watch means no way to know an id is spent, so it is not handed out.
    host::LiveMidiSources sources;
    host::LiveMidiRouting routing(sources);

    REQUIRE(routing.resolve({monitoring(1, "all"), monitoring(2, "all")}) != nullptr);
    const auto second = sources.auditionSourceFor(2);

    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);
    CHECK(sources.waitingToBeReused() == 1);
    CHECK(sources.auditionSourceFor(3) != second);
}

TEST_CASE("A track that stays keeps the id it had", "[live-routing][2590]") {
    std::atomic<std::uint64_t> drains{0};
    host::LiveMidiSources sources;
    sources.observeDrains(drains);
    host::LiveMidiRouting routing(sources);

    REQUIRE(routing.resolve({monitoring(1, "all"), monitoring(2, "all")}) != nullptr);
    const auto first = sources.auditionSourceFor(1);

    drains.fetch_add(4, std::memory_order_relaxed);
    REQUIRE(routing.resolve({monitoring(1, "all")}) != nullptr);

    CHECK(sources.auditionSourceFor(1) == first);
}
