#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>

#include "../music/ChordEngine.hpp"
#include "../music/ChordSuggestionEngine.hpp"
#include "../music/KeyModeHistogram.hpp"
#include "../music/ScaleDetector.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief MIDI analysis plugin that provides real-time chord detection and suggestions.
 *
 * Transparent passthrough — does not modify audio or MIDI. Dropped onto a track to
 * enable chord analysis for that track. On the audio thread, note-on/off events are
 * collected; on the message thread, a timer periodically runs detection and updates
 * exposed state that the UI reads.
 *
 * Dual UI surface:
 * - Inline/window: real-time chord display, key indicator, suggestion grid
 * - Editor panel chord row: timeline overlay + drag-and-drop from suggestions
 */
class MidiChordEnginePlugin : public te::Plugin, private juce::Timer {
  public:
    MidiChordEnginePlugin(const te::PluginCreationInfo& info);
    ~MidiChordEnginePlugin() override;

    static const char* getPluginName() {
        return "Chord Engine";
    }
    static const char* xmlTypeName;

    // --- te::Plugin overrides ---
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Chord";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // --- Chord detection state (message-thread readable) ---

    /** Current detected chord display name (e.g. "Cmaj7", "Am"). Empty if no chord held. */
    juce::String getCurrentChordName() const;

    /** Last detected chord name — persists after note release for UI display. */
    juce::String getLastDetectedChordName() const;

    /** Current detected chord object. */
    magda::music::Chord getCurrentChord() const;

    /** Recent chord history (most recent last). */
    std::vector<magda::music::Chord> getRecentChords() const;

    /** Detected key and mode (e.g. {"C", "major"}). nullopt if not enough data. */
    std::optional<std::pair<juce::String, juce::String>> getDetectedKeyMode() const;

    /** Generate chord suggestions based on current context. */
    std::vector<magda::music::ChordEngine::SuggestionItem> getSuggestions() const;

    /** Detected scales sorted by match score (top N). Each entry has the scale and its chords. */
    std::vector<magda::music::ScaleWithChords> getDetectedScales(int maxResults = 5) const;

    /** Suggestion parameters — UI can tweak these. */
    magda::music::ChordEngine::SuggestionParams& getSuggestionParams() {
        return suggestionParams_;
    }
    const magda::music::ChordEngine::SuggestionParams& getSuggestionParams() const {
        return suggestionParams_;
    }

    // --- AI chord progression state (persisted across UI rebuilds) ---
    struct AIProgression {
        juce::String name;
        juce::String description;
        std::vector<magda::music::Chord> chords;
    };

    std::vector<AIProgression>& getAIProgressions() {
        return aiProgressions_;
    }
    const std::vector<AIProgression>& getAIProgressions() const {
        return aiProgressions_;
    }

    /** Number of currently held notes (audio-thread written, safe to read on message thread). */
    int getHeldNoteCount() const {
        return heldNoteCount_.load(std::memory_order_relaxed);
    }
    /** Note number for held note at index (0 ≤ index < getHeldNoteCount()). */
    int getHeldNote(int index) const {
        return heldNotes_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }

    /** Suppress detection (e.g. during chord preview playback). */
    void setDetectionSuppressed(bool suppressed) {
        detectionSuppressed_.store(suppressed, std::memory_order_relaxed);
    }

    /** Clear chord history and reset detection state. */
    void clearHistory();

    /** Re-generate suggestions from current context + params (call after param changes). */
    void refreshSuggestions();

    // --- Listener for UI updates ---
    struct Listener {
        virtual ~Listener() = default;
        virtual void chordChanged(MidiChordEnginePlugin*) {}
        virtual void keyModeChanged(MidiChordEnginePlugin*) {}
        virtual void suggestionsChanged(MidiChordEnginePlugin*) {}
    };

    void addListener(Listener* l) {
        listeners_.add(l);
    }
    void removeListener(Listener* l) {
        listeners_.remove(l);
    }

  private:
    // --- Audio-thread state (lock-free) ---

    // SPSC ring buffer for note events from audio thread → message thread
    static constexpr int NOTE_FIFO_SIZE = 256;
    struct NoteEvent {
        int noteNumber = 0;
        bool isNoteOn = false;
        double timeSeconds = 0.0;
    };
    juce::AbstractFifo noteFifo_{NOTE_FIFO_SIZE};
    std::array<NoteEvent, NOTE_FIFO_SIZE> noteBuffer_{};

    // Currently held notes on the audio thread (for chord snapshot)
    static constexpr int MAX_HELD_NOTES = 32;
    std::atomic<int> heldNoteCount_{0};
    std::array<std::atomic<int>, MAX_HELD_NOTES> heldNotes_{};

    // Debounce: wait for held-note count to stabilise before detecting
    int lastSnapshotNoteCount_ = 0;  // message thread only
    int debounceCountdown_ = 0;      // message thread only

    // Suppress detection during preview playback
    std::atomic<bool> detectionSuppressed_{false};

    // --- Message-thread state ---
    magda::music::ChordSuggestionEngine suggestionEngine_;
    magda::music::KeyModeHistogram keyHistogram_;
    magda::music::ChordEngine::SuggestionParams suggestionParams_;

    magda::music::Chord currentChord_;
    juce::String lastDetectedChordName_;  // persists after note release
    std::vector<magda::music::Chord> chordHistory_;
    static constexpr size_t MAX_CHORD_HISTORY = 64;

    std::optional<std::pair<juce::String, juce::String>> cachedKeyMode_;
    std::vector<magda::music::ChordEngine::SuggestionItem> cachedSuggestions_;
    std::vector<magda::music::ScaleWithChords> cachedScales_;

    mutable std::mutex stateMutex_;  // protects message-thread state reads from UI

    std::vector<AIProgression> aiProgressions_;

    double sampleRate_ = 44100.0;

    juce::ListenerList<Listener> listeners_;

    // --- Timer (message thread) ---
    void timerCallback() override;

    // Process pending note events from the FIFO
    void processNoteEvents();

    // Run chord detection on current held notes
    void runDetection();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiChordEnginePlugin)
};

}  // namespace magda::daw::audio
