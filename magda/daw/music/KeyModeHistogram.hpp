#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <optional>

#include "ChordTypes.hpp"

namespace magda::music {

class KeyModeHistogram {
  public:
    KeyModeHistogram() = default;

    void reset() {
        histogram.fill(0.0);
        lastUpdateSeconds = 0.0;
    }

    void setTauSeconds(double tau) {
        tauSeconds = (tau > 0.1 ? tau : 0.1);
    }

    void updateWithMidiNote(int midiNote, double nowSeconds) {
        decay(nowSeconds);
        if (midiNote >= 0) {
            const int pc = midiNote % 12;
            histogram[static_cast<std::size_t>(pc)] += 1.0;
        }
        lastUpdateSeconds = nowSeconds;
    }

    void updateWithChord(const Chord& chord, double nowSeconds) {
        decay(nowSeconds);
        bool seen[12]{};
        for (const auto& n : chord.notes) {
            const int pc = (n.noteNumber >= 0) ? (n.noteNumber % 12) : -1;
            if (pc >= 0 && !seen[pc]) {
                seen[pc] = true;
                histogram[static_cast<std::size_t>(pc)] += 1.0;
            }
        }
        lastUpdateSeconds = nowSeconds;
    }

    std::optional<std::pair<juce::String, juce::String>> inferKeyMode() const {
        const double total = sum();
        if (total <= 3.0)
            return std::nullopt;
        std::array<double, 12> norm{};
        for (std::size_t i = 0; i < 12; ++i)
            norm[i] = histogram[i] / total;

        // clang-format off
        static const double KRUMH_MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
        static const double KRUMH_MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
        // clang-format on

        auto dot = [](const double* a, const std::array<double, 12>& b, int rot) {
            double s = 0.0;
            for (int i = 0; i < 12; ++i) {
                const int idx = (i - rot) & 11;
                s += a[i] * b[static_cast<std::size_t>(idx)];
            }
            return s;
        };

        double bestScore = -1e18;
        int bestTonic = 0;
        juce::String bestMode = "major";
        for (int tonic = 0; tonic < 12; ++tonic) {
            double sMaj = dot(KRUMH_MAJ, norm, tonic);
            double sMin = dot(KRUMH_MIN, norm, tonic);

            const int pcMajThird = (tonic + 4) % 12;
            const int pcMinThird = (tonic + 3) % 12;
            const double thirdDelta = norm[static_cast<std::size_t>(pcMinThird)] -
                                      norm[static_cast<std::size_t>(pcMajThird)];
            const double kBias = 0.25;
            if (thirdDelta > 0.0)
                sMin += kBias * thirdDelta;
            else if (thirdDelta < 0.0)
                sMaj += kBias * (-thirdDelta);

            if (sMaj > bestScore) {
                bestScore = sMaj;
                bestTonic = tonic;
                bestMode = "major";
            }
            if (sMin > bestScore) {
                bestScore = sMin;
                bestTonic = tonic;
                bestMode = "minor";
            }
        }

        static const char* N[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
        return std::make_pair(juce::String(N[bestTonic]), bestMode);
    }

  private:
    void decay(double nowSeconds) {
        if (lastUpdateSeconds <= 0.0)
            return;
        const double dt = nowSeconds - lastUpdateSeconds;
        if (dt <= 0.0)
            return;
        const double decayFactor = std::exp(-dt / tauSeconds);
        for (double& h : histogram)
            h *= decayFactor;
    }

    double sum() const {
        double s = 0.0;
        for (double h : histogram)
            s += h;
        return s;
    }

    std::array<double, 12> histogram{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    double lastUpdateSeconds{0.0};
    double tauSeconds{5.0};
};

}  // namespace magda::music
