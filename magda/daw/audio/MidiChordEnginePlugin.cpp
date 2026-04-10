#include "MidiChordEnginePlugin.hpp"

namespace magda::daw::audio {

const char* MidiChordEnginePlugin::xmlTypeName = "midichordengine";

MidiChordEnginePlugin::MidiChordEnginePlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    for (auto& n : heldNotes_)
        n.store(0, std::memory_order_relaxed);
    // Start timer here as fallback — initialise() may not be called
    // if the graph isn't rebuilt after plugin insertion.
    startTimerHz(30);
}

MidiChordEnginePlugin::~MidiChordEnginePlugin() {
    stopTimer();
    notifyListenersOfDeletion();
}

void MidiChordEnginePlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    startTimerHz(30);  // ~33ms polling interval
}

void MidiChordEnginePlugin::deinitialise() {
    stopTimer();
}

void MidiChordEnginePlugin::reset() {
    heldNoteCount_.store(0, std::memory_order_relaxed);
    noteFifo_.reset();
}

// =============================================================================
// Audio thread
// =============================================================================

void MidiChordEnginePlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI

    if (!fc.bufferForMidiMessages)
        return;

    // Skip recording during preview playback or when plugin is bypassed/disabled
    if (detectionSuppressed_.load(std::memory_order_relaxed) || !isEnabled())
        return;

    const double blockTimeSeconds = static_cast<double>(fc.bufferStartSample) / sampleRate_;

    for (const auto& msg : *fc.bufferForMidiMessages) {
        if (msg.isNoteOn()) {
            // Add to held notes
            int count = heldNoteCount_.load(std::memory_order_relaxed);
            if (count < MAX_HELD_NOTES) {
                heldNotes_[static_cast<size_t>(count)].store(msg.getNoteNumber(),
                                                             std::memory_order_relaxed);
                heldNoteCount_.store(count + 1, std::memory_order_release);
            }
            // Push to FIFO for message-thread processing
            int start1, size1, start2, size2;
            noteFifo_.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0) {
                noteBuffer_[static_cast<size_t>(start1)] = {msg.getNoteNumber(), true,
                                                            blockTimeSeconds};
                noteFifo_.finishedWrite(1);
            }
        } else if (msg.isNoteOff()) {
            // Remove from held notes
            int count = heldNoteCount_.load(std::memory_order_relaxed);
            int noteNum = msg.getNoteNumber();
            for (int i = 0; i < count; ++i) {
                if (heldNotes_[static_cast<size_t>(i)].load(std::memory_order_relaxed) == noteNum) {
                    // Swap with last
                    if (i < count - 1) {
                        heldNotes_[static_cast<size_t>(i)].store(
                            heldNotes_[static_cast<size_t>(count - 1)].load(
                                std::memory_order_relaxed),
                            std::memory_order_relaxed);
                    }
                    heldNoteCount_.store(count - 1, std::memory_order_release);
                    break;
                }
            }

            // Push to FIFO
            int start1, size1, start2, size2;
            noteFifo_.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0) {
                noteBuffer_[static_cast<size_t>(start1)] = {msg.getNoteNumber(), false,
                                                            blockTimeSeconds};
                noteFifo_.finishedWrite(1);
            }
        } else if (msg.isAllNotesOff()) {
            heldNoteCount_.store(0, std::memory_order_relaxed);
        }
    }
}

// =============================================================================
// Message thread
// =============================================================================

void MidiChordEnginePlugin::timerCallback() {
    if (!isEnabled()) {
        // Flush stale FIFO events by consuming them (safe — we're the reader).
        // Don't call reset() here as it races with the audio thread writer.
        int start1, size1, start2, size2;
        noteFifo_.prepareToRead(noteFifo_.getNumReady(), start1, size1, start2, size2);
        noteFifo_.finishedRead(size1 + size2);
        heldNoteCount_.store(0, std::memory_order_relaxed);
        return;
    }

    processNoteEvents();

    // Debounce: if held note count changed since last detection, wait
    // for notes to settle before re-detecting (avoids partial chord snapshots)
    int count = heldNoteCount_.load(std::memory_order_relaxed);
    if (count != lastSnapshotNoteCount_) {
        lastSnapshotNoteCount_ = count;
        debounceCountdown_ = 2;  // skip 2 timer cycles (~66ms at 30Hz)
        return;
    }
    if (debounceCountdown_ > 0) {
        --debounceCountdown_;
        return;
    }

    runDetection();
}

