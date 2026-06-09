#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace magda::daw::audio {

/**
 * @brief Inter-track frequency-masking detector (issue #1390).
 *
 * Pure analysis over per-track band spectra: given each track's energy in a
 * shared set of 1/3-octave bands, it finds pairs of tracks that compete for the
 * same frequency region ("kick and bass masking at 80-120 Hz"). This is the
 * agent-side DSP that feeds the mixing agent (#886); it has no audio-thread or
 * Tracktion dependency, so it is unit-testable in isolation. The per-track band
 * energies are produced upstream by the measurement layer's taps (#1388).
 */

/// 1/3-octave bands from ~20 Hz; band b spans [edge(b), edge(b+1)].
inline constexpr int kNumMaskingBands = 30;

/// Lower edge of band index (0..kNumMaskingBands), in Hz.
inline float maskingBandEdgeHz(int edge) {
    return 20.0f * std::pow(2.0f, static_cast<float>(edge) / 3.0f);  // edge 30 -> ~20480 Hz
}

/// dB energy treated as "no content" in a band.
inline constexpr float kMaskingFloorDb = -60.0f;

struct TrackBandEnergies {
    int trackId = -1;
    juce::String name;
    std::array<float, kNumMaskingBands> bandDb{};  // per-band energy, dBFS-ish
};

struct MaskingFinding {
    int trackA = -1, trackB = -1;
    juce::String nameA, nameB;
    float loHz = 0.0f, hiHz = 0.0f;  // merged offending frequency range
    float severity = 0.0f;           // 0..1, worst band in the range
};

struct MaskingOptions {
    float absFloorDb = kMaskingFloorDb;  // bands below this are ignored
    float relRangeDb = 12.0f;   // a band "matters" for a track within this of its peak band
    float minSeverity = 0.12f;  // cull findings weaker than this
    int maxFindings = 12;       // cap the returned list
};

namespace masking_detail {

// Perceptual weight: masking is most audible / most common in the mids.
inline float bandWeight(int b) {
    const float c = std::sqrt(maskingBandEdgeHz(b) * maskingBandEdgeHz(b + 1));
    return (c >= 150.0f && c <= 6000.0f) ? 1.0f : 0.45f;
}

}  // namespace masking_detail

/**
 * Detect masking between tracks. Returns findings sorted by severity (worst
 * first), at most opts.maxFindings.
 */
inline std::vector<MaskingFinding> detectMasking(const std::vector<TrackBandEnergies>& tracks,
                                                 const MaskingOptions& opts = {}) {
    const int n = static_cast<int>(tracks.size());

    // Per-track relative presence in each band: 1.0 at the track's peak band,
    // ramping to 0 at relRangeDb below it; 0 below the absolute floor.
    std::vector<std::array<float, kNumMaskingBands>> rel(static_cast<size_t>(juce::jmax(0, n)));
    for (int t = 0; t < n; ++t) {
        float peak = opts.absFloorDb;
        for (int b = 0; b < kNumMaskingBands; ++b)
            peak = juce::jmax(peak, tracks[static_cast<size_t>(t)].bandDb[static_cast<size_t>(b)]);
        const float lo = peak - opts.relRangeDb;
        for (int b = 0; b < kNumMaskingBands; ++b) {
            const float db = tracks[static_cast<size_t>(t)].bandDb[static_cast<size_t>(b)];
            float r = 0.0f;
            if (db > opts.absFloorDb && opts.relRangeDb > 0.0f)
                r = juce::jlimit(0.0f, 1.0f, (db - lo) / opts.relRangeDb);
            rel[static_cast<size_t>(t)][static_cast<size_t>(b)] = r;
        }
    }

    // Per-pair, per-band severity = weight * min(relA, relB): both tracks must
    // prominently occupy the band for it to count as mutual masking.
    std::map<std::pair<int, int>, std::array<float, kNumMaskingBands>> pairBands;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            std::array<float, kNumMaskingBands> sev{};
            bool any = false;
            for (int b = 0; b < kNumMaskingBands; ++b) {
                const float s = masking_detail::bandWeight(b) *
                                juce::jmin(rel[static_cast<size_t>(i)][static_cast<size_t>(b)],
                                           rel[static_cast<size_t>(j)][static_cast<size_t>(b)]);
                sev[static_cast<size_t>(b)] = s;
                any = any || s > 0.0f;
            }
            if (any)
                pairBands[{i, j}] = sev;
        }
    }

    // Merge consecutive significant bands per pair into one finding (a range),
    // taking the worst band as the severity.
    std::vector<MaskingFinding> findings;
    for (const auto& [pair, sev] : pairBands) {
        int runStart = -1;
        float runMax = 0.0f;
        auto flush = [&](int runEnd) {
            if (runStart >= 0 && runMax >= opts.minSeverity) {
                MaskingFinding f;
                f.trackA = tracks[static_cast<size_t>(pair.first)].trackId;
                f.trackB = tracks[static_cast<size_t>(pair.second)].trackId;
                f.nameA = tracks[static_cast<size_t>(pair.first)].name;
                f.nameB = tracks[static_cast<size_t>(pair.second)].name;
                f.loHz = maskingBandEdgeHz(runStart);
                f.hiHz = maskingBandEdgeHz(runEnd + 1);
                f.severity = runMax;
                findings.push_back(std::move(f));
            }
            runStart = -1;
            runMax = 0.0f;
        };
        for (int b = 0; b < kNumMaskingBands; ++b) {
            if (sev[static_cast<size_t>(b)] > 0.0f) {
                if (runStart < 0)
                    runStart = b;
                runMax = juce::jmax(runMax, sev[static_cast<size_t>(b)]);
            } else {
                flush(b - 1);
            }
        }
        flush(kNumMaskingBands - 1);
    }

    std::sort(
        findings.begin(), findings.end(),
        [](const MaskingFinding& a, const MaskingFinding& b) { return a.severity > b.severity; });
    if (static_cast<int>(findings.size()) > opts.maxFindings)
        findings.resize(static_cast<size_t>(opts.maxFindings));
    return findings;
}

}  // namespace magda::daw::audio
