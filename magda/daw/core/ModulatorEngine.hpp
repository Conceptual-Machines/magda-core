#pragma once

#include <juce_events/juce_events.h>

#include "ModInfo.hpp"

namespace magda {

/**
 * @brief Engine for calculating LFO modulation values
 *
 * Singleton that runs at 60 FPS to update all LFO phase and output values.
 * Updates phase based on rate, then generates waveform output.
 */
class ModulatorEngine : public juce::Timer {
  public:
    static ModulatorEngine& getInstance() {
        static ModulatorEngine instance;
        return instance;
    }

    ~ModulatorEngine() override {
        stopTimer();
    }

    // Delete copy/move
    ModulatorEngine(const ModulatorEngine&) = delete;
    ModulatorEngine& operator=(const ModulatorEngine&) = delete;
    ModulatorEngine(ModulatorEngine&&) = delete;
    ModulatorEngine& operator=(ModulatorEngine&&) = delete;

    /**
     * @brief Calculate LFO rate in Hz from tempo sync division
     * @param division The sync division (musical note value)
     * @param bpm Current tempo in beats per minute
     * @return Rate in Hz (cycles per second)
     */
    static float calculateSyncRateHz(SyncDivision division, double bpm) {
        // 1 beat = 60/BPM seconds
        // Quarter note = 1 beat, so quarter note freq = BPM/60 Hz
        double beatsPerSecond = bpm / 60.0;

        switch (division) {
            case SyncDivision::Whole:  // 4 beats
                return static_cast<float>(beatsPerSecond / 4.0);
            case SyncDivision::Half:  // 2 beats
                return static_cast<float>(beatsPerSecond / 2.0);
            case SyncDivision::Quarter:  // 1 beat
                return static_cast<float>(beatsPerSecond);
            case SyncDivision::Eighth:  // 1/2 beat
                return static_cast<float>(beatsPerSecond * 2.0);
            case SyncDivision::Sixteenth:  // 1/4 beat
                return static_cast<float>(beatsPerSecond * 4.0);
            case SyncDivision::ThirtySecond:  // 1/8 beat
                return static_cast<float>(beatsPerSecond * 8.0);
            case SyncDivision::DottedHalf:  // 3 beats
                return static_cast<float>(beatsPerSecond / 3.0);
            case SyncDivision::DottedQuarter:  // 1.5 beats
                return static_cast<float>(beatsPerSecond / 1.5);
            case SyncDivision::DottedEighth:  // 0.75 beats
                return static_cast<float>(beatsPerSecond / 0.75);
            case SyncDivision::TripletHalf:  // 2/3 of half = 4/3 beats
                return static_cast<float>(beatsPerSecond / (4.0 / 3.0));
            case SyncDivision::TripletQuarter:  // 2/3 beat
                return static_cast<float>(beatsPerSecond / (2.0 / 3.0));
            case SyncDivision::TripletEighth:  // 1/3 beat
                return static_cast<float>(beatsPerSecond / (1.0 / 3.0));
            default:
                return static_cast<float>(beatsPerSecond);  // Default to quarter note
        }
    }

    /**
     * @brief Generate waveform value for given phase
     * @param waveform The waveform type
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float generateWaveform(LFOWaveform waveform, float phase) {
        constexpr float PI = 3.14159265359f;

        switch (waveform) {
            case LFOWaveform::Sine:
                return (std::sin(2.0f * PI * phase) + 1.0f) * 0.5f;

            case LFOWaveform::Triangle:
                return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;

            case LFOWaveform::Square:
                return phase < 0.5f ? 1.0f : 0.0f;

            case LFOWaveform::Saw:
                return phase;

            case LFOWaveform::ReverseSaw:
                return 1.0f - phase;

            case LFOWaveform::Custom:
                // For Custom, default to triangle - use generateCurvePreset for full support
                return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;

            default:
                return 0.5f;
        }
    }

    /**
     * @brief Generate curve preset value for given phase
     * @param preset The curve preset type
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float generateCurvePreset(CurvePreset preset, float phase) {
        constexpr float PI = 3.14159265359f;

        switch (preset) {
            case CurvePreset::Triangle:
                return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;

            case CurvePreset::Sine:
                return (std::sin(2.0f * PI * phase) + 1.0f) * 0.5f;

            case CurvePreset::RampUp:
                return phase;

            case CurvePreset::RampDown:
                return 1.0f - phase;

            case CurvePreset::SCurve: {
                // Smooth S-curve using smoothstep
                float t = phase;
                return t * t * (3.0f - 2.0f * t);
            }

            case CurvePreset::Exponential:
                // Exponential rise
                return (std::exp(phase * 3.0f) - 1.0f) / (std::exp(3.0f) - 1.0f);

            case CurvePreset::Logarithmic:
                // Logarithmic rise
                return std::log(1.0f + phase * (std::exp(1.0f) - 1.0f));

            case CurvePreset::Custom:
            default:
                // Custom uses curve points - default to linear ramp
                return phase;
        }
    }

    /**
     * @brief Generate waveform value for a mod (handles Custom waveforms with curve presets)
     * @param mod The modulator info
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float generateWaveformForMod(const ModInfo& mod, float phase) {
        if (mod.waveform == LFOWaveform::Custom) {
            return generateCurvePreset(mod.curvePreset, phase);
        }
        return generateWaveform(mod.waveform, phase);
    }

  private:
    ModulatorEngine() = default;

    void timerCallback() override {
        // Calculate delta time (approximately 1/60 second at 60 FPS)
        double deltaTime = getTimerInterval() / 1000.0;

        // Update all mods through TrackManager
        updateAllMods(deltaTime);
    }

    void updateAllMods(double deltaTime);
};

}  // namespace magda
