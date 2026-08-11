#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
 * @file NullDiffMaterial.hpp
 * @brief The audio a null-diff case plays, generated rather than checked in.
 *
 * Two reasons it is generated. A corpus that carries binary fixtures grows a
 * megabyte per case and a review that cannot read them, and a fixture recorded
 * once is a fixture nobody can regenerate when a case needs one bar more.
 *
 * The choice of material per case is not decoration, it is what makes a
 * residual mean something (#2040). Two correct implementations of the same
 * thing disagree wherever they interpolate, and between these two engines
 * stand three interpolators: a four-point cubic Lagrange here against JUCE's
 * five-point in the fork, and two phase vocoders whose output depends on how
 * their input was framed. Broadband material through any of those produces a
 * residual tens of decibels above anything a placement bug makes, and the only
 * way to pass is a tolerance wide enough to hide the placement bug too.
 *
 * So:
 *
 * - Where the engines must agree sample for sample, which is placement, trims,
 *   fades, loop tiling, reverse and comping, the material is impulses and
 *   steps. Nothing interpolates, one sample of disagreement is a residual at
 *   full scale, and a fade curve wrong in the fourth decimal shows up.
 * - Where an interpolator or a stretcher stands between them, the material is
 *   a tone well below Nyquist. Interpolation error falls with the fourth power
 *   of frequency over sample rate, so at a few hundred hertz both curves are
 *   the same curve to far below the floor, while a wrong position, a wrong
 *   ratio or a dropped sample is as loud as it ever was.
 * - Noise exists for one case, which measures how far apart the two stretchers
 *   are and asserts nothing.
 *
 * Written as 32-bit float, for the same reason the incumbent leg renders as
 * float: a 16-bit fixture puts quantisation noise at -96 dBFS, which is well
 * above the floor, and every case would be measuring the file format.
 */

namespace magda::nulldiff {

/// What a case plays. See the file comment for why this is a choice per case
/// rather than a tolerance per case.
enum class MaterialKind {
    /// One full-scale sample every interval, silence between. Placement to the
    /// sample, and nothing to interpolate.
    Impulses,

    /// A constant level whose sign flips every interval. A gain envelope
    /// applied to it is readable directly off the render, which is what the
    /// fade cases need, and the flips keep it from being pure DC.
    Steps,

    /// A sine well below Nyquist, with a short raised-cosine window at each end
    /// so the file's own edges are not a broadband click. For every path with
    /// an interpolator or a stretcher in it.
    Tone,

    /// White noise from a fixed generator. One case, which measures rather than
    /// asserts.
    Noise,
};

struct MaterialSpec {
    MaterialKind kind = MaterialKind::Impulses;
    double sampleRate = 44100.0;
    double durationSeconds = 8.0;
    int channels = 1;

    /// Tone only.
    double frequency = 220.0;

    /// Impulses and Steps: how often one lands.
    double intervalSeconds = 0.25;

    /// Noise only. Fixed, so the same case produces the same bytes on every
    /// machine and a residual is never a different random sequence.
    unsigned int seed = 0x9E3779B9u;

    float level = 0.5f;
};

/**
 * @brief Write @p spec into @p directory as @p name.wav and return the file.
 *
 * Deterministic: same spec, same bytes. Overwrites, because a corpus run that
 * reused a file from a previous run with different contents would be the one
 * kind of failure nobody would think to look for.
 */
juce::File writeMaterial(const juce::File& directory, const juce::String& name,
                         const MaterialSpec& spec);

/// Render @p spec into a buffer without writing it. What writeMaterial writes,
/// and what the material's own tests read.
juce::AudioBuffer<float> renderMaterial(const MaterialSpec& spec);

}  // namespace magda::nulldiff
