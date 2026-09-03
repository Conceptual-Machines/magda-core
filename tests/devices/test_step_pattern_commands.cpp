#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/StepPatternCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

// A pattern edit is one undoable step on the model's device state document
// (#2313). What is under test here is the coalescing rule: a drag writes the
// pattern on every mouse move and those writes belong in one undo entry, while
// anything the user would recognise as a separate action belongs in its own.
//
// No live engine in this binary, so TrackManager skips the projection and this
// is exactly the model-side contract.

namespace {

using namespace magda;
namespace ds = magda::device_state;

/// A step sequencer on a fresh track, with an empty history behind it.
ChainNodePath addSequencer(const juce::String& pluginId) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    const auto trackId = tracks.createTrack("Sequenced", TrackType::Media);

    ds::Doc doc;
    doc.deviceType = pluginId;

    DeviceInfo device;
    device.name = pluginId;
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    device.pluginState = ds::encode(doc);
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

/// One step of a velocity drag on @p step, at @p velocity.
bool dragVelocity(const ChainNodePath& path, int gestureId, int step, int velocity) {
    return editPolyStepPattern(
        path, "Set Step Velocity",
        [step, velocity](step_pattern::PolyPattern& pattern) {
            pattern.steps[static_cast<size_t>(step)].velocity = velocity;
        },
        StepPatternGesture::Continuous, gestureId);
}

int velocityOf(const ChainNodePath& path, int step) {
    return currentPolyPattern(path).steps[static_cast<size_t>(step)].velocity;
}

}  // namespace

TEST_CASE("One drag over a pattern lane is one undo step", "[steppattern][undo]") {
    const auto path = addSequencer("polystepsequencer");

    for (int velocity : {90, 70, 50, 40})
        REQUIRE(dragVelocity(path, /*gestureId=*/1, /*step=*/3, velocity));

    REQUIRE(velocityOf(path, 3) == 40);
    REQUIRE(UndoManager::getInstance().undo());
    // Back to where the drag started, not to its previous mouse position.
    REQUIRE(velocityOf(path, 3) == 100);
}

TEST_CASE("Two drags are two undo steps, not one", "[steppattern][undo]") {
    // Regression for #2335. Coalescing keyed on the device path, the command's
    // description and "this is a continuous edit", and nothing marked where a
    // gesture ended - so a second drag with the same description folded into
    // the first and one undo walked back both. The faceplate now bumps a token
    // when a drag ends, and only edits carrying the same token coalesce.
    const auto path = addSequencer("polystepsequencer");

    for (int velocity : {90, 40})
        REQUIRE(dragVelocity(path, /*gestureId=*/1, /*step=*/3, velocity));
    for (int velocity : {80, 20})
        REQUIRE(dragVelocity(path, /*gestureId=*/2, /*step=*/9, velocity));

    REQUIRE(velocityOf(path, 3) == 40);
    REQUIRE(velocityOf(path, 9) == 20);

    // The second drag alone.
    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(velocityOf(path, 9) == 100);
    REQUIRE(velocityOf(path, 3) == 40);

    // And the first, separately.
    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(velocityOf(path, 3) == 100);
}

TEST_CASE("A discrete edit does not swallow the drag after it", "[steppattern][undo]") {
    // canMergeWith looked only at the INCOMING command's gesture, so a discrete
    // edit sitting on top of the stack absorbed the next continuous one as long
    // as the description matched (#2335).
    const auto path = addSequencer("polystepsequencer");

    REQUIRE(editPolyStepPattern(
        path, "Set Step Velocity",
        [](step_pattern::PolyPattern& pattern) { pattern.steps[3].velocity = 60; },
        StepPatternGesture::Discrete, kNoStepPatternGesture));

    for (int velocity : {50, 30})
        REQUIRE(dragVelocity(path, /*gestureId=*/1, /*step=*/3, velocity));

    REQUIRE(velocityOf(path, 3) == 30);
    REQUIRE(UndoManager::getInstance().undo());
    // The drag alone comes off, leaving the discrete edit standing.
    REQUIRE(velocityOf(path, 3) == 60);
    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(velocityOf(path, 3) == 100);
}

TEST_CASE("A mono pattern drag coalesces on the same terms", "[steppattern][undo]") {
    const auto path = addSequencer("stepsequencer");

    const auto dragLength = [&path](int gestureId, int length) {
        return editMonoStepPattern(
            path, "Set Step Count",
            [length](step_pattern::MonoPattern& pattern) { pattern.length = length; },
            StepPatternGesture::Continuous, gestureId);
    };

    for (int length : {8, 12, 20})
        REQUIRE(dragLength(/*gestureId=*/1, length));
    REQUIRE(currentMonoPattern(path).length == 20);

    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(currentMonoPattern(path).length == 16);

    // Redo the drag, then a second one, and check they undo separately.
    REQUIRE(UndoManager::getInstance().redo());
    REQUIRE(dragLength(/*gestureId=*/2, 4));
    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(currentMonoPattern(path).length == 20);
}
