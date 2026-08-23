#include "ModCurve.hpp"

#include <algorithm>
#include <cmath>

#include "CurveMath.hpp"

namespace magda::modcurve {

namespace {

constexpr float kPi = 3.14159265359f;

/// The curve types a drawn point can open its run with. Stored as an int on
/// CurvePointData because the editor writes it from a menu index.
constexpr int kStepCurveType = 2;
constexpr int kHardCornerCurveType = 3;

/// A handle this small is a handle nobody dragged. Below it the run is shaped
/// by its tension scalar alone, which is what a point the user has only bent
/// carries.
constexpr float kHandleEpsilon = 0.000001f;

bool hasDraggedHandle(const CurvePointData& from, const CurvePointData& to) {
    return to.phase > from.phase &&
           (std::abs(from.outHandleX) > kHandleEpsilon ||
            std::abs(from.outHandleY) > kHandleEpsilon || std::abs(to.inHandleX) > kHandleEpsilon ||
            std::abs(to.inHandleY) > kHandleEpsilon);
}

}  // namespace

float waveform(LFOWaveform wave, float phase) {
    switch (wave) {
        case LFOWaveform::Sine:
            return (std::sin(2.0f * kPi * phase) + 1.0f) * 0.5f;
        case LFOWaveform::Triangle:
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        case LFOWaveform::Square:
            return phase < 0.5f ? 1.0f : 0.0f;
        case LFOWaveform::Saw:
            return phase;
        case LFOWaveform::ReverseSaw:
            return 1.0f - phase;
        case LFOWaveform::Custom:
            // Nothing drawn and no preset to fall back on from here. Triangle
            // is what the curve editor opens on, so it is what a Custom
            // waveform asked about on its own answers with.
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
    }
    return 0.5f;
}

float preset(CurvePreset shape, float phase) {
    switch (shape) {
        case CurvePreset::Triangle:
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        case CurvePreset::Sine:
            return (std::sin(2.0f * kPi * phase) + 1.0f) * 0.5f;
        case CurvePreset::RampUp:
            return phase;
        case CurvePreset::RampDown:
            return 1.0f - phase;
        case CurvePreset::SCurve:
            return phase * phase * (3.0f - 2.0f * phase);
        case CurvePreset::Exponential:
            return (std::exp(phase * 3.0f) - 1.0f) / (std::exp(3.0f) - 1.0f);
        case CurvePreset::Logarithmic:
            return std::log(1.0f + phase * (std::exp(1.0f) - 1.0f));
        case CurvePreset::Custom:
            break;
    }
    return phase;
}

float points(std::span<const CurvePointData> curve, float phase) {
    if (curve.empty())
        return 0.5f;
    if (curve.size() == 1)
        return curve[0].value;

    // The run @p phase falls in. Past the last point it is the run that closes
    // the cycle, from the last point back round to the first.
    const CurvePointData* from = nullptr;
    const CurvePointData* to = nullptr;

    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (curve[i].phase > phase) {
            from = i == 0 ? &curve.back() : &curve[i - 1];
            to = i == 0 ? &curve.front() : &curve[i];
            break;
        }
    }

    if (from == nullptr) {
        from = &curve.back();
        to = &curve.front();
    }

    // A step holds until the next point, which is what sample and hold is.
    if (from->curveType == kStepCurveType)
        return from->value;

    float span = 0.0f;
    float local = 0.0f;

    if (to->phase < from->phase) {
        // The wrapping run, which is as long as what is left of the cycle plus
        // what has begun of the next one.
        span = (1.0f - from->phase) + to->phase;
        local = phase >= from->phase ? phase - from->phase : (1.0f - from->phase) + phase;
    } else {
        span = to->phase - from->phase;
        local = phase - from->phase;
    }

    const float t = std::clamp(span > 0.0001f ? local / span : 0.0f, 0.0f, 1.0f);
    const float tension = from->tension;

    const auto applyTension = [tension](float input) {
        if (std::abs(tension) < 0.001f)
            return input;
        if (tension > 0.0f)
            return std::pow(input, 1.0f + tension * 2.0f);
        return 1.0f - std::pow(1.0f - input, 1.0f - tension * 2.0f);
    };

    if (from->curveType == kHardCornerCurveType) {
        // Two straight runs meeting at a corner, and where the corner is
        // depends on whether the user has dragged the handle that sets it.
        float cornerT = 0.5f;
        float cornerValue = from->value + applyTension(0.5f) * (to->value - from->value);

        if (hasDraggedHandle(*from, *to)) {
            const float cornerPhase =
                std::clamp(from->phase + from->outHandleX, from->phase, to->phase);
            const float raw = (to->phase - from->phase > 0.0001f)
                                  ? ((cornerPhase - from->phase) / (to->phase - from->phase))
                                  : 0.5f;
            cornerT = std::clamp(raw, 0.001f, 0.999f);
            cornerValue = std::clamp(from->value + from->outHandleY, 0.0f, 1.0f);
        }

        if (t <= cornerT)
            return from->value + (t / cornerT) * (cornerValue - from->value);

        return cornerValue + ((t - cornerT) / (1.0f - cornerT)) * (to->value - cornerValue);
    }

    // A bend, through the shared warp, so what is heard is what is drawn.
    return magda::curvemath::evalSegment(from->value, to->value, from->value + from->outHandleY,
                                         tension, hasDraggedHandle(*from, *to), t);
}

float shapeAt(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve,
              float phase) {
    if (wave != LFOWaveform::Custom)
        return waveform(wave, phase);

    return curve.empty() ? preset(shape, phase) : points(curve, phase);
}

float endValue(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve) {
    if (wave != LFOWaveform::Custom)
        return waveform(wave, 1.0f);

    return curve.empty() ? preset(shape, 1.0f) : curve.back().value;
}

}  // namespace magda::modcurve
