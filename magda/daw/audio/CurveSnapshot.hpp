#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "../core/ModInfo.hpp"

namespace magda {

/**
 * @brief Fixed-size curve data safe for audio-thread reading.
 *
 * Mirrors the variable-length CurvePointData vector from ModInfo but in a
 * fixed-size std::array so there are no heap allocations on the audio thread.
 */
struct CurveSnapshot {
    static constexpr int kMaxPoints = 64;

    struct Point {
        float phase = 0.0f;
        float value = 0.5f;
        float tension = 0.0f;
    };

    std::array<Point, kMaxPoints> points{};
    int count = 0;
    CurvePreset preset = CurvePreset::Triangle;
    bool hasCustomPoints = false;
    bool oneShot = false;

    /**
     * @brief Generate a preset curve value (no custom points).
     *
     * Same logic as ModulatorEngine::generateCurvePreset — pure math, no
     * allocations, safe for audio thread.
     */
    static float evaluatePreset(CurvePreset p, float phase) {
        constexpr float PI = 3.14159265359f;
        switch (p) {
            case CurvePreset::Triangle:
                return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
            case CurvePreset::Sine:
                return (std::sin(2.0f * PI * phase) + 1.0f) * 0.5f;
            case CurvePreset::RampUp:
                return phase;
            case CurvePreset::RampDown:
                return 1.0f - phase;
            case CurvePreset::SCurve: {
                float t = phase;
                return t * t * (3.0f - 2.0f * t);
            }
            case CurvePreset::Exponential:
                return (std::exp(phase * 3.0f) - 1.0f) / (std::exp(3.0f) - 1.0f);
            case CurvePreset::Logarithmic:
                return std::log(1.0f + phase * (std::exp(1.0f) - 1.0f));
            case CurvePreset::Custom:
            default:
                return phase;
        }
    }

    /**
     * @brief Evaluate the curve at a given phase.
     *
     * If custom points exist, uses tension-based interpolation (same as
     * ModulatorEngine::evaluateCurvePoints). Otherwise falls back to
     * generating the preset curve mathematically.
     */
    float evaluate(float phase) const {
        if (count == 0)
            return evaluatePreset(preset, phase);
        if (count == 1)
            return points[0].value;

        const Point* p1 = nullptr;
        const Point* p2 = nullptr;

        for (int i = 0; i < count; ++i) {
            if (points[static_cast<size_t>(i)].phase > phase) {
                if (i == 0) {
                    p1 = &points[static_cast<size_t>(count - 1)];
                    p2 = &points[0];
                } else {
                    p1 = &points[static_cast<size_t>(i - 1)];
                    p2 = &points[static_cast<size_t>(i)];
                }
                break;
            }
        }

        if (!p1) {
            p1 = &points[static_cast<size_t>(count - 1)];
            p2 = &points[0];
        }

        float phaseSpan;
        float localPhase;
        if (p2->phase < p1->phase) {
            phaseSpan = (1.0f - p1->phase) + p2->phase;
            if (phase >= p1->phase)
                localPhase = phase - p1->phase;
            else
                localPhase = (1.0f - p1->phase) + phase;
        } else {
            phaseSpan = p2->phase - p1->phase;
            localPhase = phase - p1->phase;
        }

        float t = (phaseSpan > 0.0001f) ? (localPhase / phaseSpan) : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        float tension = p1->tension;
        if (std::abs(tension) < 0.001f) {
            return p1->value + t * (p2->value - p1->value);
        } else {
            float curvedT;
            if (tension > 0)
                curvedT = std::pow(t, 1.0f + tension * 2.0f);
            else
                curvedT = 1.0f - std::pow(1.0f - t, 1.0f - tension * 2.0f);
            return p1->value + curvedT * (p2->value - p1->value);
        }
    }
};

/**
 * @brief Double-buffered CurveSnapshot holder for lock-free audio-thread reads.
 *
 * Message thread writes to the inactive buffer then atomically swaps.
 * Audio thread reads the active buffer via the static callback.
 */
struct CurveSnapshotHolder {
    CurveSnapshot buffers[2];
    std::atomic<CurveSnapshot*> active{&buffers[0]};

    // One-shot state: audio thread tracks phase to detect cycle completion
    std::atomic<float> previousPhase_{-1.0f};
    std::atomic<bool> oneShotCompleted_{false};

    /**
     * @brief Message thread: copy curve data from ModInfo into the inactive
     *        buffer, then swap active pointer.
     */
    void update(const ModInfo& modInfo) {
        // Determine which buffer is inactive
        CurveSnapshot* current = active.load(std::memory_order_acquire);
        CurveSnapshot* back = (current == &buffers[0]) ? &buffers[1] : &buffers[0];

        // Fill the back buffer
        back->preset = modInfo.curvePreset;
        back->hasCustomPoints = !modInfo.curvePoints.empty();
        back->oneShot = modInfo.oneShot;
        back->count =
            std::min(static_cast<int>(modInfo.curvePoints.size()), CurveSnapshot::kMaxPoints);

        for (int i = 0; i < back->count; ++i) {
            const auto& src = modInfo.curvePoints[static_cast<size_t>(i)];
            auto& dst = back->points[static_cast<size_t>(i)];
            dst.phase = src.phase;
            dst.value = src.value;
            dst.tension = src.tension;
        }

        // Swap: audio thread will now read from the newly written buffer
        active.store(back, std::memory_order_release);

        // If oneShot was turned off, reset completed state
        if (!modInfo.oneShot)
            oneShotCompleted_.store(false, std::memory_order_release);
    }

    /**
     * @brief Reset one-shot state so the LFO plays through one more cycle.
     *
     * Call this alongside LFOModifier::triggerNoteOn() when retriggering.
     * Safe to call from any thread (uses relaxed atomics).
     */
    void resetOneShot() {
        oneShotCompleted_.store(false, std::memory_order_release);
        previousPhase_.store(-1.0f, std::memory_order_release);
    }

    /**
     * @brief Static callback wired to LFOModifier::customWaveFunction.
     *
     * Called on the audio thread once per block. userData points to this holder.
     * Loads the active snapshot and evaluates the curve at the given phase.
     * In one-shot mode, holds the end value after the first complete cycle.
     */
    static float evaluateCallback(float phase, void* userData) {
        auto* holder = static_cast<CurveSnapshotHolder*>(userData);
        const CurveSnapshot* snap = holder->active.load(std::memory_order_acquire);

        if (snap->oneShot) {
            // Use 0.999999f so curve evaluation stays in the last segment
            // rather than wrapping to the first point.
            constexpr float kEndPhase = 0.999999f;

            if (holder->oneShotCompleted_.load(std::memory_order_acquire))
                return snap->evaluate(kEndPhase);

            float prev = holder->previousPhase_.load(std::memory_order_relaxed);
            holder->previousPhase_.store(phase, std::memory_order_relaxed);

            // Detect phase wrap-around: phase jumped back significantly
            if (prev >= 0.0f && phase < prev - 0.5f) {
                holder->oneShotCompleted_.store(true, std::memory_order_release);
                return snap->evaluate(kEndPhase);
            }
        }

        return snap->evaluate(phase);
    }
};

}  // namespace magda
