#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Pure, engine-free beats-domain ripple math for the tempo / time-sig / pitch
// sequences. Kept free of Tracktion Engine so it can be unit-tested in the fast
// CLI (Catch2) target; TempoSequenceRippleCommand applies the result to a real
// te::Edit (which the JUCE test target covers).
namespace magda::temporipple {

enum class Mode {
    Insert,     // open a gap [start,end): shift events >= start right by the length
    Delete,     // close [start,end): drop events inside, shift events >= end left
    Duplicate,  // open a gap at end, then copy events in [start,end) into it
};

constexpr double kBeatEps = 1.0e-6;

// Ripple one sequence's events. `T` is any struct with a public `double beat`
// member (plus whatever payload it carries). Index 0 is the beat-0 anchor that
// TE always keeps; it never moves and is never dropped or duplicated.
template <typename T>
std::vector<T> rippleEvents(const std::vector<T>& in, Mode mode, double startBeat, double endBeat) {
    const double dur = endBeat - startBeat;
    std::vector<T> out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        T e = in[i];
        const bool anchor = (i == 0);

        switch (mode) {
            case Mode::Insert:
                if (!anchor && e.beat >= startBeat - kBeatEps)
                    e.beat += dur;
                out.push_back(e);
                break;

            case Mode::Delete:
                if (anchor) {
                    out.push_back(e);
                    break;
                }
                if (e.beat >= startBeat - kBeatEps && e.beat < endBeat - kBeatEps)
                    break;  // inside the deleted range -> drop
                if (e.beat >= endBeat - kBeatEps)
                    e.beat -= dur;
                out.push_back(e);
                break;

            case Mode::Duplicate:
                // Open a gap at endBeat (everything after it slides right).
                if (!anchor && e.beat >= endBeat - kBeatEps)
                    e.beat += dur;
                out.push_back(e);
                break;
        }
    }

    // Duplicate: copy the events inside [start,end) into the freshly opened gap.
    if (mode == Mode::Duplicate) {
        for (std::size_t i = 1; i < in.size(); ++i) {  // never duplicate the anchor
            const T& e = in[i];
            if (e.beat >= startBeat - kBeatEps && e.beat < endBeat - kBeatEps) {
                T copy = e;
                copy.beat = e.beat + dur;
                out.push_back(copy);
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const T& a, const T& b) { return a.beat < b.beat; });
    return out;
}

// Compare two event lists. Ripple never changes an event's payload, only its
// beat position and set membership, so size + beats settle equality.
template <typename T> bool sameBeats(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i].beat - b[i].beat) > kBeatEps)
            return false;
    return true;
}

}  // namespace magda::temporipple
