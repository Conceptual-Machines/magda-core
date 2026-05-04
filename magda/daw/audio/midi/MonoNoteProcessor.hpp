#pragma once

#include <algorithm>
#include <vector>

namespace magda::daw::audio {

/**
 * @brief Pure mono-note state machine for step sequencer MIDI output.
 *
 * Extracted from StepSequencerPlugin::applyToBuffer to enable unit testing.
 * Handles note-on/off generation, gate length countdown, tie/glide logic,
 * and stuck-note safety — without any JUCE/TE dependencies.
 *
 * Usage: call processBlock() each audio buffer with step events from StepClock.
 * Collect the emitted MidiOutput events and forward to your MIDI buffer.
 */
class MonoNoteProcessor {
  public:
    // --- Step data (mirrors StepSequencerPlugin::Step) ---
    struct Step {
        int noteNumber = 60;
        int octaveShift = 0;
        bool gate = true;
        bool accent = false;
        bool glide = false;
        bool tie = false;
    };

    // --- Step event (mirrors StepClock::StepEvent, no TE deps) ---
    struct StepEvent {
        int stepIndex;
        double timeInBlock;  // seconds from block start
    };

    // --- Output MIDI event ---
    struct MidiOutput {
        enum Type { NoteOn, NoteOff };
        Type type;
        int noteNumber;
        int velocity;
        double time;  // seconds from block start
    };

    // --- Block parameters ---
    struct BlockParams {
        float gateLength = 0.5f;         // 0.05-1.0
        int accentVelocity = 110;        // 1-127
        int normalVelocity = 80;         // 1-127
        int stepDurationSamples = 5512;  // based on BPM and rate
        int bufferSamples = 512;
        double sampleRate = 44100.0;
    };

    void reset() {
        lastPlayedNote_ = -1;
        noteOffCountdown_ = 0;
        silentBlockCount_ = 0;
    }

    /**
     * Process one audio block. Returns emitted MIDI events.
     *
     * @param events     Step events from StepClock (sorted by timeInBlock)
     * @param eventCount Number of step events
     * @param steps      Step data array
     * @param stepCount  Number of active steps in the pattern
     * @param params     Block parameters (gate length, velocities, timing)
     * @param output     Vector to append MIDI output events to
     */
    void processBlock(const StepEvent* events, int eventCount, const Step* steps, int stepCount,
                      const BlockParams& params, std::vector<MidiOutput>& output) {
        double blockDurationSecs = static_cast<double>(params.bufferSamples) / params.sampleRate;

        // --- Emit pending note-off (sample countdown) ---
        if (noteOffCountdown_ > 0 && lastPlayedNote_ >= 0) {
            if (noteOffCountdown_ <= params.bufferSamples) {
                double countdownTime = static_cast<double>(noteOffCountdown_) / params.sampleRate;
                bool stepFiresFirst = (eventCount > 0 && events[0].timeInBlock <= countdownTime);

                bool nextStepIsTie = (eventCount > 0 && steps[events[0].stepIndex].tie &&
                                      steps[events[0].stepIndex].gate);

                if (!stepFiresFirst && !nextStepIsTie) {
                    killNote(countdownTime, output);
                } else {
                    noteOffCountdown_ = 0;
                }
            } else {
                if (eventCount > 0) {
                    noteOffCountdown_ = 0;
                } else {
                    noteOffCountdown_ -= params.bufferSamples;
                }
            }
        }

        // --- Process each step event ---
        for (int i = 0; i < eventCount; ++i) {
            const auto& evt = events[i];
            const auto& step = steps[evt.stepIndex];

            // Rest step
            if (!step.gate) {
                killNote(evt.timeInBlock, output);
                continue;
            }

            // Tie step — hold current note
            if (step.tie && lastPlayedNote_ >= 0) {
                noteOffCountdown_ = 0;
                continue;
            }

            int noteNum = resolveNote(step);
            int vel = step.accent ? params.accentVelocity : params.normalVelocity;

            // Note-off for previous note (before note-on)
            if (lastPlayedNote_ >= 0) {
                double offTime = std::max(0.0, evt.timeInBlock - 0.0001);
                output.push_back({MidiOutput::NoteOff, lastPlayedNote_, 0, offTime});
                noteOffCountdown_ = 0;
            }

            // Note-on
            output.push_back({MidiOutput::NoteOn, noteNum, vel, evt.timeInBlock});
            lastPlayedNote_ = noteNum;

            // Schedule note-off countdown
            int nextIdx = (evt.stepIndex + 1) % stepCount;
            bool nextIsTie = steps[nextIdx].tie;
            double gateRatio =
                (step.glide || nextIsTie) ? 1.0 : static_cast<double>(params.gateLength);
            int noteOnSample = static_cast<int>(evt.timeInBlock * params.sampleRate);
            int gateSamples = static_cast<int>(params.stepDurationSamples * gateRatio);
            noteOffCountdown_ = gateSamples - (params.bufferSamples - noteOnSample);
            if (noteOffCountdown_ <= 0) {
                double offTimeInBlock =
                    evt.timeInBlock + static_cast<double>(gateSamples) / params.sampleRate;
                offTimeInBlock = std::min(offTimeInBlock, blockDurationSecs);
                killNote(offTimeInBlock, output);
            }
        }

        // --- Stuck-note safety ---
        if (eventCount > 0) {
            silentBlockCount_ = 0;
        } else if (lastPlayedNote_ >= 0 && noteOffCountdown_ <= 0) {
            ++silentBlockCount_;
            if (silentBlockCount_ > 4) {
                killNote(0.0, output);
                silentBlockCount_ = 0;
            }
        }
    }

    // --- State accessors (for tests and external queries) ---
    int lastPlayedNote() const {
        return lastPlayedNote_;
    }
    int noteOffCountdown() const {
        return noteOffCountdown_;
    }
    int silentBlockCount() const {
        return silentBlockCount_;
    }

  private:
    int lastPlayedNote_ = -1;
    int noteOffCountdown_ = 0;
    int silentBlockCount_ = 0;

    void killNote(double time, std::vector<MidiOutput>& output) {
        if (lastPlayedNote_ >= 0) {
            output.push_back({MidiOutput::NoteOff, lastPlayedNote_, 0, time});
            lastPlayedNote_ = -1;
            noteOffCountdown_ = 0;
        }
    }

    static int resolveNote(const Step& step) {
        return step.noteNumber + step.octaveShift * 12;
    }
};

}  // namespace magda::daw::audio
