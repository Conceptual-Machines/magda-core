#pragma once

#include <juce_events/juce_events.h>

#include <memory>

#include "ModInfo.hpp"

namespace magda {

// Constants for ModulatorEngine
namespace ModulatorConstants {
    // Timer update rate
    constexpr int DEFAULT_UPDATE_FPS = 60;
    constexpr double SECONDS_PER_MINUTE = 60.0;
    
    // Musical note beat multipliers (relative to quarter note)
    constexpr double WHOLE_NOTE_BEATS = 4.0;
    constexpr double HALF_NOTE_BEATS = 2.0;
    constexpr double QUARTER_NOTE_BEATS = 1.0;
    constexpr double EIGHTH_NOTE_BEATS = 0.5;
    constexpr double SIXTEENTH_NOTE_BEATS = 0.25;
    constexpr double THIRTY_SECOND_NOTE_BEATS = 0.125;
    
    constexpr double DOTTED_HALF_BEATS = 3.0;
    constexpr double DOTTED_QUARTER_BEATS = 1.5;
    constexpr double DOTTED_EIGHTH_BEATS = 0.75;
    
    constexpr double TRIPLET_HALF_BEATS = 4.0 / 3.0;
    constexpr double TRIPLET_QUARTER_BEATS = 2.0 / 3.0;
    constexpr double TRIPLET_EIGHTH_BEATS = 1.0 / 3.0;
    
    // Waveform generation constants
    constexpr float PI = 3.14159265359f;
    constexpr float TWO_PI = 2.0f * PI;
    constexpr float HALF_CYCLE = 0.5f;
    constexpr float FULL_RANGE = 1.0f;
    constexpr float HALF_RANGE = 0.5f;
    
    // Interpolation constants
    constexpr float TENSION_THRESHOLD = 0.001f;
    constexpr float PHASE_EPSILON = 0.0001f;
    constexpr float TENSION_SCALE = 2.0f;
    
    // Curve preset constants
    constexpr float SMOOTHSTEP_SCALE = 3.0f;
    constexpr float EXPONENTIAL_SCALE = 3.0f;
    
    // Default values
    constexpr float DEFAULT_VALUE = 0.5f;
}  // namespace ModulatorConstants

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
     * @brief Get beat duration for a sync division
     * @param division The sync division (musical note value)
     * @return Number of beats for the division
     */
    static double getBeatMultiplier(SyncDivision division) {
        using namespace ModulatorConstants;
        
        switch (division) {
            case SyncDivision::Whole:
                return WHOLE_NOTE_BEATS;
            case SyncDivision::Half:
                return HALF_NOTE_BEATS;
            case SyncDivision::Quarter:
                return QUARTER_NOTE_BEATS;
            case SyncDivision::Eighth:
                return EIGHTH_NOTE_BEATS;
            case SyncDivision::Sixteenth:
                return SIXTEENTH_NOTE_BEATS;
            case SyncDivision::ThirtySecond:
                return THIRTY_SECOND_NOTE_BEATS;
            case SyncDivision::DottedHalf:
                return DOTTED_HALF_BEATS;
            case SyncDivision::DottedQuarter:
                return DOTTED_QUARTER_BEATS;
            case SyncDivision::DottedEighth:
                return DOTTED_EIGHTH_BEATS;
            case SyncDivision::TripletHalf:
                return TRIPLET_HALF_BEATS;
            case SyncDivision::TripletQuarter:
                return TRIPLET_QUARTER_BEATS;
            case SyncDivision::TripletEighth:
                return TRIPLET_EIGHTH_BEATS;
            default:
                return QUARTER_NOTE_BEATS;
        }
    }

    /**
     * @brief Calculate LFO rate in Hz from tempo sync division
     * @param division The sync division (musical note value)
     * @param bpm Current tempo in beats per minute
     * @return Rate in Hz (cycles per second)
     */
    static float calculateSyncRateHz(SyncDivision division, double bpm) {
        using namespace ModulatorConstants;
        
        // Convert BPM to beats per second (quarter note = 1 beat)
        double beatsPerSecond = bpm / SECONDS_PER_MINUTE;
        
        // Get beat duration for the division and calculate frequency
        double beatMultiplier = getBeatMultiplier(division);
        return static_cast<float>(beatsPerSecond / beatMultiplier);
    }

