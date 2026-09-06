#pragma once

#include <juce_events/juce_events.h>

#include <atomic>

#include "../../music/ChordEngine.hpp"
#include "../../music/ChordSuggestionEngine.hpp"
#include "../../music/KeyModeHistogram.hpp"
#include "../../music/ScaleDetector.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief The chord detector: it reads the chain's MIDI and writes none (#2314).
 *
 * A listener rather than a MIDI effect. On the audio thread it records the
 * notes going past; on the message thread a timer runs detection and updates
 * the state the UI reads. Nothing it does reaches the signal, which is what
 * `DeviceType::Analysis` and a `properties()` with no `producesMidi` say
 * (#2427).
 *
 * Dual UI surface:
 * - Inline/window: real-time chord display, key indicator, suggestion grid
 * - Editor panel chord row: timeline overlay + drag-and-drop from suggestions
 *
 * The timer is the device's own, which two other MagdaDevices already do
 * (FaustPlugin): only `process()` belongs to the audio thread, and the
 * analysis behind it is message-thread work on a plain object the host owns.
 */
class MidiChordEnginePlugin : public MagdaDevice, private juce::Timer {
  public:
    MidiChordEnginePlugin();
    ~MidiChordEnginePlugin() override;

    static const char* getPluginName() {
        return "Chord Engine";
    }
    static const char* xmlTypeName;

    // --- MagdaDevice ---

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Chord",
            .takesMidiInput = true,
            .producesMidi = false,
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    /**
     * @brief Whether the chord track's audition is off, pushed by the host.
     *
     * A value rather than a lookup (#2314): the old plugin polled a
     * `DeviceTrackContext` for the chord track's mute, and an engine-built
     * device has no session to poll. The host owns the model and pushes this
     * when it changes.
     *
     * Read together with the block's own `isPlaying`, so live authoring while
     * the transport is stopped still detects. What it skips is the recording,
     * not the signal: the chain's MIDI belongs to the host and a muted chord
     * track is already silent.
     */
    void setChordTrackMuted(bool muted) {
        chordTrackMuted_.store(muted, std::memory_order_relaxed);
    }

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

    /** Prime the engine's context from an authored progression (e.g. the chord
        track) so key / suggestions / scales appear without live play. */
    void seedFromChords(const std::vector<magda::music::Chord>& chords);

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

    // The chord track's audition (its mute), pushed by the host and read on the
    // audio thread beside the block's own isPlaying.
    std::atomic<bool> chordTrackMuted_{false};

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

    // Recompute cached key / suggestions / scales from current engine state.
    // Caller must hold stateMutex_. Returns true if the key/mode changed.
    bool recomputeCachesLocked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiChordEnginePlugin)
};

}  // namespace magda::daw::audio
