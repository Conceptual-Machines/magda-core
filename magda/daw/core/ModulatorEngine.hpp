#pragma once

#include <juce_events/juce_events.h>

#include <chrono>
#include <functional>
#include <memory>

#include "ModInfo.hpp"

namespace magda {

/**
 * @brief Engine for calculating LFO modulation values
 *
 * Singleton that runs at 60 FPS to update all LFO phase and output values.
 * Updates phase based on rate, then generates waveform output.
 */
class ModulatorEngine {
  private:
    // Internal timer class that calls back to ModulatorEngine
    class UpdateTimer : public juce::Timer {
      public:
        explicit UpdateTimer(ModulatorEngine& engine) : engine_(engine) {}

        void timerCallback() override {
            engine_.onTimerCallback();
        }

      private:
        ModulatorEngine& engine_;
    };

  public:
    static ModulatorEngine& getInstance() {
        static ModulatorEngine instance;
        return instance;
    }

    ~ModulatorEngine() {
        shutdown();
    }

    /**
     * @brief Install a hook that runs after the local visual sim updates
     * each tick, used to overlay the audio thread's authoritative LFO
     * phase + value (from te::LFOModifier::getCurrentPhase / getCurrentValue)
     * onto MAGDA's ModInfo. Without it, the visual sim and audio LFO
     * free-run independently and drift apart even at matching rates.
     * AudioBridge installs this on construction.
     */
    void setPostUpdateHook(std::function<void()> hook) {
        postUpdateHook_ = std::move(hook);
    }

    // Delete copy/move
    ModulatorEngine(const ModulatorEngine&) = delete;
    ModulatorEngine& operator=(const ModulatorEngine&) = delete;
    ModulatorEngine(ModulatorEngine&&) = delete;
    ModulatorEngine& operator=(ModulatorEngine&&) = delete;

    /**
     * @brief Start the modulation update timer at specified interval
     */
    void startTimer(int intervalMs) {
        if (!timer_) {
            timer_ = std::make_unique<UpdateTimer>(*this);
        }
        timer_->startTimer(intervalMs);
    }

    /**
     * @brief Stop the modulation update timer
     */
    void stopTimer() {
        if (timer_) {
            timer_->stopTimer();
        }
    }

    /**
     * @brief Shutdown and destroy timer resources
     * Call this during app shutdown, before JUCE cleanup begins
     */
    void shutdown() {
        timer_.reset();  // Destroy timer early, not during static cleanup
    }

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
            case SyncDivision::SixteenBars:  // 64 beats
                return static_cast<float>(beatsPerSecond / 64.0);
            case SyncDivision::EightBars:  // 32 beats
                return static_cast<float>(beatsPerSecond / 32.0);
            case SyncDivision::FourBars:  // 16 beats
                return static_cast<float>(beatsPerSecond / 16.0);
            case SyncDivision::TwoBars:  // 8 beats
                return static_cast<float>(beatsPerSecond / 8.0);
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
            case SyncDivision::DottedSixteenth:  // 3/8 beat
                return static_cast<float>(beatsPerSecond / (3.0 / 8.0));
            case SyncDivision::TripletSixteenth:  // 1/6 beat
                return static_cast<float>(beatsPerSecond / (1.0 / 6.0));
            case SyncDivision::DottedThirtySecond:  // 3/16 beat
                return static_cast<float>(beatsPerSecond / (3.0 / 16.0));
            case SyncDivision::TripletThirtySecond:  // 1/12 beat
                return static_cast<float>(beatsPerSecond / (1.0 / 12.0));
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
     * @brief Evaluate curve points at given phase using tension-based interpolation
     * @param points The curve points sorted by phase
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float evaluateCurvePoints(const std::vector<CurvePointData>& points, float phase) {
        if (points.empty()) {
            return 0.5f;  // Default to center
        }
        if (points.size() == 1) {
            return points[0].value;
        }

        // Find bracketing points (curve loops, so we may wrap around)
        const CurvePointData* p1 = nullptr;
        const CurvePointData* p2 = nullptr;

        for (size_t i = 0; i < points.size(); ++i) {
            if (points[i].phase > phase) {
                if (i == 0) {
                    // Before first point - wrap from last point
                    p1 = &points.back();
                    p2 = &points[0];
                } else {
                    p1 = &points[i - 1];
                    p2 = &points[i];
                }
                break;
            }
        }

        // If we didn't find a bracket, we're after the last point - wrap to first
        if (!p1) {
            p1 = &points.back();
            p2 = &points.front();
        }

        // Calculate interpolation t value
        float phaseSpan;
        float localPhase;
        if (p2->phase < p1->phase) {
            // Wrapping case
            phaseSpan = (1.0f - p1->phase) + p2->phase;
            if (phase >= p1->phase) {
                localPhase = phase - p1->phase;
            } else {
                localPhase = (1.0f - p1->phase) + phase;
            }
        } else {
            phaseSpan = p2->phase - p1->phase;
            localPhase = phase - p1->phase;
        }

        float t = (phaseSpan > 0.0001f) ? (localPhase / phaseSpan) : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        // Apply tension-based interpolation (same formula as CurveEditorBase)
        float tension = p1->tension;
        if (std::abs(tension) < 0.001f) {
            // Linear interpolation
            return p1->value + t * (p2->value - p1->value);
        } else {
            float curvedT;
            if (tension > 0) {
                // Ease in - slow start, fast end
                curvedT = std::pow(t, 1.0f + tension * 2.0f);
            } else {
                // Ease out - fast start, slow end
                curvedT = 1.0f - std::pow(1.0f - t, 1.0f - tension * 2.0f);
            }
            return p1->value + curvedT * (p2->value - p1->value);
        }
    }

