#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/api/remote_changes.hpp"

using namespace magda;
using namespace magda::remote;

namespace {

/// Collects everything a listener is told, so a test can assert on the shape of
/// the delivered batches rather than only on the final state.
struct Recorder {
    std::vector<std::vector<ChangeSource::Change>> batches;

    ChangeSource::Listener listener() {
        return [this](const std::vector<ChangeSource::Change>& changes) {
            batches.push_back(changes);
        };
    }

    std::size_t totalChanges() const {
        std::size_t total = 0;
        for (const auto& batch : batches)
            total += batch.size();
        return total;
    }
};

}  // namespace

TEST_CASE("Topic names round-trip through their wire form", "[remote][changes]") {
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
        const auto topic = static_cast<Topic>(index);
        INFO("topic: " << toString(topic));
        REQUIRE(parseTopic(toString(topic)) == topic);
    }

    // Unknown topics are rejected rather than silently mapped to the first
    // enumerator, which would subscribe a client to the wrong stream.
    REQUIRE_FALSE(parseTopic("not_a_topic").has_value());
    REQUIRE_FALSE(parseTopic("").has_value());
}

TEST_CASE("A burst of changes to one topic coalesces to a single notification",
          "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    // The case this exists for: a 100 Hz fader move must not become 100
    // notifications per subscriber.
    for (Revision revision = 1; revision <= 100; ++revision)
        changes.markChanged(Topic::Tracks, revision);

    changes.flush();

    REQUIRE(recorder.batches.size() == 1);
    REQUIRE(recorder.batches[0].size() == 1);
    REQUIRE(recorder.batches[0][0].topic == Topic::Tracks);
    // Latest-value-wins: the newest revision survives, not the first.
    REQUIRE(recorder.batches[0][0].revision == 100);
}

TEST_CASE("Out-of-order marks keep the highest revision", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    changes.markChanged(Topic::Clips, 40);
    changes.markChanged(Topic::Clips, 12);
    changes.flush();

    REQUIRE(recorder.batches.size() == 1);
    REQUIRE(recorder.batches[0][0].revision == 40);
}

TEST_CASE("Distinct topics are delivered in one batch", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    changes.markChanged(Topic::Tracks, 1);
    changes.markChanged(Topic::Clips, 2);
    changes.markChanged(Topic::Automation, 3);
    changes.flush();

    REQUIRE(recorder.batches.size() == 1);
    REQUIRE(recorder.batches[0].size() == 3);

    // Ordering follows the enum, so a subscriber sees a stable topic order.
    REQUIRE(recorder.batches[0][0].topic == Topic::Tracks);
    REQUIRE(recorder.batches[0][1].topic == Topic::Clips);
    REQUIRE(recorder.batches[0][2].topic == Topic::Automation);
}

TEST_CASE("Flushing with nothing pending notifies nobody", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    changes.flush();
    changes.flush();

    REQUIRE(recorder.batches.empty());
}

TEST_CASE("A flush clears the pending set", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    changes.markChanged(Topic::Transport, 5);
    changes.flush();
    changes.flush();

    // The second flush must not repeat the first one's changes.
    REQUIRE(recorder.batches.size() == 1);
}

TEST_CASE("Removed listeners stop receiving changes", "[remote][changes]") {
    ChangeSource changes;
    Recorder first;
    Recorder second;

    const auto firstToken = changes.addListener(first.listener());
    changes.addListener(second.listener());

    changes.markChanged(Topic::Devices, 1);
    changes.flush();

    changes.removeListener(firstToken);
    changes.markChanged(Topic::Devices, 2);
    changes.flush();

    REQUIRE(first.totalChanges() == 1);
    REQUIRE(second.totalChanges() == 2);
}

TEST_CASE("Discarded changes are never delivered", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    changes.markChanged(Topic::Session, 7);
    // Project replacement: the pending revision describes state that no longer
    // exists, so delivering it would point subscribers at a dead project.
    changes.discardPending();
    changes.flush();

    REQUIRE(recorder.batches.empty());
}

TEST_CASE("A listener may unsubscribe from inside its own callback", "[remote][changes]") {
    ChangeSource changes;
    int calls = 0;
    int token = 0;

    token = changes.addListener([&](const std::vector<ChangeSource::Change>&) {
        ++calls;
        // Notification copies the listener list before invoking it, so this
        // cannot deadlock on the listener mutex or invalidate the iteration.
        changes.removeListener(token);
    });

    changes.markChanged(Topic::Project, 1);
    changes.flush();
    changes.markChanged(Topic::Project, 2);
    changes.flush();

    REQUIRE(calls == 1);
}

TEST_CASE("Every topic can be marked and delivered independently", "[remote][changes]") {
    ChangeSource changes;
    Recorder recorder;
    changes.addListener(recorder.listener());

    for (std::size_t index = 0; index < TOPIC_COUNT; ++index)
        changes.markChanged(static_cast<Topic>(index), static_cast<Revision>(index + 1));
    changes.flush();

    REQUIRE(recorder.batches.size() == 1);
    REQUIRE(recorder.batches[0].size() == TOPIC_COUNT);
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
        REQUIRE(recorder.batches[0][index].topic == static_cast<Topic>(index));
        REQUIRE(recorder.batches[0][index].revision == static_cast<Revision>(index + 1));
    }
}
