#include "slot/StepSequencerClipExport.hpp"

#include <algorithm>
#include <vector>

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/sequencer/StepClock.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/StepPatternState.hpp"
#include "core/TrackManager.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/common/InternalFileDrag.hpp"

namespace magda::daw::ui {

namespace {

namespace seq = daw::audio::sequencer;

/// What an export needs from the model: the pattern, and the settings that
/// decide where its notes land and how long they last.
template <typename PatternT> struct SequencerExportState {
    PatternT pattern;
    seq::StepClock::Rate rate = seq::StepClock::Rate::Sixteenth;
    float gateLength = 0.8f;
    int accentVelocity = 120;
    int normalVelocity = 90;
    bool valid = false;
};

/// A device's parameter in its display domain, or @p fallback when the model
/// has no entry for that slot.
///
/// By paramIndex, never by array position - the model's parameter list is in
/// document order and need not be complete, so a positional read exports the
/// clip at another slot's value (#2335).
float parameterOr(const magda::DeviceInfo& device, int index, float fallback) {
    const auto* parameter = device.findParameterByIndex(index);
    return parameter != nullptr ? parameter->currentValue : fallback;
}

SequencerExportState<step_pattern::MonoPattern> monoStateOf(const magda::ChainNodePath& path) {
    SequencerExportState<step_pattern::MonoPattern> state;
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr ||
        !device->pluginId.equalsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName))
        return state;

    using Seq = daw::audio::StepSequencerPlugin;
    state.pattern = step_pattern::monoPatternOf(device->pluginState);
    state.rate =
        static_cast<seq::StepClock::Rate>(juce::roundToInt(parameterOr(*device, Seq::kRate, 7.0f)));
    state.gateLength = parameterOr(*device, Seq::kGateLength, 0.8f);
    state.accentVelocity = juce::roundToInt(parameterOr(*device, Seq::kAccentVelocity, 120.0f));
    state.normalVelocity = juce::roundToInt(parameterOr(*device, Seq::kNormalVelocity, 90.0f));
    state.valid = true;
    return state;
}

SequencerExportState<step_pattern::PolyPattern> polyStateOf(const magda::ChainNodePath& path) {
    SequencerExportState<step_pattern::PolyPattern> state;
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr ||
        !device->pluginId.equalsIgnoreCase(daw::audio::PolyStepSequencerPlugin::xmlTypeName))
        return state;

    using Seq = daw::audio::PolyStepSequencerPlugin;
    state.pattern = step_pattern::polyPatternOf(device->pluginState);
    state.rate =
        static_cast<seq::StepClock::Rate>(juce::roundToInt(parameterOr(*device, Seq::kRate, 7.0f)));
    state.gateLength = parameterOr(*device, Seq::kGateLength, 0.8f);
    state.valid = true;
    return state;
}

std::vector<magda::MidiNote> collectStepSequencerNotes(const magda::ChainNodePath& path) {
    const auto state = monoStateOf(path);
    if (!state.valid)
        return {};

    const double stepBeats = seq::StepClock::rateToBeats(state.rate);
    const int count = state.pattern.playingLength();

    std::vector<magda::MidiNote> notes;
    for (int i = 0; i < count; ++i) {
        const auto& step = state.pattern.steps[static_cast<size_t>(i)];
        if (!step.gate)
            continue;

        magda::MidiNote note;
        note.noteNumber = std::clamp(step.noteNumber + (step.octaveShift * 12), 0, 127);
        note.velocity = step.accent ? state.accentVelocity : state.normalVelocity;
        note.startBeat = i * stepBeats;
        note.lengthBeats = stepBeats * state.gateLength;
        notes.push_back(note);
    }
    return notes;
}

double stepSequencerPatternLengthBeats(const magda::ChainNodePath& path) {
    const auto state = monoStateOf(path);
    if (!state.valid)
        return 0.0;
    return static_cast<double>(state.pattern.playingLength()) *
           seq::StepClock::rateToBeats(state.rate);
}

