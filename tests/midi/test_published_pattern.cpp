#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

#include "magda/daw/audio/sequencer/PublishedPattern.hpp"
#include "magda/daw/audio/sequencer/StepPattern.hpp"

// The model publishes a pattern, one audio thread plays it (#2313). Two slots
// and an index were not enough on their own: an index says which slot is
// current, not whether the audio thread still holds a reference to the other,
// so two publishes inside one block rewrote the pattern being played (#2335).

namespace seq = magda::daw::audio::sequencer;

namespace {

seq::MonoPattern patternOfLength(int length, int firstNote) {
    seq::MonoPattern pattern;
    pattern.length = length;
    for (auto& step : pattern.steps)
        step.noteNumber = firstNote;
    return pattern;
}

}  // namespace

TEST_CASE("A published pattern reaches the next hold", "[sequencer][publish]") {
    seq::PublishedPattern<seq::MonoPattern> published;
    published.publish(patternOfLength(8, 48));

    {
        const seq::PublishedPattern<seq::MonoPattern>::Hold hold{published};
        REQUIRE(hold.isValid());
        REQUIRE(hold.pattern().length == 8);
        REQUIRE(hold.pattern().steps[0].noteNumber == 48);
    }

    published.publish(patternOfLength(3, 72));
    const seq::PublishedPattern<seq::MonoPattern>::Hold hold{published};
    REQUIRE(hold.isValid());
    REQUIRE(hold.pattern().length == 3);
}

TEST_CASE("The message thread reads back what it last published", "[sequencer][publish]") {
    seq::PublishedPattern<seq::MonoPattern> published;
    REQUIRE(published.current().length == 16);  // the default pattern

    published.publish(patternOfLength(5, 60));
    REQUIRE(published.current().length == 5);

    // Reading it back while the audio thread is holding it is two readers, and
    // must not disturb what the hold is looking at.
    const seq::PublishedPattern<seq::MonoPattern>::Hold hold{published};
    REQUIRE(published.current().length == 5);
    REQUIRE(hold.pattern().length == 5);
}

TEST_CASE("A publish waits for the block that is reading", "[sequencer][publish]") {
    // The defect, exactly: a publish while a block is mid-read. Flipping an
    // index let it through and rewrote the slot the audio thread still had a
    // reference to; handing the slot over instead makes the publish wait, and
    // what the block is playing cannot move under it.
    seq::PublishedPattern<seq::MonoPattern> published;
    published.publish(patternOfLength(4, 40));

    std::atomic<int> publishes{0};
    std::thread writer;
    {
        const seq::PublishedPattern<seq::MonoPattern>::Hold hold{published};
        REQUIRE(hold.isValid());
        REQUIRE(hold.pattern().length == 4);

        writer = std::thread([&published, &publishes] {
            published.publish(patternOfLength(6, 50));
            publishes.store(1, std::memory_order_release);
            published.publish(patternOfLength(7, 55));
            publishes.store(2, std::memory_order_release);
        });

        for (int i = 0; i < 2000; ++i) {
            REQUIRE(hold.pattern().length == 4);
            REQUIRE(publishes.load(std::memory_order_acquire) == 0);
        }
    }

    // Released: both publishes go through, and the next block plays the newest.
    writer.join();
    const seq::PublishedPattern<seq::MonoPattern>::Hold next{published};
    REQUIRE(next.pattern().length == 7);
}
