#include "slot/StepSequencerClipExport.hpp"

#include <algorithm>
#include <vector>

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/transport/StepClock.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiFileWriter.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

namespace {

std::vector<magda::MidiNote> collectStepSequencerNotes(daw::audio::StepSequencerPlugin& plugin) {
    const int count =
        juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS, plugin.numSteps.get());
    const auto rateEnum = static_cast<daw::audio::StepClock::Rate>(plugin.rate.get());
    const double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
    const float gate = plugin.gateLength.get();
    const int accentVelocity = plugin.accentVelocity.get();
    const int normalVelocity = plugin.normalVelocity.get();

    std::vector<magda::MidiNote> notes;
    for (int i = 0; i < count; ++i) {
        const auto step = plugin.getStep(i);
        if (!step.gate)
            continue;

        magda::MidiNote note;
        note.noteNumber = std::clamp(step.noteNumber + step.octaveShift * 12, 0, 127);
        note.velocity = step.accent ? accentVelocity : normalVelocity;
        note.startBeat = i * stepBeats;
        note.lengthBeats = stepBeats * gate;
        notes.push_back(note);
    }
    return notes;
}

double stepSequencerPatternLengthBeats(daw::audio::StepSequencerPlugin& plugin) {
    const int count =
        juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS, plugin.numSteps.get());
    const auto rateEnum = static_cast<daw::audio::StepClock::Rate>(plugin.rate.get());
    return static_cast<double>(count) * daw::audio::StepClock::rateToBeats(rateEnum);
}

std::vector<magda::MidiNote> collectPolyStepSequencerNotes(
    daw::audio::PolyStepSequencerPlugin& plugin) {
    const int count =
        juce::jlimit(1, daw::audio::PolyStepSequencerPlugin::MAX_STEPS, plugin.numSteps.get());
    const auto rateEnum = static_cast<daw::audio::StepClock::Rate>(plugin.rate.get());
    const double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
    const float gate = plugin.gateLength.get();

    std::vector<magda::MidiNote> notes;

    std::vector<int> spanSteps(static_cast<size_t>(count), 1);
    for (int i = 0; i < count; ++i) {
        const auto step = plugin.getStep(i);
        if (!step.gate || step.tie)
            continue;
        int span = 1;
        for (int j = i + 1; j < count; ++j) {
            const auto next = plugin.getStep(j);
            if (next.gate && next.tie)
                ++span;
            else
                break;
        }
        spanSteps[static_cast<size_t>(i)] = span;
    }

    for (int i = 0; i < count; ++i) {
        const auto step = plugin.getStep(i);
        if (!step.gate || step.tie)
            continue;
        if (step.noteCount == 0)
            continue;

        double startBeat = i * stepBeats;
        int span = spanSteps[static_cast<size_t>(i)];
        double lengthBeats = (span - 1) * stepBeats + stepBeats * static_cast<double>(gate);

        for (int n = 0; n < step.noteCount; ++n) {
            const auto& sn = step.notes[static_cast<size_t>(n)];
            magda::MidiNote note;
            note.noteNumber = juce::jlimit(0, 127, sn.noteNumber);
            note.velocity = juce::jlimit(1, 127, sn.velocity > 0 ? sn.velocity : step.velocity);
            note.startBeat = startBeat;
            note.lengthBeats = lengthBeats;
            notes.push_back(note);
        }
    }
    return notes;
}

double polyStepSequencerPatternLengthBeats(daw::audio::PolyStepSequencerPlugin& plugin) {
    const int count =
        juce::jlimit(1, daw::audio::PolyStepSequencerPlugin::MAX_STEPS, plugin.numSteps.get());
    const auto rateEnum = static_cast<daw::audio::StepClock::Rate>(plugin.rate.get());
    return static_cast<double>(count) * daw::audio::StepClock::rateToBeats(rateEnum);
}

double currentProjectTempoOrDefault() {
    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo <= 0.0)
        tempo = 120.0;
    return tempo;
}

}  // namespace

void copyStepSequencerPatternToClipboard(daw::audio::StepSequencerPlugin& plugin) {
    auto notes = collectStepSequencerNotes(plugin);
    if (!notes.empty()) {
        auto& clipManager = ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.setMidiClipClipboard(std::move(notes), "Step Sequencer Pattern",
                                         stepSequencerPatternLengthBeats(plugin));
    }
}

juce::File writeStepSequencerPatternToTempMidiFile(daw::audio::StepSequencerPlugin& plugin) {
    auto notes = collectStepSequencerNotes(plugin);
    if (notes.empty())
        return {};

    return daw::MidiFileWriter::writeToTempFile(notes, currentProjectTempoOrDefault(),
                                                "seq-pattern");
}

bool handleStepSequencerPatternExternalDrag(daw::audio::StepSequencerPlugin* plugin,
                                            juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event, int dragThresholdPx) {
    if (plugin == nullptr || exportButton == nullptr || dragOwner == nullptr ||
        event.originalComponent != exportButton ||
        event.getDistanceFromDragStart() <= dragThresholdPx) {
        return false;
    }

    auto tempFile = writeStepSequencerPatternToTempMidiFile(*plugin);
    if (tempFile.existsAsFile()) {
        exportButton->setAlpha(0.4f);
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            juce::StringArray{tempFile.getFullPathName()}, false, dragOwner);
        exportButton->setAlpha(1.0f);
    }

    return true;
}

void copyPolyStepSequencerPatternToClipboard(daw::audio::PolyStepSequencerPlugin& plugin) {
    auto notes = collectPolyStepSequencerNotes(plugin);
    if (!notes.empty()) {
        auto& clipManager = ClipManager::getInstance();
        clipManager.setNoteClipboard({});
        clipManager.setMidiClipClipboard(std::move(notes), "Poly Sequencer Pattern",
                                         polyStepSequencerPatternLengthBeats(plugin));
    }
}

juce::File writePolyStepSequencerPatternToTempMidiFile(
    daw::audio::PolyStepSequencerPlugin& plugin) {
    auto notes = collectPolyStepSequencerNotes(plugin);
    if (notes.empty())
        return {};
    return daw::MidiFileWriter::writeToTempFile(notes, currentProjectTempoOrDefault(),
                                                "polyseq-pattern");
}

bool handlePolyStepSequencerPatternExternalDrag(daw::audio::PolyStepSequencerPlugin* plugin,
                                                juce::Component* exportButton,
                                                juce::Component* dragOwner,
                                                const juce::MouseEvent& event,
                                                int dragThresholdPx) {
    if (plugin == nullptr || exportButton == nullptr || dragOwner == nullptr ||
        event.originalComponent != exportButton ||
        event.getDistanceFromDragStart() <= dragThresholdPx) {
        return false;
    }
    auto tempFile = writePolyStepSequencerPatternToTempMidiFile(*plugin);
    if (tempFile.existsAsFile()) {
        exportButton->setAlpha(0.4f);
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            juce::StringArray{tempFile.getFullPathName()}, false, dragOwner);
        exportButton->setAlpha(1.0f);
    }
    return true;
}

}  // namespace magda::daw::ui
