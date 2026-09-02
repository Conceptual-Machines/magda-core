#include "StepPatternCommands.hpp"

#include <memory>
#include <utility>

#include "DeviceInfo.hpp"
#include "ProjectManager.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

namespace {

// The sequencers' device ids, spelled out rather than included: core must not
// depend on a concrete device header (the CMake scan enforces it), and these
// are a frozen persistence surface anyway - saved projects carry them.
constexpr const char* kMonoSequencerId = "stepsequencer";
constexpr const char* kPolySequencerId = "polystepsequencer";

/// The device at @p path when it is the internal sequencer @p deviceId names
/// and its saved state is one this build can rewrite.
const DeviceInfo* editableSequencer(const ChainNodePath& path, const char* deviceId) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr || device->format != PluginFormat::Internal)
        return nullptr;
    if (device->pluginId != deviceId)
        return nullptr;
    // The precondition updateDeviceAuthoredState() enforces, checked here so a
    // refused edit is refused BEFORE SnapshotCommand marks the command executed
    // and the UndoManager records a dirty no-op undo step.
    if (device_state::isFutureDeviceState(device->pluginState))
        return nullptr;
    return device;
}

juce::String capturedState(const ChainNodePath& path) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    return device != nullptr ? device->pluginState : juce::String();
}

void restoreCapturedState(const ChainNodePath& path, const juce::String& state) {
    TrackManager::getInstance().setDeviceAuthoredState(path, state);
    ProjectManager::getInstance().markDirty();
}

}  // namespace

step_pattern::MonoPattern currentMonoPattern(const ChainNodePath& devicePath) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (device == nullptr)
        return {};
    return step_pattern::monoPatternOf(device->pluginState);
}

step_pattern::PolyPattern currentPolyPattern(const ChainNodePath& devicePath) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (device == nullptr)
        return {};
    return step_pattern::polyPatternOf(device->pluginState);
}

// =============================================================================
// Mono
// =============================================================================

SetMonoStepPatternCommand::SetMonoStepPatternCommand(const ChainNodePath& devicePath,
                                                     step_pattern::MonoPattern pattern,
                                                     juce::String description,
                                                     StepPatternGesture gesture)
    : devicePath_(devicePath),
      pattern_(pattern),
      description_(std::move(description)),
      gesture_(gesture) {}

juce::String SetMonoStepPatternCommand::getDescription() const {
    return description_;
}

bool SetMonoStepPatternCommand::canExecute() const {
    return editableSequencer(devicePath_, kMonoSequencerId) != nullptr;
}

bool SetMonoStepPatternCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* next = dynamic_cast<const SetMonoStepPatternCommand*>(other);
    return next != nullptr && next->gesture_ == StepPatternGesture::Continuous &&
           next->devicePath_ == devicePath_ && next->description_ == description_;
}

void SetMonoStepPatternCommand::mergeWith(const UndoableCommand* other) {
    // The state to undo BACK to is this command's, captured before the gesture
    // began; what the gesture ended on is the newer command's pattern.
    if (const auto* next = dynamic_cast<const SetMonoStepPatternCommand*>(other))
        pattern_ = next->pattern_;
}

juce::String SetMonoStepPatternCommand::captureState() {
    return capturedState(devicePath_);
}

void SetMonoStepPatternCommand::restoreState(const juce::String& state) {
    restoreCapturedState(devicePath_, state);
}

void SetMonoStepPatternCommand::performAction() {
    TrackManager::getInstance().updateDeviceAuthoredState(
        devicePath_, [this](device_state::Doc& doc) { step_pattern::writeMono(doc, pattern_); });
    ProjectManager::getInstance().markDirty();
}

// =============================================================================
// Poly
// =============================================================================

SetPolyStepPatternCommand::SetPolyStepPatternCommand(const ChainNodePath& devicePath,
                                                     step_pattern::PolyPattern pattern,
                                                     juce::String description,
                                                     StepPatternGesture gesture)
    : devicePath_(devicePath),
      pattern_(pattern),
      description_(std::move(description)),
      gesture_(gesture) {}

juce::String SetPolyStepPatternCommand::getDescription() const {
    return description_;
}