void MidiChordEnginePlugin::processNoteEvents() {
    int start1, size1, start2, size2;
    noteFifo_.prepareToRead(noteFifo_.getNumReady(), start1, size1, start2, size2);

    auto processRange = [this](int start, int count) {
        for (int i = 0; i < count; ++i) {
            const auto& evt = noteBuffer_[static_cast<size_t>(start + i)];
            keyHistogram_.updateWithMidiNote(evt.noteNumber, evt.timeSeconds);
        }
    };

    if (size1 > 0)
        processRange(start1, size1);
    if (size2 > 0)
        processRange(start2, size2);

    noteFifo_.finishedRead(size1 + size2);
}

void MidiChordEnginePlugin::runDetection() {
    // Snapshot held notes from atomic array
    int count = heldNoteCount_.load(std::memory_order_acquire);
    std::vector<magda::music::ChordNote> heldNotes;
    heldNotes.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        int noteNum = heldNotes_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        heldNotes.push_back({noteNum, 100});
    }

    if (heldNotes.empty()) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentChord_.name.isNotEmpty()) {
            currentChord_ = {};
            // Don't notify — UI uses lastDetectedChord_ for display
        }
        return;
    }

    auto& engine = magda::music::ChordEngine::getInstance();
    auto detected = engine.smartDetect(heldNotes);
    magda::music::ChordEngine::finalizeChord(detected);

    if (detected.name.isEmpty() || detected.name == "none" || detected.name == "unknown")
        return;

    std::lock_guard<std::mutex> lock(stateMutex_);

    bool chordChanged = detected.getDisplayName() != currentChord_.getDisplayName();
    currentChord_ = detected;
    lastDetectedChordName_ = detected.getDisplayName();

    if (chordChanged) {
        DBG("MidiChordEngine: " << detected.getDisplayName()
                                << " exact=" << (detected.exactMatch ? "yes" : "no")
                                << " missing=" << (int)detected.missingIntervals.size()
                                << " extra=" << (int)detected.extraPitchClasses.size());
        // Add to history
        chordHistory_.push_back(detected);
        if (chordHistory_.size() > MAX_CHORD_HISTORY)
            chordHistory_.erase(chordHistory_.begin());

        // Update suggestion engine context
        double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        suggestionEngine_.processNewChord(detected, nowSeconds, suggestionParams_);
        keyHistogram_.updateWithChord(detected, nowSeconds);

        // Update cached key/mode
        auto newKeyMode = keyHistogram_.inferKeyMode();
        bool keyChanged = newKeyMode != cachedKeyMode_;
        cachedKeyMode_ = newKeyMode;

        // Update cached suggestions
        auto recentChords = suggestionEngine_.getRecentChords();
        if (cachedKeyMode_.has_value()) {
            cachedSuggestions_ = suggestionEngine_.generateSuggestions(
                recentChords, suggestionParams_, cachedKeyMode_->first, cachedKeyMode_->second);
        } else {
            cachedSuggestions_ =
                suggestionEngine_.generateSuggestions(recentChords, suggestionParams_);
        }

        // Update cached scales from chord history pitch classes
        {
            std::set<int> pitchClasses;
            for (const auto& chord : chordHistory_) {
                for (const auto& note : chord.notes)
                    pitchClasses.insert(note.noteNumber % 12);
            }
            if (pitchClasses.size() >= 3) {
                // Pass detected key root so scales rooted on it rank higher
                int preferredRoot = -1;
                if (cachedKeyMode_.has_value()) {
                    auto keyRoot = cachedKeyMode_->first;
                    static const juce::String noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                             "F#", "G",  "G#", "A",  "A#", "B"};
                    for (int i = 0; i < 12; ++i) {
                        if (keyRoot == noteNames[i]) {
                            preferredRoot = i;
                            break;
                        }
                    }
                    // Handle flats
                    if (preferredRoot < 0) {
                        static const juce::String flatNames[] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                                                 "Gb", "G",  "Ab", "A",  "Bb", "B"};
                        for (int i = 0; i < 12; ++i) {
                            if (keyRoot == flatNames[i]) {
                                preferredRoot = i;
                                break;
                            }
                        }
                    }
                }
                auto scored = magda::music::detectScalesFromPitchClasses(
                    pitchClasses, magda::music::getAllScalesWithChordsCached(), preferredRoot);
                cachedScales_.clear();
                int limit = std::min(static_cast<int>(scored.size()), 8);
                for (int i = 0; i < limit; ++i)
                    cachedScales_.push_back(scored[static_cast<size_t>(i)].first);
            }
        }

        listeners_.call(&Listener::chordChanged, this);
        if (keyChanged)
            listeners_.call(&Listener::keyModeChanged, this);
        listeners_.call(&Listener::suggestionsChanged, this);
    }
}