    /**
     * @brief Generate waveform value for a mod (handles Custom waveforms with curve points)
     * @param mod The modulator info
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    /**
     * @brief Return the end-of-cycle value for oneshot mode.
     *
     * Returns the last custom point's value directly (avoiding wrap-around
     * interpolation), or the preset value at phase 1.0 for non-custom waveforms.
     */
    static float generateOneShotEndValue(const ModInfo& mod) {
        if (mod.waveform == LFOWaveform::Custom) {
            if (!mod.curvePoints.empty())
                return mod.curvePoints.back().value;
            return generateCurvePreset(mod.curvePreset, 1.0f);
        }
        return generateWaveform(mod.waveform, 1.0f);
    }

    static float generateWaveformForMod(const ModInfo& mod, float phase) {
        if (mod.waveform == LFOWaveform::Custom) {
            if (!mod.curvePoints.empty()) {
                return evaluateCurvePoints(mod.curvePoints, phase);
            }
            // Fallback to preset if no custom points
            return generateCurvePreset(mod.curvePreset, phase);
        }
        return generateWaveform(mod.waveform, phase);
    }

  private:
    ModulatorEngine() = default;

    // Timer callback handler
    void onTimerCallback() {
        // Use the actual elapsed wall time since the previous tick rather than
        // the timer's nominal interval. juce::Timer fires late under message-
        // thread load and never coalesces missed ticks, so a fixed deltaTime
        // makes the visual sim drift slower than the audio LFO. Clamp to a
        // sensible upper bound so a long stall (debugger pause, modal dialog)
        // doesn't cause the LFO phase to jump wildly on the next tick.
        const auto now = std::chrono::steady_clock::now();
        double deltaTime = 0.0;
        if (lastTickValid_) {
            const std::chrono::duration<double> elapsed = now - lastTickTime_;
            deltaTime = juce::jlimit(0.0, 0.25, elapsed.count());
        } else {
            deltaTime = timer_ ? timer_->getTimerInterval() / 1000.0 : 0.0;
            lastTickValid_ = true;
        }
        lastTickTime_ = now;

        updateAllMods(deltaTime);

        // Overlay TE LFO phase + value onto MAGDA ModInfo so the visual
        // marker tracks the audio LFO exactly (no free-run drift).
        if (postUpdateHook_)
            postUpdateHook_();
    }

    void updateAllMods(double deltaTime);

    // Timer instance - using composition instead of inheritance to allow early destruction
    std::unique_ptr<UpdateTimer> timer_;

    std::chrono::steady_clock::time_point lastTickTime_{};
    bool lastTickValid_ = false;

    std::function<void()> postUpdateHook_;
};

}  // namespace magda
