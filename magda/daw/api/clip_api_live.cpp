#include "clip_api_live.hpp"

#include "../audio/AudioThumbnailManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/MidiNoteCommands.hpp"
#include "../core/UndoManager.hpp"

namespace magda {

namespace {

QuantizeMode toCoreQuantizeMode(MidiNoteQuantizeMode mode) {
    switch (mode) {
        case MidiNoteQuantizeMode::StartOnly:
            return QuantizeMode::StartOnly;
        case MidiNoteQuantizeMode::LengthOnly:
            return QuantizeMode::LengthOnly;
        case MidiNoteQuantizeMode::StartAndLength:
            return QuantizeMode::StartAndLength;
    }
    return QuantizeMode::StartAndLength;
}

bool isMidiClip(ClipId clipId) {
    auto* clip = ClipManager::getInstance().getClip(clipId);
    return clip != nullptr && clip->isMidi();
}

}  // namespace

ClipInfo* ClipApiLive::getClip(ClipId clipId) {
    return ClipManager::getInstance().getClip(clipId);
}

std::vector<ClipInfo> ClipApiLive::getArrangementClips() const {
    return ClipManager::getInstance().getArrangementClips();
}

std::vector<ClipId> ClipApiLive::getClipsOnTrack(TrackId trackId) const {
    return ClipManager::getInstance().getClipsOnTrack(trackId);
}

ClipId ClipApiLive::createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                        ClipView view) {
    return ClipManager::getInstance().createMidiClipBeats(trackId, startBeats, lengthBeats, view);
}

void ClipApiLive::deleteClip(ClipId clipId) {
    ClipManager::getInstance().deleteClip(clipId);
}

void ClipApiLive::setClipName(ClipId clipId, const juce::String& name) {
    ClipManager::getInstance().setClipName(clipId, name);
}

void ClipApiLive::setGrooveTemplate(ClipId clipId, const juce::String& templateName) {
    ClipManager::getInstance().setGrooveTemplate(clipId, templateName);
}

bool ClipApiLive::addMidiNote(ClipId clipId, double startBeat, int noteNumber, double lengthBeats,
                              int velocity) {
    if (!isMidiClip(clipId) || lengthBeats <= 0.0)
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<AddMidiNoteCommand>(clipId, startBeat, noteNumber, lengthBeats, velocity));
    return true;
}

bool ClipApiLive::quantizeMidiNotes(ClipId clipId, const std::vector<size_t>& noteIndices,
                                    double gridResolution, MidiNoteQuantizeMode mode) {
    if (!isMidiClip(clipId) || noteIndices.empty() || gridResolution <= 0.0)
        return false;

    UndoManager::getInstance().executeCommand(std::make_unique<QuantizeMidiNotesCommand>(
        clipId, noteIndices, gridResolution, toCoreQuantizeMode(mode)));
    return true;
}

bool ClipApiLive::sliceMidiNotes(ClipId clipId, const std::vector<size_t>& noteIndices,
                                 int subdivisions) {
    if (!isMidiClip(clipId) || noteIndices.empty() || subdivisions < 2)
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<SliceMidiNotesCommand>(clipId, noteIndices, subdivisions));
    return true;
}

bool ClipApiLive::transposeMidiClip(ClipId clipId, int semitones) {
    if (!isMidiClip(clipId))
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<TransposeMidiClipCommand>(clipId, semitones));
    return true;
}

const juce::Array<double>* ClipApiLive::getCachedTransients(const juce::String& filePath) const {
    return AudioThumbnailManager::getInstance().getCachedTransients(filePath);
}

}  // namespace magda
