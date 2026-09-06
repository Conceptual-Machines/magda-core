#include "plugins/MidiChordEnginePlugin.hpp"

namespace magda::daw::audio {

const char* MidiChordEnginePlugin::xmlTypeName = "midichordengine";

MidiChordEnginePlugin::MidiChordEnginePlugin() {
    for (auto& n : heldNotes_)
        n.store(0, std::memory_order_relaxed);

    // Started here rather than in prepare(), because the UI reads this device
    // whether or not a graph was ever built for it: a chord track sitting in a
    // stopped project still shows what its panel detected.
    startTimerHz(30);
}

MidiChordEnginePlugin::~MidiChordEnginePlugin() {
    stopTimer();
}

void MidiChordEnginePlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;
}

void MidiChordEnginePlugin::release() {}

void MidiChordEnginePlugin::reset() {
    heldNoteCount_.store(0, std::memory_order_relaxed);
    noteFifo_.reset();
}

// =============================================================================
// Audio thread
// =============================================================================

void MidiChordEnginePlugin::process(DeviceProcessContext& context) {
    // Nothing is written anywhere: the chain's MIDI is the host's to pass on
    // and this device declares no output of its own (#2427). What happens here
    // is recording, and every early return below skips only that.
    if (context.midiIn == nullptr)
        return;

    // Audition off while the transport rolls. The old plugin cleared the
    // buffer here so nothing downstream heard it; a listener has no buffer to
    // clear and needs none, because the audition toggle is the chord track's
    // own mute and a muted track is already silent (#2314). What is left to do
    // is keep playback out of the detection, which is this.
    if (chordTrackMuted_.load(std::memory_order_relaxed) && context.isPlaying) {
        heldNoteCount_.store(0, std::memory_order_relaxed);
        return;
    }

    // Skip recording during preview playback.
    if (detectionSuppressed_.load(std::memory_order_relaxed))
        return;

    const double blockTimeSeconds = context.timelineStartSeconds;

    for (int index = 0; index < context.midiIn->size(); ++index) {
        const auto& msg = context.midiIn->message(index);
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

bool MidiChordEnginePlugin::recomputeCachesLocked() {
    // Caller holds stateMutex_. Refresh key / suggestions / scales from the
    // current suggestionEngine_ + keyHistogram_ + chordHistory_ state.
    auto newKeyMode = keyHistogram_.inferKeyMode();
    const bool keyChanged = newKeyMode != cachedKeyMode_;
    cachedKeyMode_ = newKeyMode;

    auto recentChords = suggestionEngine_.getRecentChords();
    if (cachedKeyMode_.has_value()) {
        cachedSuggestions_ = suggestionEngine_.generateSuggestions(
            recentChords, suggestionParams_, cachedKeyMode_->first, cachedKeyMode_->second);
    } else {
        cachedSuggestions_ = suggestionEngine_.generateSuggestions(recentChords, suggestionParams_);
    }

    std::set<int> pitchClasses;
    for (const auto& chord : chordHistory_) {
        for (const auto& note : chord.notes)
            pitchClasses.insert(note.noteNumber % 12);
    }
    if (pitchClasses.size() >= 3) {
        int preferredRoot = -1;
        if (cachedKeyMode_.has_value()) {
            auto keyRoot = cachedKeyMode_->first;
            static const juce::String noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                     "F#", "G",  "G#", "A",  "A#", "B"};
            static const juce::String flatNames[] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                                     "Gb", "G",  "Ab", "A",  "Bb", "B"};
            for (int i = 0; i < 12; ++i)
                if (keyRoot == noteNames[i] || keyRoot == flatNames[i]) {
                    preferredRoot = i;
                    break;
                }
        }
        auto scored = magda::music::detectScalesFromPitchClasses(
            pitchClasses, magda::music::getAllScalesWithChordsCached(), preferredRoot);
        cachedScales_.clear();
        int limit = std::min(static_cast<int>(scored.size()), 8);
        for (int i = 0; i < limit; ++i)
            cachedScales_.push_back(scored[static_cast<size_t>(i)].first);
    }

    return keyChanged;
}

void MidiChordEnginePlugin::seedFromChords(const std::vector<magda::music::Chord>& chords) {
    // Prime the engine's context from an authored progression (e.g. the chord
    // track) so key / suggestions / scales appear without live play. Message
    // thread; serialised with runDetection via stateMutex_.
    std::scoped_lock lock(stateMutex_);

    suggestionEngine_.reset();
    keyHistogram_.reset();
    chordHistory_.clear();

    double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    for (const auto& c : chords) {
        if (c.notes.empty())
            continue;
        chordHistory_.push_back(c);
        if (chordHistory_.size() > MAX_CHORD_HISTORY)
            chordHistory_.erase(chordHistory_.begin());
        suggestionEngine_.processNewChord(c, t, suggestionParams_);
        keyHistogram_.updateWithChord(c, t);
        t += 1.0;
    }

    recomputeCachesLocked();
    listeners_.call(&Listener::chordChanged, this);
    listeners_.call(&Listener::keyModeChanged, this);
    listeners_.call(&Listener::suggestionsChanged, this);
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
        std::scoped_lock lock(stateMutex_);
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

    std::scoped_lock lock(stateMutex_);

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

        const bool keyChanged = recomputeCachesLocked();

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
    std::scoped_lock lock(stateMutex_);
    return currentChord_.getDisplayName();
}

juce::String MidiChordEnginePlugin::getLastDetectedChordName() const {
    std::scoped_lock lock(stateMutex_);
    return lastDetectedChordName_;
}

magda::music::Chord MidiChordEnginePlugin::getCurrentChord() const {
    std::scoped_lock lock(stateMutex_);
    return currentChord_;
}

std::vector<magda::music::Chord> MidiChordEnginePlugin::getRecentChords() const {
    std::scoped_lock lock(stateMutex_);
    return chordHistory_;
}

std::optional<std::pair<juce::String, juce::String>> MidiChordEnginePlugin::getDetectedKeyMode()
    const {
    std::scoped_lock lock(stateMutex_);
    return cachedKeyMode_;
}

std::vector<magda::music::ChordEngine::SuggestionItem> MidiChordEnginePlugin::getSuggestions()
    const {
    std::scoped_lock lock(stateMutex_);
    return cachedSuggestions_;
}

std::vector<magda::music::ScaleWithChords> MidiChordEnginePlugin::getDetectedScales(
    int maxResults) const {
    std::scoped_lock lock(stateMutex_);
    if (static_cast<int>(cachedScales_.size()) <= maxResults)
        return cachedScales_;
    return {cachedScales_.begin(), cachedScales_.begin() + maxResults};
}

void MidiChordEnginePlugin::clearHistory() {
    std::scoped_lock lock(stateMutex_);
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
    std::scoped_lock lock(stateMutex_);
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

}  // namespace magda::daw::audio
