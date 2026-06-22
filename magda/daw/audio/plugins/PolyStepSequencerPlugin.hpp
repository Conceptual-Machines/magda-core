#pragma once

#include <array>

#include "plugins/MidiDevicePlugin.hpp"
#include "transport/StepClock.hpp"

namespace magda::daw::audio {

/**
 * @brief Polyphonic step sequencer MIDI device.
 *
 * Like StepSequencerPlugin but each step holds up to 8 notes (chords).
 * Steps have gate, tie, probability, and a step-level velocity; each note
 * can optionally override the step velocity.
 *
 * The ValueTree is the source of truth for the pattern: setters write STEP /
 * NOTE children through the undo manager and a listener mirrors the tree back
 * into the audio-thread step array, so undo/redo just works.
 *
 * Uses StepClock for tempo-synced step timing (transport or free-running).
 */
class PolyStepSequencerPlugin : public MidiDevicePlugin {
  public:
    PolyStepSequencerPlugin(const te::PluginCreationInfo& info);
    ~PolyStepSequencerPlugin() override;

    static const char* getPluginName() {
        return "Poly Sequencer";
    }
    static const char* xmlTypeName;

    static constexpr int MAX_STEPS = 32;
    static constexpr int MAX_NOTES_PER_STEP = 8;

    // --- Per-step data ---
    struct Note {
        int noteNumber = 60;  // MIDI note 0-127
        int velocity = 0;     // 0 = use step velocity, 1-127 = per-note override
    };

    struct Step {
        bool gate = true;          // true = active, false = rest
        bool tie = false;          // Extend previous step's notes (no retrigger)
        float probability = 1.0f;  // 0-1, evaluated once per step fire
        int velocity = 100;        // Step-level velocity (1-127)
        int noteCount = 0;
        std::array<Note, MAX_NOTES_PER_STEP> notes{};
    };

    // --- te::Plugin overrides ---
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "PSeq";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext& fc) override;

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;

    // --- Parameters (CachedValues for persistence) ---
    juce::CachedValue<int> numSteps;
    juce::CachedValue<int> rate;       // StepClock::Rate enum
    juce::CachedValue<int> direction;  // StepClock::Direction enum
    juce::CachedValue<float> swing;
    juce::CachedValue<float> gateLength;  // 0-1 normalized (0.1 = staccato, 1.0 = legato)
    juce::CachedValue<float> ramp;        // -1.0 to 1.0: bezier timing depth
    juce::CachedValue<float> skew;        // -1.0 to 1.0: bezier control point offset
    juce::CachedValue<int> rampCycles;    // 1-8: curve repetitions within one pattern cycle
    juce::CachedValue<bool> hardAngle;    // true = piecewise linear, false = smooth bezier
    juce::CachedValue<float> quantize;    // 0.0-1.0: adaptive quantize strength
    juce::CachedValue<int> quantizeSub;   // quantize grid subdivisions (16, 32, 48... 256)

    /** MIDI thru: pass incoming MIDI to downstream plugins. */
    juce::CachedValue<bool> midiThru;

    /** Pattern-view mode for the UI ("keys" or "drum"). Persisted with the
     *  edit and undo-safe; the engine does not act on it. */
    juce::CachedValue<juce::String> viewMode;

    // --- Automatable parameters (for macro/mod linking) ---
    te::AutomatableParameter::Ptr rateParam, directionParam;
    te::AutomatableParameter::Ptr swingParam, gateLengthParam;
    te::AutomatableParameter::Ptr rampParam, skewParam;

    // --- Pattern access (message thread) ---
    Step getStep(int index) const;
    bool stepHasNote(int index, int noteNumber) const;

    void setStepGate(int index, bool gate);
    void setStepTie(int index, bool tie);
    void setStepProbability(int index, float probability);
    void setStepVelocity(int index, int velocity);

    /** Add a note to a step (no-op if already present or step is full).
     *  velocityOverride 0 = use step velocity, 1-127 = per-note velocity. */
    void addStepNote(int index, int noteNumber, int velocityOverride = 0);
    void removeStepNote(int index, int noteNumber);
    void toggleStepNote(int index, int noteNumber);
    void setStepNoteVelocity(int index, int noteNumber, int velocityOverride);

    /** Reset a step to defaults (gate on, no notes). */
    void clearStep(int index);

    /** Shift all notes in all steps by semitones. If any note would leave 0-127, returns false and
     * leaves the pattern untouched. Single undo transaction. */
    bool transposePattern(int semitones);

    /** Remove all steps from the pattern in one undo transaction. */
    void clearPattern();

    /** Replace the pattern with a simple random one: ~70% gates, 1-3 notes per
     *  active step across two octaves (C2-B3). Single undo transaction. A
     *  musically-aware variant (scale/chord heuristics) can replace this later. */
    void randomizePattern();

    /** Current playback step index for UI highlight (-1 if not playing). */
    std::atomic<int> currentPlayStep_{-1};

    // --- Step recording: play notes to fill steps. A chord (notes held
    // together) lands on one step; the step advances when the chord releases.
    bool isStepRecording() const {
        return stepRecording_.load(std::memory_order_relaxed);
    }
    void setStepRecording(bool enabled);
    std::atomic<int> stepRecordPosition_{0};  // next step to write (UI highlight)

  private:
    // Step clock (handles timing, transport, swing, direction)
    StepClock stepClock_;

    // --- Step state (mirrored from ValueTree, read on audio thread) ---
    std::array<Step, MAX_STEPS> steps_{};

    // --- Step recording state ---
    std::atomic<bool> stepRecording_{false};
    int recordHeldCount_ = 0;  // notes currently held during recording (audio thread)

    // --- Audio-thread state ---
    std::array<int, MAX_NOTES_PER_STEP> soundingNotes_{};
    int soundingCount_ = 0;
    int noteOffCountdown_ = 0;       // Samples remaining until note-off (0 = no pending)
    int silentBlockCount_ = 0;       // Blocks with no step events (for safety note-off)
    bool needsAllNotesOff_ = false;  // Send all-notes-off on next applyToBuffer

    // Previous timing params - detect structural changes that require clock reset
    int prevRate_ = -1;
    int prevCycles_ = 1;
    bool prevHardAngle_ = false;

    // Inline xorshift32 PRNG for step probability (no allocation, audio-thread safe)
    juce::uint32 rngState_ = 0x9E3779B9u;
    float nextRandom01() {
        auto x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
    }

    /** Send note-offs for all sounding notes immediately (audio thread). */
    void killAllNotes(te::MidiMessageArray& midi, double time);

    // --- ValueTree pattern helpers (message thread) ---
    juce::ValueTree findStepTree(int index) const;
    juce::ValueTree getOrCreateStepTree(int index);
    static juce::ValueTree findNoteTree(const juce::ValueTree& stepTree, int noteNumber);

    /** Mirror the STEP/NOTE children of state into steps_. */
    void loadStepsFromState();

    // Sync CachedValue changes to AutomatableParams
    void syncParamFromProperty(const juce::Identifier& property);

    struct StateListener : public juce::ValueTree::Listener {
        PolyStepSequencerPlugin& owner;
        explicit StateListener(PolyStepSequencerPlugin& o) : owner(o) {}
        void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& p) override;
        void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override;
        void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override;
    };
    StateListener stateListener_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyStepSequencerPlugin)
};

}  // namespace magda::daw::audio
