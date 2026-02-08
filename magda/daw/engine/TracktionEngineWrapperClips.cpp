#include "TracktionEngineWrapper.hpp"

#include <iostream>

namespace magda {

// =============================================================================
// Helper Functions for Beat/Time Conversion
// =============================================================================

// Helper: Convert beats to seconds using current tempo
static double beatsToSeconds(double beats, double tempo) {
    return beats * (60.0 / tempo);
}

// Helper: Convert seconds to beats using current tempo
static double secondsToBeats(double seconds, double tempo) {
    return seconds / (60.0 / tempo);
}

// =============================================================================
// ClipInterface Implementation
// =============================================================================

std::string TracktionEngineWrapper::addMidiClip(const std::string& track_id, double start_time,
                                                double length, const std::vector<MidiNote>& notes) {
    auto track = findTrackById(track_id);
    if (!track || !currentEdit_) {
        std::cerr << "addMidiClip: Track not found or no edit: " << track_id << std::endl;
        return "";
    }

    // Cast to AudioTrack (needed for insertMIDIClip)
    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        std::cerr << "addMidiClip: Track is not an AudioTrack: " << track_id << std::endl;
        return "";
    }

    // Create MIDI clip at specified position
    namespace te = tracktion;
    auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(start_time),
                                   te::TimePosition::fromSeconds(start_time + length));

    auto midiClipPtr = audioTrack->insertMIDIClip(timeRange, nullptr);
    if (!midiClipPtr) {
        std::cerr << "addMidiClip: Failed to create MIDI clip" << std::endl;
        return "";
    }

    // Get current tempo for beat-to-seconds conversion
    double tempo = getTempo();

    // Add notes to the clip's sequence
    auto& sequence = midiClipPtr->getSequence();
    for (const auto& note : notes) {
        // Convert beat-relative position to absolute beats
        // Note: Tracktion Engine uses beats, not seconds for MIDI notes
        te::BeatPosition startBeat = te::BeatPosition::fromBeats(note.startBeat);
        te::BeatDuration lengthBeats = te::BeatDuration::fromBeats(note.lengthBeats);

        DBG("Adding MIDI note: number=" << note.noteNumber << " start=" << note.startBeat
                                        << " beats, length=" << note.lengthBeats
                                        << " beats, velocity=" << note.velocity);

        sequence.addNote(note.noteNumber, startBeat, lengthBeats, note.velocity,
                         0,         // colour index
                         nullptr);  // undo manager
    }

    // Generate ID and store in clipMap_
    auto clipId = generateClipId();
    clipMap_[clipId] = midiClipPtr;

    std::cout << "Created MIDI clip: " << clipId << " on track " << track_id << " with "
              << notes.size() << " notes" << std::endl;
    return clipId;
}

std::string TracktionEngineWrapper::addAudioClip(const std::string& track_id, double start_time,
                                                 const std::string& audio_file_path) {
    // 1. Find Tracktion AudioTrack
    auto* track = findTrackById(track_id);
    if (!track || !currentEdit_) {
        DBG("TracktionEngineWrapper: Track not found or no edit: " << track_id);
        return "";
    }

    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        DBG("TracktionEngineWrapper: Track is not an AudioTrack");
        return "";
    }

    // 2. Validate audio file exists
    juce::File audioFile(audio_file_path);
    if (!audioFile.existsAsFile()) {
        DBG("TracktionEngineWrapper: Audio file not found: " << audio_file_path);
        return "";
    }

    // 3. Get audio file metadata to determine length
    namespace te = tracktion;
    te::AudioFile teAudioFile(audioTrack->edit.engine, audioFile);
    if (!teAudioFile.isValid()) {
        DBG("TracktionEngineWrapper: Invalid audio file: " << audio_file_path);
        return "";
    }

    double fileLengthSeconds = teAudioFile.getLength();

    // 4. Create time range for clip position
    auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(start_time),
                                   te::TimePosition::fromSeconds(start_time + fileLengthSeconds));

    // 5. Insert WaveAudioClip onto track
    auto clipPtr =
        insertWaveClip(*audioTrack,
                       audioFile.getFileNameWithoutExtension(),  // clip name
                       audioFile, te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

    if (!clipPtr) {
        DBG("TracktionEngineWrapper: Failed to create WaveAudioClip");
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Audio Clip Error",
            "Failed to create audio clip from:\n" + juce::String(audio_file_path));
        return "";
    }

    // 6. Store in clip map with generated ID
    auto clipId = generateClipId();
    clipMap_[clipId] = clipPtr.get();  // Store raw pointer

    DBG("TracktionEngineWrapper: Created audio clip '" << clipId << "' from " << audio_file_path);
    return clipId;
}

void TracktionEngineWrapper::deleteClip(const std::string& clip_id) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "deleteClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Remove from track
    clip->removeFromParent();

    // Remove from clipMap_
    clipMap_.erase(clip_id);

    std::cout << "Deleted clip: " << clip_id << std::endl;
}

