#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "../../core/CurveMath.hpp"
#include "../../core/ModInfo.hpp"

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
        int curveType = 0;
        float inHandleX = 0.0f;
        float inHandleY = 0.0f;
        float outHandleX = 0.0f;
        float outHandleY = 0.0f;
    };

    std::array<Point, kMaxPoints> points{};
    int count = 0;
    CurvePreset preset = CurvePreset::Triangle;
    bool hasCustomPoints = false;
    bool oneShot = false;

    // MSEG loop region. When useLoopRegion is set, the intro [0, loopStart)
    // plays once, then [loopStart, loopEnd] repeats indefinitely.
    bool useLoopRegion = false;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;

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
     * @brief Return the value at the very end of the curve (for oneshot hold).
     *
     * Returns the last custom point's value, or the preset evaluated at phase 1.0.
     * Avoids the wrap-around interpolation that evaluate(0.999) would do
     * (which would interpolate from the last point back toward the first).
     */
    float endValue() const {
        if (count > 0)
            return points[static_cast<size_t>(count - 1)].value;
        return evaluatePreset(preset, 1.0f);
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

        // Step: the segment holds p1's value until the next point (sample &
        // hold / rectangular steps).
        constexpr int kStepCurveType = 2;
        if (p1->curveType == kStepCurveType)
            return p1->value;

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
        auto applyTension = [tension](float input) {
            if (std::abs(tension) < 0.001f)
                return input;
            if (tension > 0)
                return std::pow(input, 1.0f + tension * 2.0f);
            return 1.0f - std::pow(1.0f - input, 1.0f - tension * 2.0f);
        };

        auto getShaper = [&]() {
            struct Shaper {
                float t = 0.5f;
                float value = 0.5f;
                bool stored = false;
            };

            constexpr float kHandleEpsilon = 0.000001f;
            const bool hasStoredShaper =
                p2->phase > p1->phase && (std::abs(p1->outHandleX) > kHandleEpsilon ||
                                          std::abs(p1->outHandleY) > kHandleEpsilon ||
                                          std::abs(p2->inHandleX) > kHandleEpsilon ||
                                          std::abs(p2->inHandleY) > kHandleEpsilon);

            if (hasStoredShaper) {
                const float shaperPhase =
                    std::clamp(p1->phase + p1->outHandleX, p1->phase, p2->phase);
                const float shaperT = (p2->phase - p1->phase > 0.0001f)
                                          ? ((shaperPhase - p1->phase) / (p2->phase - p1->phase))
                                          : 0.5f;
                return Shaper{std::clamp(shaperT, 0.001f, 0.999f),
                              std::clamp(p1->value + p1->outHandleY, 0.0f, 1.0f), true};
            }

            constexpr float cornerT = 0.5f;
            return Shaper{cornerT, p1->value + applyTension(cornerT) * (p2->value - p1->value),
                          false};
        };

        constexpr int kHardCornerCurveType = 3;
        if (p1->curveType == kHardCornerCurveType) {
            const auto shaper = getShaper();
            if (t <= shaper.t) {
                const float u = t / shaper.t;
                return p1->value + u * (shaper.value - p1->value);
            }
            const float u = (t - shaper.t) / (1.0f - shaper.t);
            return shaper.value + u * (p2->value - shaper.value);
        }

        // Linear segment: shared bounded power warp so the audio output matches
        // exactly what the curve editor draws (see core/CurveMath.hpp).
        constexpr float kLinearHandleEps = 0.000001f;
        const bool hasStoredShaper =
            p2->phase > p1->phase && (std::abs(p1->outHandleX) > kLinearHandleEps ||
                                      std::abs(p1->outHandleY) > kLinearHandleEps ||
                                      std::abs(p2->inHandleX) > kLinearHandleEps ||
                                      std::abs(p2->inHandleY) > kLinearHandleEps);
        return magda::curvemath::evalSegment(p1->value, p2->value, p1->value + p1->outHandleY,
                                             tension, hasStoredShaper, t);
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

    // One-shot state: cumulative phase tracks how far through the cycle we are.
    // evaluateCallback adds phase deltas each block on the destination audio thread.
    // When cumulative >= 1.0 the curve has played once → hold at endValue.
    //
    // pendingReset_ is the cross-thread signal: resetOneShot() (source audio thread)
    // sets it to true; evaluateCallback (destination audio thread) consumes it and
    // locally zeroes cumulativePhase_/previousPhase_. This avoids the race where a
    // concurrent evaluateCallback reads stale cumulative and overwrites the zero.
    std::atomic<bool> pendingReset_{false};
    std::atomic<float> cumulativePhase_{0.0f};
    std::atomic<float> previousPhase_{-1.0f};
    std::atomic<bool> oneShotCompleted_{false};
    std::atomic<int> evalLogCount_{0};  // throttle DBG spam in evaluateCallback
    // Last remapped (looped) phase, published for the UI phase indicator so the
    // dot follows the loop region instead of TE's raw 0..1 sweep.
    std::atomic<float> lastEffectivePhase_{0.0f};

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
        back->useLoopRegion = modInfo.useLoopRegion;
        back->loopStart = modInfo.loopStart;
        back->loopEnd = modInfo.loopEnd;
        back->count =
            std::min(static_cast<int>(modInfo.curvePoints.size()), CurveSnapshot::kMaxPoints);

        for (int i = 0; i < back->count; ++i) {
            const auto& src = modInfo.curvePoints[static_cast<size_t>(i)];
            auto& dst = back->points[static_cast<size_t>(i)];
            dst.phase = src.phase;
            dst.value = src.value;
            dst.tension = src.tension;
            dst.curveType = src.curveType;
            dst.inHandleX = src.inHandleX;
            dst.inHandleY = src.inHandleY;
            dst.outHandleX = src.outHandleX;
            dst.outHandleY = src.outHandleY;
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
     * Safe to call from any thread. Sets a pending flag that evaluateCallback
     * consumes on the destination audio thread, avoiding cross-thread races
     * on cumulativePhase_/previousPhase_.
     */
    void resetOneShot() {
        oneShotCompleted_.store(false, std::memory_order_release);
        pendingReset_.store(true, std::memory_order_release);
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
        if (!holder)
            return 0.0f;
        const CurveSnapshot* snap = holder->active.load(std::memory_order_acquire);

        const float loopLen = snap->loopEnd - snap->loopStart;
        const bool looping = snap->useLoopRegion && loopLen > 1.0e-4f;

        // Both one-shot completion tracking and MSEG looping integrate the
        // incoming phase into a cumulative position. Looping additionally
        // remaps that position so the [loopStart, loopEnd] region repeats once
        // the intro (0 -> loopStart) has played through.
        if (snap->oneShot || looping) {
            // Consume pending reset from resetOneShot() on this thread,
            // so cumulativePhase_/previousPhase_ are only written by one thread.
            bool wasReset = holder->pendingReset_.exchange(false, std::memory_order_acquire);
            if (wasReset) {
                holder->cumulativePhase_.store(0.0f, std::memory_order_relaxed);
                holder->previousPhase_.store(-1.0f, std::memory_order_relaxed);
                holder->oneShotCompleted_.store(false, std::memory_order_relaxed);
                holder->evalLogCount_.store(0, std::memory_order_relaxed);
            }

            // A one-shot without a sustain loop holds its end value once it has
            // played through. With a loop, the region sustains instead.
            if (snap->oneShot && !looping &&
                holder->oneShotCompleted_.load(std::memory_order_relaxed))
                return snap->endValue();

            // Accumulate phase delta to advance the cumulative position.
            float prev = holder->previousPhase_.load(std::memory_order_relaxed);
            holder->previousPhase_.store(phase, std::memory_order_relaxed);

            float cum = holder->cumulativePhase_.load(std::memory_order_relaxed);
            if (prev >= 0.0f) {
                float delta = phase - prev;
                // Normal forward movement: delta is small positive.
                // Phase wrap (0.99 → 0.01): delta is ~-1.0, correct to ~+0.01.
                if (delta < -0.5f)
                    delta += 1.0f;
                if (delta > 0.0f) {
                    cum += delta;
                    // Keep the cumulative position bounded inside the loop so a
                    // long-running LFO never loses float precision.
                    if (looping && cum >= snap->loopEnd)
                        cum = snap->loopStart + std::fmod(cum - snap->loopStart, loopLen);
                    holder->cumulativePhase_.store(cum, std::memory_order_relaxed);
                    if (snap->oneShot && !looping && cum >= 1.0f) {
                        holder->oneShotCompleted_.store(true, std::memory_order_relaxed);
                        return snap->endValue();
                    }
                }
            }

            if (looping) {
                const float eff = (cum < snap->loopStart)
                                      ? cum  // intro segment, plays once
                                      : snap->loopStart + std::fmod(cum - snap->loopStart, loopLen);
                holder->lastEffectivePhase_.store(eff, std::memory_order_relaxed);
                return snap->evaluate(eff);
            }
        }

        return snap->evaluate(phase);
    }
};

}  // namespace magda