    /**
     * @brief Generate sine waveform value
     */
    static float generateSineWave(float phase) {
        using namespace ModulatorConstants;
        return (std::sin(TWO_PI * phase) + FULL_RANGE) * HALF_RANGE;
    }

    /**
     * @brief Generate triangle waveform value
     */
    static float generateTriangleWave(float phase) {
        using namespace ModulatorConstants;
        return (phase < HALF_CYCLE) ? phase * 2.0f : 2.0f - phase * 2.0f;
    }

    /**
     * @brief Generate square waveform value
     */
    static float generateSquareWave(float phase) {
        using namespace ModulatorConstants;
        return phase < HALF_CYCLE ? FULL_RANGE : 0.0f;
    }

    /**
     * @brief Generate saw waveform value
     */
    static float generateSawWave(float phase) {
        return phase;
    }

    /**
     * @brief Generate reverse saw waveform value
     */
    static float generateReverseSawWave(float phase) {
        using namespace ModulatorConstants;
        return FULL_RANGE - phase;
    }

    /**
     * @brief Generate waveform value for given phase
     * @param waveform The waveform type
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float generateWaveform(LFOWaveform waveform, float phase) {
        using namespace ModulatorConstants;
        
        switch (waveform) {
            case LFOWaveform::Sine:
                return generateSineWave(phase);
            case LFOWaveform::Triangle:
                return generateTriangleWave(phase);
            case LFOWaveform::Square:
                return generateSquareWave(phase);
            case LFOWaveform::Saw:
                return generateSawWave(phase);
            case LFOWaveform::ReverseSaw:
                return generateReverseSawWave(phase);
            case LFOWaveform::Custom:
                // For Custom, default to triangle - use generateCurvePreset for full support
                return generateTriangleWave(phase);
            default:
                return DEFAULT_VALUE;
        }
    }

    /**
     * @brief Generate S-curve using smoothstep
     */
    static float generateSCurve(float phase) {
        using namespace ModulatorConstants;
        return phase * phase * (SMOOTHSTEP_SCALE - TENSION_SCALE * phase);
    }

    /**
     * @brief Generate exponential curve
     */
    static float generateExponentialCurve(float phase) {
        using namespace ModulatorConstants;
        return (std::exp(phase * EXPONENTIAL_SCALE) - FULL_RANGE) / 
               (std::exp(EXPONENTIAL_SCALE) - FULL_RANGE);
    }

    /**
     * @brief Generate logarithmic curve
     */
    static float generateLogarithmicCurve(float phase) {
        using namespace ModulatorConstants;
        return std::log(FULL_RANGE + phase * (std::exp(FULL_RANGE) - FULL_RANGE));
    }

    /**
     * @brief Generate curve preset value for given phase
     * @param preset The curve preset type
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float generateCurvePreset(CurvePreset preset, float phase) {
        switch (preset) {
            case CurvePreset::Triangle:
                return generateTriangleWave(phase);
            case CurvePreset::Sine:
                return generateSineWave(phase);
            case CurvePreset::RampUp:
                return generateSawWave(phase);
            case CurvePreset::RampDown:
                return generateReverseSawWave(phase);
            case CurvePreset::SCurve:
                return generateSCurve(phase);
            case CurvePreset::Exponential:
                return generateExponentialCurve(phase);
            case CurvePreset::Logarithmic:
                return generateLogarithmicCurve(phase);
            case CurvePreset::Custom:
            default:
                // Custom uses curve points - default to linear ramp
                return phase;
        }
    }

    /**
     * @brief Point pair for curve interpolation
     */
    struct CurvePointPair {
        const CurvePointData* p1;
        const CurvePointData* p2;
    };

