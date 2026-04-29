#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/SelectionManager.hpp"

using namespace magda;

namespace {

// Minimal listener that runs an arbitrary action inside trackSelectionChanged.
// Used to provoke the reentrant-add and add/remove patterns that crashed the
// listener iteration before the pendingAdditions_ fix landed.
struct ReentrantTrackListener : public SelectionManagerListener {
    std::function<void()> onTrackChanged;
    int callCount = 0;

    void selectionTypeChanged(SelectionType) override {}
    void trackSelectionChanged(TrackId) override {
        ++callCount;
        if (onTrackChanged)
            onTrackChanged();
    }
};

}  // namespace

TEST_CASE("SelectionManager - listener added during notify does not crash",
          "[selection][listeners]") {
    auto& sm = SelectionManager::getInstance();

    ReentrantTrackListener outer;
    ReentrantTrackListener added;

    // Force the vector to a small capacity so push_back during notify will
    // reallocate — the exact pattern that crashed the in-flight ranged-for.
    sm.addListener(&outer);

    outer.onTrackChanged = [&]() {
        // Reentrantly register a new listener while the outer notify is still
        // walking listeners_. Pre-fix this could reallocate the vector and
        // invalidate the iterator, dereferencing the 0xff..ff sentinel on the
        // next step.
        sm.addListener(&added);
    };

    sm.selectTrack(TrackId{42});

    REQUIRE(outer.callCount == 1);
    // The listener registered mid-notify must not be invoked for the
    // in-flight event (it wasn't a listener when the event started), but
    // must be live for the next event.
    REQUIRE(added.callCount == 0);

    outer.onTrackChanged = nullptr;
    sm.selectTrack(TrackId{43});
    REQUIRE(added.callCount == 1);

    sm.removeListener(&outer);
    sm.removeListener(&added);
}

TEST_CASE("SelectionManager - add+remove during notify cancels the deferred add",
          "[selection][listeners]") {
    auto& sm = SelectionManager::getInstance();

    ReentrantTrackListener outer;
    ReentrantTrackListener transient;

    sm.addListener(&outer);

    outer.onTrackChanged = [&]() {
        sm.addListener(&transient);
        sm.removeListener(&transient);
    };

    sm.selectTrack(TrackId{1});

    // After the add+remove pair during notify, transient must NOT linger in
    // pendingAdditions_ — otherwise the outermost guard would resurrect it
    // and a later notify would call into a freed listener.
    outer.onTrackChanged = nullptr;
    sm.selectTrack(TrackId{2});
    REQUIRE(transient.callCount == 0);

    sm.removeListener(&outer);
}
