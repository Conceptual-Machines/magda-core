#pragma once

#include <array>
#include <atomic>

#include "core/ParameterUtils.hpp"
#include "plugins/MidiMagdaDevice.hpp"
#include "sequencer/MonoStepSequencer.hpp"

namespace magda::daw::audio {

/**
 * @brief 303-style monophonic step sequencer MIDI device.
 *
 * Plays a looping pattern of notes with pitch, accent, glide, gate and octave
 * shift, in front of a synth on a track's chain. The playing itself is
 * sequencer::MonoStepSequencer; this class is the device shell around it.
 *
 * A MagdaDevice since #2299: one sequencer hosted by whichever engine is
 * running it. The slot ids, order and display ranges are the ones the retired
 * host-native plugin registered.
 *
 * The pattern is NOT owned here (#2313). A step pattern is authored state, the
 * same category as MIDI notes, so the model owns it and undo lives there; the
 * device is handed the pattern with the rest of its state and plays what it is
 * given. Everything that edits a pattern - the faceplate, the agents - goes
 * through the model's undoable commands (core/StepPatternCommands.hpp).
 */
class StepSequencerPlugin : public MidiMagdaDevice {
  public:
    StepSequencerPlugin();
    ~StepSequencerPlugin() override;

    static const char* getPluginName() {
        return "Step Sequencer";
    }
    static const char* xmlTypeName;

    // --- Per-step data (the sequencing core's, so a pattern is one type) ---
    using Step = sequencer::MonoStep;
    static constexpr int MAX_STEPS = sequencer::kMaxSteps;

    /// FROZEN slot order - saved links address these by index.
    enum ParamIndex {
        kRate = 0,
        kDirection,
        kSwing,
        kGateLength,
        kAccentVelocity,
        kNormalVelocity,
        kRamp,  // -1..1: bezier timing depth
        kSkew,  // -1..1: control-point position offset from centre
        kNumParams
    };

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Seq",
            .takesMidiInput = true,
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

    // ValueTree property ids for the non-slot settings below, and for the
    // pattern's length. The spellings are the retired host-native plugin's, so
    // saved projects keep them. Public because the faceplate's settings edits
    // travel as model document patches in this vocabulary (#2317), not as
    // writes to the atomics. How many steps play has no atomic: it is part of
    // the pattern, and pattern() is where it is read.
    struct SettingIDs {
        static const juce::Identifier numSteps;
        static const juce::Identifier midiThru;
        static const juce::Identifier rampCycles;
        static const juce::Identifier hardAngle;
        static const juce::Identifier quantize;
        static const juce::Identifier quantizeSub;
    };

    // --- Non-parameter settings (persisted device state) ---
    // Atomics: restoreState() writes on the message thread while process() reads.
    std::atomic<bool> midiThru{true};    // pass incoming MIDI to downstream devices
    std::atomic<int> rampCycles{1};      // 1-8: curve repetitions within one pattern cycle
    std::atomic<bool> hardAngle{false};  // piecewise linear instead of smooth bezier
    std::atomic<float> quantize{0.0f};   // 0-1: adaptive quantize strength
    std::atomic<int> quantizeSub{16};    // quantize grid subdivisions

    /// The pattern the model last published. Message thread only - the
    /// faceplate reads it to draw, and edits it through the model.
    sequencer::MonoPattern pattern() const;

    /** Current playback step index for UI highlight (-1 if not playing). */
    std::atomic<int> currentPlayStep_{-1};

    // --- Step recording -----------------------------------------------------
    // Incoming notes fill steps in order. The device cannot write the model, so
    // it records what it heard and the faceplate drains that into one undoable
    // pattern edit per note.

    /** One note the step recorder captured. */
    struct RecordedStep {
        int stepIndex = 0;
        int noteNumber = 60;
    };

    bool isStepRecording() const {
        return stepRecording_.load(std::memory_order_relaxed);
    }
    void setStepRecording(bool enabled);

    /** Take the oldest recorded note, if any. Message thread. */
    bool popRecordedStep(RecordedStep& step);

    /** Current step record position (for UI). */
    std::atomic<int> stepRecordPosition_{0};

  private:
    // The sequencing engine: clock, voice and the tie/glide/gate rules.
    sequencer::MonoStepSequencer sequencer_;

    /// The published pattern, double-buffered: restoreState() fills the slot
    /// the audio thread is not reading and then flips the index, so a publish
    /// never rewrites the pattern mid-block.
    std::array<sequencer::MonoPattern, 2> patternSlots_{};
    std::atomic<int> livePattern_{0};

    // --- Audio-thread state ---
    bool needsAllNotesOff_ = false;

    /// Incoming MIDI held back while the sequencer writes its own notes, then
    /// appended so it still reaches the instrument downstream. A member so the
    /// block allocates nothing.
    static constexpr int kMaxThruMessages = 64;
    std::array<juce::MidiMessage, kMaxThruMessages> thruMessages_{};
    std::array<std::uint32_t, kMaxThruMessages> thruSources_{};

    // --- Step recording state ---
    std::atomic<bool> stepRecording_{false};
    static constexpr int kRecordQueueSize = 32;
    std::array<RecordedStep, kRecordQueueSize> recordQueue_{};
    // Unsigned so the ring's indices wrap defined rather than overflowing.
    std::atomic<unsigned> recordWriteIndex_{0};
    std::atomic<unsigned> recordReadIndex_{0};

    /// The parameter's display-domain value, converted through the cached
    /// domain rather than a freshly built ParameterInfo: this runs per block on
    /// the audio thread, and a ParameterInfo carries strings that allocate.
    float displayValue(int index) const;
    int displayIndex(int index) const;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerPlugin)
};

}  // namespace magda::daw::audio