// =============================================================================
// Public accessors (UI thread)
// =============================================================================

juce::String MidiChordEnginePlugin::getCurrentChordName() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentChord_.getDisplayName();
}

juce::String MidiChordEnginePlugin::getLastDetectedChordName() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastDetectedChordName_;
}

magda::music::Chord MidiChordEnginePlugin::getCurrentChord() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentChord_;
}

std::vector<magda::music::Chord> MidiChordEnginePlugin::getRecentChords() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return chordHistory_;
}

std::optional<std::pair<juce::String, juce::String>> MidiChordEnginePlugin::getDetectedKeyMode()
    const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return cachedKeyMode_;
}

std::vector<magda::music::ChordEngine::SuggestionItem> MidiChordEnginePlugin::getSuggestions()
    const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return cachedSuggestions_;
}

std::vector<magda::music::ScaleWithChords> MidiChordEnginePlugin::getDetectedScales(
    int maxResults) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (static_cast<int>(cachedScales_.size()) <= maxResults)
        return cachedScales_;
    return {cachedScales_.begin(), cachedScales_.begin() + maxResults};
}

void MidiChordEnginePlugin::clearHistory() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    chordHistory_.clear();
    currentChord_ = {};
    lastDetectedChordName_.clear();
    cachedKeyMode_ = std::nullopt;
    cachedSuggestions_.clear();
    cachedScales_.clear();
    suggestionEngine_.reset();
    keyHistogram_.reset();
    listeners_.call(&Listener::chordChanged, this);
    listeners_.call(&Listener::keyModeChanged, this);
    listeners_.call(&Listener::suggestionsChanged, this);
}

void MidiChordEnginePlugin::refreshSuggestions() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto recentChords = suggestionEngine_.getRecentChords();
    if (recentChords.empty() && !cachedKeyMode_.has_value()) {
        // No input yet — don't show default C major suggestions
        if (!cachedSuggestions_.empty()) {
            cachedSuggestions_.clear();
            listeners_.call(&Listener::suggestionsChanged, this);
        }
        return;
    }
    if (cachedKeyMode_.has_value()) {
        cachedSuggestions_ = suggestionEngine_.generateSuggestions(
            recentChords, suggestionParams_, cachedKeyMode_->first, cachedKeyMode_->second);
    } else {
        cachedSuggestions_ = suggestionEngine_.generateSuggestions(recentChords, suggestionParams_);
    }
    listeners_.call(&Listener::suggestionsChanged, this);
}

void MidiChordEnginePlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    // No persistent parameters yet — suggestion params could be saved here later
}

}  // namespace magda::daw::audio