    /**
     * @brief Find bracketing points for a given phase
     * @param points The curve points sorted by phase
     * @param phase Current phase (0.0 to 1.0)
     * @return Pair of points that bracket the phase
     */
    static CurvePointPair findBracketingPoints(const std::vector<CurvePointData>& points, 
                                                float phase) {
        CurvePointPair result{nullptr, nullptr};
        
        for (size_t i = 0; i < points.size(); ++i) {
            if (points[i].phase > phase) {
                if (i == 0) {
                    // Before first point - wrap from last point
                    result.p1 = &points.back();
                    result.p2 = &points[0];
                } else {
                    result.p1 = &points[i - 1];
                    result.p2 = &points[i];
                }
                return result;
            }
        }
        
        // After the last point - wrap to first
        result.p1 = &points.back();
        result.p2 = &points.front();
        return result;
    }

    /**
     * @brief Calculate normalized interpolation parameter
     * @param p1 First curve point
     * @param p2 Second curve point
     * @param phase Current phase (0.0 to 1.0)
     * @return Normalized interpolation parameter (0.0 to 1.0)
     */
    static float calculateInterpolationT(const CurvePointData* p1, 
                                          const CurvePointData* p2, 
                                          float phase) {
        using namespace ModulatorConstants;
        
        float phaseSpan;
        float localPhase;
        
        if (p2->phase < p1->phase) {
            // Wrapping case (curve loops)
            phaseSpan = (FULL_RANGE - p1->phase) + p2->phase;
            if (phase >= p1->phase) {
                localPhase = phase - p1->phase;
            } else {
                localPhase = (FULL_RANGE - p1->phase) + phase;
            }
        } else {
            // Normal case
            phaseSpan = p2->phase - p1->phase;
            localPhase = phase - p1->phase;
        }
        
        float t = (phaseSpan > PHASE_EPSILON) ? (localPhase / phaseSpan) : 0.0f;
        return std::clamp(t, 0.0f, FULL_RANGE);
    }

    /**
     * @brief Apply tension-based curve to interpolation parameter
     * @param t Normalized interpolation parameter (0.0 to 1.0)
     * @param tension Curve tension (-3.0 to +3.0)
     * @return Curved interpolation parameter
     */
    static float applyTensionCurve(float t, float tension) {
        using namespace ModulatorConstants;
        
        if (std::abs(tension) < TENSION_THRESHOLD) {
            return t;  // Linear (no tension)
        }
        
        if (tension > 0) {
            // Ease in - slow start, fast end
            return std::pow(t, FULL_RANGE + tension * TENSION_SCALE);
        } else {
            // Ease out - fast start, slow end
            return FULL_RANGE - std::pow(FULL_RANGE - t, FULL_RANGE - tension * TENSION_SCALE);
        }
    }

    /**
     * @brief Evaluate curve points at given phase using tension-based interpolation
     * @param points The curve points sorted by phase
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
    static float evaluateCurvePoints(const std::vector<CurvePointData>& points, float phase) {
        using namespace ModulatorConstants;
        
        if (points.empty()) {
            return DEFAULT_VALUE;
        }
        if (points.size() == 1) {
            return points[0].value;
        }

        // Find bracketing points
        CurvePointPair bracket = findBracketingPoints(points, phase);
        
        // Calculate interpolation parameter
        float t = calculateInterpolationT(bracket.p1, bracket.p2, phase);
        
        // Apply tension curve
        float curvedT = applyTensionCurve(t, bracket.p1->tension);
        
        // Interpolate between point values
        return bracket.p1->value + curvedT * (bracket.p2->value - bracket.p1->value);
    }

    /**
     * @brief Generate waveform value for a mod (handles Custom waveforms with curve points)
     * @param mod The modulator info
     * @param phase Current phase (0.0 to 1.0)
     * @return Output value (0.0 to 1.0)
     */
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
        using namespace ModulatorConstants;
        
        // Calculate delta time in seconds from milliseconds
        constexpr double MS_TO_SECONDS = 0.001;
        double deltaTime = timer_ ? timer_->getTimerInterval() * MS_TO_SECONDS : 0.0;

        // Update all mods through TrackManager
        updateAllMods(deltaTime);
    }

    void updateAllMods(double deltaTime);

    // Timer instance - using composition instead of inheritance to allow early destruction
    std::unique_ptr<UpdateTimer> timer_;
};

}  // namespace magda