void TracktionEngineWrapper::moveClip(const std::string& clip_id, double new_start_time) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "moveClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Get current position and create new position with updated start time
    auto currentPos = clip->getPosition();
    auto currentLength = currentPos.getLength().inSeconds();

    namespace te = tracktion;
    auto newTimeRange =
        te::TimeRange(te::TimePosition::fromSeconds(new_start_time),
                      te::TimePosition::fromSeconds(new_start_time + currentLength));

    clip->setStart(newTimeRange.getStart(), false, false);

    std::cout << "Moved clip: " << clip_id << " to " << new_start_time << std::endl;
}

void TracktionEngineWrapper::resizeClip(const std::string& clip_id, double new_length) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "resizeClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Get current position and create new position with updated length
    auto currentPos = clip->getPosition();
    auto currentStart = currentPos.getStart();

    namespace te = tracktion;
    auto newEnd = te::TimePosition::fromSeconds(currentStart.inSeconds() + new_length);

    clip->setEnd(newEnd, false);

    std::cout << "Resized clip: " << clip_id << " to " << new_length << std::endl;
}

double TracktionEngineWrapper::getClipStartTime(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getClipStartTime: Clip not found: " << clip_id << std::endl;
        return 0.0;
    }

    return clip->getPosition().getStart().inSeconds();
}

double TracktionEngineWrapper::getClipLength(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getClipLength: Clip not found: " << clip_id << std::endl;
        return 1.0;
    }

    return clip->getPosition().getLength().inSeconds();
}

void TracktionEngineWrapper::addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "addNoteToMidiClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "addNoteToMidiClip: Clip is not a MIDI clip: " << clip_id << std::endl;
        return;
    }

    // Convert beat-relative position to absolute beats
    namespace te = tracktion;
    te::BeatPosition startBeat = te::BeatPosition::fromBeats(note.startBeat);
    te::BeatDuration lengthBeats = te::BeatDuration::fromBeats(note.lengthBeats);

    // Add note to sequence
    auto& sequence = midiClip->getSequence();
    sequence.addNote(note.noteNumber, startBeat, lengthBeats, note.velocity,
                     0,         // colour index
                     nullptr);  // undo manager

    std::cout << "Added note " << note.noteNumber << " to MIDI clip: " << clip_id << std::endl;
}

void TracktionEngineWrapper::removeNotesFromMidiClip(const std::string& clip_id, double start_time,
                                                     double end_time) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "removeNotesFromMidiClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "removeNotesFromMidiClip: Clip is not a MIDI clip: " << clip_id << std::endl;
        return;
    }

    // Remove notes in the specified time range
    // Note: start_time and end_time are in beats (relative to clip)
    auto& sequence = midiClip->getSequence();
    juce::Array<te::MidiNote*> notesToRemove;

    for (auto* note : sequence.getNotes()) {
        double noteStart = note->getStartBeat().inBeats();
        if (noteStart >= start_time && noteStart < end_time) {
            notesToRemove.add(note);
        }
    }

    for (auto* note : notesToRemove) {
        sequence.removeNote(*note, nullptr);  // nullptr = no undo
    }

    std::cout << "Removed " << notesToRemove.size() << " notes from MIDI clip: " << clip_id
              << std::endl;
}

std::vector<MidiNote> TracktionEngineWrapper::getMidiClipNotes(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getMidiClipNotes: Clip not found: " << clip_id << std::endl;
        return {};
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "getMidiClipNotes: Clip is not a MIDI clip: " << clip_id << std::endl;
        return {};
    }

    // Read notes from sequence and convert to our format
    std::vector<MidiNote> notes;
    auto& sequence = midiClip->getSequence();

    for (auto* note : sequence.getNotes()) {
        MidiNote midiNote;
        midiNote.noteNumber = note->getNoteNumber();
        midiNote.velocity = note->getVelocity();

        // Note: Tracktion Engine stores MIDI notes in beats (relative to clip)
        midiNote.startBeat = note->getStartBeat().inBeats();
        midiNote.lengthBeats = note->getLengthBeats().inBeats();

        notes.push_back(midiNote);
    }

    return notes;
}

std::vector<std::string> TracktionEngineWrapper::getTrackClips(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (!track) {
        std::cerr << "getTrackClips: Track not found: " << track_id << std::endl;
        return {};
    }

    // Cast to AudioTrack (needed for getClips)
    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        std::cerr << "getTrackClips: Track is not an AudioTrack: " << track_id << std::endl;
        return {};
    }

    // Iterate clips and find their IDs in our clipMap_
    std::vector<std::string> clipIds;
    for (auto* clip : audioTrack->getClips()) {
        // Search clipMap_ for this clip pointer
        for (const auto& [id, clipPtr] : clipMap_) {
            if (clipPtr.get() == clip) {
                clipIds.push_back(id);
                break;
            }
        }
    }

    return clipIds;
}

bool TracktionEngineWrapper::clipExists(const std::string& clip_id) const {
    return clipMap_.find(clip_id) != clipMap_.end();
}

}  // namespace magda
