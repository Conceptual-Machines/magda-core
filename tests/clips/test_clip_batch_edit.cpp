#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "magda/daw/core/ClipBatchEdit.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

class SetIntCommand : public UndoableCommand {
  public:
    SetIntCommand(int& target, int value) : target_(target), oldValue_(target), newValue_(value) {}

    void execute() override {
        target_ = newValue_;
    }

    void undo() override {
        target_ = oldValue_;
    }

    juce::String getDescription() const override {
        return "Set Int";
    }

  private:
    int& target_;
    int oldValue_;
    int newValue_;
};

}  // namespace

TEST_CASE("ClipBatchEdit groups multi-clip edits into one undo step", "[undo][clip][batch]") {
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();

    int first = 0;
    int second = 0;

    {
        ClipBatchEdit batch("Set Multiple Clips", 2);
        batch.execute(std::make_unique<SetIntCommand>(first, 1));
        batch.execute(std::make_unique<SetIntCommand>(second, 2));
    }

    REQUIRE(first == 1);
    REQUIRE(second == 2);
    REQUIRE(undo.getUndoDescription() == "Set Multiple Clips");

    REQUIRE(undo.undo());
    REQUIRE(first == 0);
    REQUIRE(second == 0);
    REQUIRE_FALSE(undo.canUndo());
}

TEST_CASE("ClipBatchEdit leaves single-clip edits unbatched", "[undo][clip][batch]") {
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();

    int value = 0;

    {
        ClipBatchEdit batch("Set One Clip", 1);
        batch.execute(std::make_unique<SetIntCommand>(value, 7));
    }

    REQUIRE(value == 7);
    REQUIRE(undo.getUndoDescription() == "Set Int");

    REQUIRE(undo.undo());
    REQUIRE(value == 0);
    REQUIRE_FALSE(undo.canUndo());
}

TEST_CASE("SetClipPropertyCommand restores setter side effects", "[undo][clip][property]") {
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();

    auto trackId = TrackManager::getInstance().createTrack("Track", TrackType::Media);
    auto clipId = ClipManager::getInstance().createAudioClip(trackId, 0.0, 4.0, "test.wav",
                                                             ClipView::Arrangement);
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);

    primaryEventOf(clip)->warpEnabled = false;
    primaryEventOf(clip)->analogPitch = true;

    undo.executeCommand(std::make_unique<SetClipPropertyCommand>(
        clipId, "Set Clip Warp",
        [](ClipManager& manager, ClipId id) { manager.setClipWarpEnabled(id, true); }));

    REQUIRE(primaryEventOf(clip)->warpEnabled);
    REQUIRE_FALSE(primaryEventOf(clip)->analogPitch);

    REQUIRE(undo.undo());
    REQUIRE_FALSE(primaryEventOf(clip)->warpEnabled);
    REQUIRE(primaryEventOf(clip)->analogPitch);
}

namespace {
struct ThrowingUndoListener : UndoManagerListener {
    bool called = false;
    void undoStateChanged() override {
        called = true;
        throw std::runtime_error("listener boom");
    }
};
}  // namespace

TEST_CASE("CompoundOperationScope destructor survives a throwing listener", "[undo]") {
    // #2395: ~CompoundOperationScope calls endCompoundOperation(), which
    // notifies listeners. A listener throw used to escape an
    // implicitly-noexcept destructor (std::terminate); it must now be
    // swallowed.
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();

    ThrowingUndoListener listener;
    undo.addListener(&listener);

    int value = 0;
    {
        CompoundOperationScope scope("Test Op");
        undo.executeCommand(std::make_unique<SetIntCommand>(value, 1));
    }

    undo.removeListener(&listener);
    REQUIRE(listener.called);
    REQUIRE(value == 1);
}