bool SetPolyStepPatternCommand::canExecute() const {
    return editableSequencer(devicePath_, kPolySequencerId) != nullptr;
}

bool SetPolyStepPatternCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* next = dynamic_cast<const SetPolyStepPatternCommand*>(other);
    return next != nullptr && next->gesture_ == StepPatternGesture::Continuous &&
           next->devicePath_ == devicePath_ && next->description_ == description_;
}

void SetPolyStepPatternCommand::mergeWith(const UndoableCommand* other) {
    if (const auto* next = dynamic_cast<const SetPolyStepPatternCommand*>(other))
        pattern_ = next->pattern_;
}

juce::String SetPolyStepPatternCommand::captureState() {
    return capturedState(devicePath_);
}

void SetPolyStepPatternCommand::restoreState(const juce::String& state) {
    restoreCapturedState(devicePath_, state);
}

void SetPolyStepPatternCommand::performAction() {
    TrackManager::getInstance().updateDeviceAuthoredState(
        devicePath_, [this](device_state::Doc& doc) { step_pattern::writePoly(doc, pattern_); });
    ProjectManager::getInstance().markDirty();
}

// =============================================================================
// Edit helpers
// =============================================================================

bool editMonoStepPattern(const ChainNodePath& devicePath, const juce::String& description,
                         const std::function<void(step_pattern::MonoPattern&)>& edit,
                         StepPatternGesture gesture) {
    if (editableSequencer(devicePath, kMonoSequencerId) == nullptr)
        return false;

    const auto before = currentMonoPattern(devicePath);
    auto after = before;
    edit(after);
    if (after == before)
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<SetMonoStepPatternCommand>(devicePath, after, description, gesture));
    return true;
}

bool editPolyStepPattern(const ChainNodePath& devicePath, const juce::String& description,
                         const std::function<void(step_pattern::PolyPattern&)>& edit,
                         StepPatternGesture gesture) {
    if (editableSequencer(devicePath, kPolySequencerId) == nullptr)
        return false;

    const auto before = currentPolyPattern(devicePath);
    auto after = before;
    edit(after);
    if (after == before)
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<SetPolyStepPatternCommand>(devicePath, after, description, gesture));
    return true;
}

bool randomizeMonoStepPattern(const ChainNodePath& devicePath) {
    juce::Random random;
    return editMonoStepPattern(devicePath, "Randomize Pattern",
                               [&random](step_pattern::MonoPattern& pattern) {
                                   const int count = pattern.playingLength();
                                   for (int i = 0; i < count; ++i) {
                                       auto& step = pattern.steps[static_cast<size_t>(i)];
                                       step.noteNumber = 36 + random.nextInt(24);  // C2-B3
                                       step.octaveShift = 0;
                                       step.gate = random.nextFloat() < 0.7f;
                                       step.accent = random.nextFloat() < 0.2f;
                                       step.glide = random.nextFloat() < 0.15f;
                                       step.tie = false;
                                   }
                               });
}

bool randomizePolyStepPattern(const ChainNodePath& devicePath) {
    juce::Random random;
    return editPolyStepPattern(
        devicePath, "Randomize Pattern", [&random](step_pattern::PolyPattern& pattern) {
            const int count = pattern.playingLength();
            for (int i = 0; i < count; ++i) {
                auto& step = pattern.steps[static_cast<size_t>(i)];
                step = {};
                step.gate = random.nextFloat() < 0.7f;
                if (!step.gate)
                    continue;

                const int noteCount = 1 + random.nextInt(3);
                for (int n = 0; n < noteCount; ++n) {
                    const int note = 36 + random.nextInt(24);  // C2-B3
                    // The retired device deduped through addStepNote; a chord
                    // with the same note twice is one note played twice.
                    bool alreadyThere = false;
                    for (int existing = 0; existing < step.noteCount; ++existing)
                        alreadyThere |=
                            step.notes[static_cast<size_t>(existing)].noteNumber == note;
                    if (alreadyThere)
                        continue;
                    step.notes[static_cast<size_t>(step.noteCount)] = {.noteNumber = note};
                    ++step.noteCount;
                }
            }
        });
}

}  // namespace magda
