#pragma once

#include <juce_events/juce_events.h>

#include <chrono>
#include <functional>
#include <memory>

#include "ModCurve.hpp"
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
     *
     * The model's own reading (core/ModCurve.hpp), so the visual sim, the
     * audio snapshot and the native engine's LFO all draw one shape.
     */
    static float generateWaveform(LFOWaveform waveform, float phase) {
        return modcurve::waveform(waveform, phase);
    }

    /** @brief Generate curve preset value for given phase. */
    static float generateCurvePreset(CurvePreset preset, float phase) {
        return modcurve::preset(preset, phase);
    }

    /** @brief Evaluate drawn curve points at given phase. */
    static float evaluateCurvePoints(const std::vector<CurvePointData>& points, float phase) {
        return modcurve::points(points, phase);
    }

    /**
     * @brief Return the end-of-cycle value for oneshot mode.
     *
     * Returns the last custom point's value directly (avoiding wrap-around
     * interpolation), or the preset value at phase 1.0 for non-custom waveforms.
     */
    static float generateOneShotEndValue(const ModInfo& mod) {
        return modcurve::endValue(mod.waveform, mod.curvePreset, mod.curvePoints);
    }

    /** @brief Waveform value for a mod, handling Custom waveforms with curve points. */
    static float generateWaveformForMod(const ModInfo& mod, float phase) {
        return modcurve::shapeAt(mod.waveform, mod.curvePreset, mod.curvePoints, phase);
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