std::vector<magda::MidiNote> collectPolyStepSequencerNotes(const magda::ChainNodePath& path) {
    const auto state = polyStateOf(path);
    if (!state.valid)
        return {};

    const double stepBeats = seq::StepClock::rateToBeats(state.rate);
    const int count = state.pattern.playingLength();
    const auto& steps = state.pattern.steps;

    std::vector<magda::MidiNote> notes;

    // A run of tied steps is one held note, so a step's length is its own plus
    // every tie that follows it.
    std::vector<int> spanSteps(static_cast<size_t>(count), 1);
    for (int i = 0; i < count; ++i) {
        const auto& step = steps[static_cast<size_t>(i)];
        if (!step.gate || step.tie)
            continue;
        int span = 1;
        for (int j = i + 1; j < count; ++j) {
            const auto& next = steps[static_cast<size_t>(j)];
            if (next.gate && next.tie)
                ++span;
            else
                break;
        }
        spanSteps[static_cast<size_t>(i)] = span;
    }

    for (int i = 0; i < count; ++i) {
        const auto& step = steps[static_cast<size_t>(i)];
        if (!step.gate || step.tie || step.noteCount == 0)
            continue;

        const double startBeat = i * stepBeats;
        const int span = spanSteps[static_cast<size_t>(i)];
        const double lengthBeats =
            ((span - 1) * stepBeats) + (stepBeats * static_cast<double>(state.gateLength));

        for (int n = 0; n < step.noteCount; ++n) {
            const auto& stepNote = step.notes[static_cast<size_t>(n)];
            magda::MidiNote note;
            note.noteNumber = juce::jlimit(0, 127, stepNote.noteNumber);
            note.velocity =
                juce::jlimit(1, 127, stepNote.velocity > 0 ? stepNote.velocity : step.velocity);
            note.startBeat = startBeat;
            note.lengthBeats = lengthBeats;
            notes.push_back(note);
        }
    }
    return notes;
}

double polyStepSequencerPatternLengthBeats(const magda::ChainNodePath& path) {
    const auto state = polyStateOf(path);
    if (!state.valid)
        return 0.0;
    return static_cast<double>(state.pattern.playingLength()) *
           seq::StepClock::rateToBeats(state.rate);
}

double currentProjectTempoOrDefault() {
    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo <= 0.0)
        tempo = 120.0;
    return tempo;
}

/// Shared tail of both drag handlers: write the file the gesture drags out.
bool startPatternDrag(const juce::File& tempFile, juce::Component* exportButton,
                      juce::Component* dragOwner) {
    if (tempFile.existsAsFile()) {
        exportButton->setAlpha(0.4f);
        magda::dnd::startFilesDrag(dragOwner, juce::StringArray{tempFile.getFullPathName()});
        exportButton->setAlpha(1.0f);
    }
    return true;
}

bool isDragGesture(juce::Component* exportButton, juce::Component* dragOwner,
                   const juce::MouseEvent& event, int dragThresholdPx) {
    return exportButton != nullptr && dragOwner != nullptr &&
           event.originalComponent == exportButton &&
           event.getDistanceFromDragStart() > dragThresholdPx;
}

}  // namespace

void copyStepSequencerPatternToClipboard(const magda::ChainNodePath& devicePath) {
    auto notes = collectStepSequencerNotes(devicePath);
    if (!notes.empty()) {
        auto& clipManager = ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.setMidiClipClipboard(std::move(notes), "Step Sequencer Pattern",
                                         stepSequencerPatternLengthBeats(devicePath));
    }
}

juce::File writeStepSequencerPatternToTempMidiFile(const magda::ChainNodePath& devicePath) {
    auto notes = collectStepSequencerNotes(devicePath);
    if (notes.empty())
        return {};

    return daw::MidiFileWriter::writeToTempFile(notes, currentProjectTempoOrDefault(),
                                                "seq-pattern");
}

bool handleStepSequencerPatternExternalDrag(const magda::ChainNodePath& devicePath,
                                            juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event, int dragThresholdPx) {
    if (!isDragGesture(exportButton, dragOwner, event, dragThresholdPx))
        return false;
    return startPatternDrag(writeStepSequencerPatternToTempMidiFile(devicePath), exportButton,
                            dragOwner);
}

void copyPolyStepSequencerPatternToClipboard(const magda::ChainNodePath& devicePath) {
    auto notes = collectPolyStepSequencerNotes(devicePath);
    if (!notes.empty()) {
        auto& clipManager = ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.setMidiClipClipboard(std::move(notes), "Poly Sequencer Pattern",
                                         polyStepSequencerPatternLengthBeats(devicePath));
    }
}

juce::File writePolyStepSequencerPatternToTempMidiFile(const magda::ChainNodePath& devicePath) {
    auto notes = collectPolyStepSequencerNotes(devicePath);
    if (notes.empty())
        return {};
    return daw::MidiFileWriter::writeToTempFile(notes, currentProjectTempoOrDefault(),
                                                "polyseq-pattern");
}

bool handlePolyStepSequencerPatternExternalDrag(const magda::ChainNodePath& devicePath,
                                                juce::Component* exportButton,
                                                juce::Component* dragOwner,
                                                const juce::MouseEvent& event,
                                                int dragThresholdPx) {
    if (!isDragGesture(exportButton, dragOwner, event, dragThresholdPx))
        return false;
    return startPatternDrag(writePolyStepSequencerPatternToTempMidiFile(devicePath), exportButton,
                            dragOwner);
}

}  // namespace magda::daw::ui
