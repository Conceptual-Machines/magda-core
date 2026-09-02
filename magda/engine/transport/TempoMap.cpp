#include "transport/TempoMap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "core/CurveMath.hpp"
#include "core/TempoUtils.hpp"

namespace magda::engine {
namespace {

/// Beats are compared, never accumulated, so this only has to absorb the error
/// in one multiply. A tick computed a billionth of a beat early is on the beat.
constexpr double kBeatEpsilon = 1.0e-9;

/// How finely a tempo ramp is cut into constant-tempo sections. Four to the
/// beat, capped, which is what the incumbent uses: at 120 bpm a section is
/// 125 ms of a curve that is already smooth, and the cap keeps a ramp across a
/// hundred bars from baking a hundred thousand sections nobody can hear.
constexpr double kRampSectionsPerBeat = 4.0;
constexpr double kMaxRampSections = 100.0;

/// The last section has no end. It is given a length rather than an infinity so
/// that its start time stays a finite number; nothing reads past it, because
/// every lookup past the last section start resolves to it.
constexpr double kTailBeats = 1.0e6;

/// Where a ramp has got to, at a beat between two changes.
///
/// Evaluated through the model's own curve maths, so a tempo ramp is the curve
/// the tempo lane draws rather than a second opinion about it.
double rampedBpm(double beat, const TempoChange& from, const TempoChange& to) {
    const auto span = to.startBeat - from.startBeat;
    if (span <= 0.0)
        return to.bpm;

    const auto position = std::clamp((beat - from.startBeat) / span, 0.0, 1.0);
    return static_cast<double>(
        curvemath::evalSegment(static_cast<float>(from.bpm), static_cast<float>(to.bpm), 0.0f,
                               from.tension, false, static_cast<float>(position)));
}

/// Bar length in beats. A beat is a quarter note, so a bar of x/y is x quarter
/// notes scaled by what the denominator says a beat of the signature is worth.
double barBeatsOf(int numerator, int denominator) {
    return 4.0 * numerator / denominator;
}

/// The note value the metronome ticks on, in beats.
double tickBeatsOf(int denominator) {
    return 4.0 / denominator;
}

/// One constant-tempo step of the tempo track, before signatures are merged in.
struct GridTempo {
    double startBeat = 0.0;
    double bpm = 120.0;
};

/// One signature region, and the bar it opens on.
struct GridSignature {
    double startBeat = 0.0;
    int numerator = 4;
    int denominator = 4;
    int startBar = 0;
};

std::uint64_t mixInto(std::uint64_t hash, std::uint64_t value) {
    // FNV-1a, as everywhere else in the engine that fingerprints a structure.
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xff;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t mixDouble(std::uint64_t hash, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return mixInto(hash, bits);
}

}  // namespace

TempoMap::TempoMap() : TempoMap({}, {}) {}

TempoMap::TempoMap(std::vector<TempoChange> tempos, std::vector<TimeSignatureChange> signatures) {
    if (tempos.empty())
        tempos.push_back({});
    if (signatures.empty())
        signatures.push_back({});

    // Negative positions are clamped rather than refused: the timeline runs
    // before beat zero (count-in does), but nothing there is editable, so a
    // change that landed there is a change at the beginning.
    for (auto& tempo : tempos) {
        tempo.startBeat = std::max(0.0, tempo.startBeat);
        tempo.bpm = clampBpm(tempo.bpm);
        tempo.tension = std::clamp(tempo.tension, -1.0f, 1.0f);
    }

    for (auto& signature : signatures) {
        signature.startBeat = std::max(0.0, signature.startBeat);
        signature.numerator = clampTimeSignatureValue(signature.numerator);
        signature.denominator = clampTimeSignatureValue(signature.denominator);
    }

    // Stable, so two changes at the same beat keep the order they arrived in:
    // that pair is how a step is written, and which of them wins is the whole
    // meaning of it.
    std::stable_sort(tempos.begin(), tempos.end(),
                     [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    std::stable_sort(signatures.begin(), signatures.end(),
                     [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });

    // What precedes the first change is that change's own value, held. For the
    // tempo that means an extra change at the beginning rather than moving the
    // first one: moving it would drag the start of its ramp back with it.
    if (tempos.front().startBeat > 0.0)
        tempos.insert(tempos.begin(), TempoChange{0.0, tempos.front().bpm, 0.0f});
    signatures.front().startBeat = 0.0;

    fingerprint_ = 14695981039346656037ULL;
    fingerprint_ = mixInto(fingerprint_, tempos.size());
    for (const auto& tempo : tempos) {
        fingerprint_ = mixDouble(fingerprint_, tempo.startBeat);
        fingerprint_ = mixDouble(fingerprint_, tempo.bpm);
        fingerprint_ = mixDouble(fingerprint_, tempo.tension);
    }

    fingerprint_ = mixInto(fingerprint_, signatures.size());
    for (const auto& signature : signatures) {
        fingerprint_ = mixDouble(fingerprint_, signature.startBeat);
        fingerprint_ = mixInto(fingerprint_, static_cast<std::uint64_t>(signature.numerator));
        fingerprint_ = mixInto(fingerprint_, static_cast<std::uint64_t>(signature.denominator));
    }

    // The tempo grid: where the tempo takes a new constant value, from the
    // tempo track alone.
    //
    // Built before anything knows about signatures, and that is the whole point
    // of building it separately. A section's tempo has to come from here rather
    // than from re-evaluating the ramp at the section's own start, because a
    // signature change cuts sections in two and re-evaluating would give the
    // second half a different tempo from the one it would have had. Beats would
    // then move in time when a signature changed, and a signature only groups
    // beats: it never says when one happens.
    std::vector<GridTempo> grid;
    grid.reserve(tempos.size());

    for (std::size_t i = 0; i < tempos.size(); ++i) {
        // The last change written at a beat is the one in force from it.
        // Everything before it was only ever the target of the ramp arriving
        // there, which is how a step is spelled: ramp to the first, jump to the
        // last.
        if (i + 1 < tempos.size() && tempos[i + 1].startBeat <= tempos[i].startBeat)
            continue;

        const auto& current = tempos[i];
        const auto* rampTarget = i + 1 < tempos.size() ? &tempos[i + 1] : nullptr;
        const auto ramping =
            rampTarget != nullptr && std::abs(rampTarget->bpm - current.bpm) > 1.0e-9;
        const auto spanEnd =
            rampTarget != nullptr ? rampTarget->startBeat : current.startBeat + kTailBeats;

        auto subdivisions = 1;
        if (ramping)
            subdivisions = static_cast<int>(std::clamp(
                kRampSectionsPerBeat * (spanEnd - current.startBeat), 1.0, kMaxRampSections));

        const auto step = (spanEnd - current.startBeat) / subdivisions;

        for (auto k = 0; k < subdivisions; ++k) {
            // Computed from the change beat rather than accumulated, so a long
            // ramp's steps still start exactly where the next change does.
            const auto startBeat = current.startBeat + step * k;
            grid.push_back(
                {startBeat, ramping ? rampedBpm(startBeat, current, *rampTarget) : current.bpm});
        }
    }

    // The signature regions, and which bar each one opens on.
    std::vector<GridSignature> regions;
    regions.reserve(signatures.size());

    for (const auto& signature : signatures) {
        if (!regions.empty() && signature.startBeat <= regions.back().startBeat) {
            // Coincident: the last one written wins, and the bar it opens is
            // the one already counted.
            regions.back().numerator = signature.numerator;
            regions.back().denominator = signature.denominator;
            continue;
        }

        if (regions.empty()) {
            regions.push_back({signature.startBeat, signature.numerator, signature.denominator, 0});
            continue;
        }

        // A signature change starts a bar, whether or not the bar it interrupts
        // was finished.
        const auto& previous = regions.back();
        const auto bars = std::ceil((signature.startBeat - previous.startBeat) /
                                        barBeatsOf(previous.numerator, previous.denominator) -
                                    kBeatEpsilon);
        regions.push_back({signature.startBeat, signature.numerator, signature.denominator,
                           previous.startBar + static_cast<int>(bars)});
    }

    // A section begins wherever either grid does. Merging is all a signature
    // does to the tempo: it splits sections, and both halves keep the tempo the
    // one they came from had.
    std::vector<double> boundaries;
    boundaries.reserve(grid.size() + regions.size());
    for (const auto& tempo : grid)
        boundaries.push_back(tempo.startBeat);
    for (const auto& region : regions)
        boundaries.push_back(region.startBeat);

    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    sections_.reserve(boundaries.size());

    double time = 0.0;
    std::size_t gridIndex = 0, regionIndex = 0;

    for (std::size_t i = 0; i < boundaries.size(); ++i) {
        const auto startBeat = boundaries[i];

        while (gridIndex + 1 < grid.size() && grid[gridIndex + 1].startBeat <= startBeat)
            ++gridIndex;
        while (regionIndex + 1 < regions.size() && regions[regionIndex + 1].startBeat <= startBeat)
            ++regionIndex;

        const auto isLast = i + 1 == boundaries.size();
        const auto endBeat = isLast ? startBeat + kTailBeats : boundaries[i + 1];

        Section section;
        section.startBeat = startBeat;
        section.startTime = time;
        section.bpm = grid[gridIndex].bpm;
        section.secondsPerBeat = 60.0 / section.bpm;
        section.numerator = regions[regionIndex].numerator;
        section.denominator = regions[regionIndex].denominator;
        section.signatureStartBeat = regions[regionIndex].startBeat;
        section.signatureStartBar = regions[regionIndex].startBar;
        section.signatureEndBeat = regionIndex + 1 < regions.size()
                                       ? regions[regionIndex + 1].startBeat
                                       : std::numeric_limits<double>::infinity();
        sections_.push_back(section);

        time += (endBeat - startBeat) * section.secondsPerBeat;
    }
}

double TempoMap::nextSectionBoundaryAfter(double beat) const {
    const auto next = std::upper_bound(
        sections_.begin(), sections_.end(), beat + kBeatEpsilon,
        [](double value, const Section& section) { return value < section.startBeat; });

    return next == sections_.end() ? std::numeric_limits<double>::infinity() : next->startBeat;
}

const TempoMap::Section& TempoMap::sectionForBeat(double beat) const {
    // Before the first section is the first section extended backwards: the
    // timeline exists before beat zero and has to have a tempo there.
    auto found = std::upper_bound(
        sections_.begin(), sections_.end(), beat,
        [](double value, const Section& section) { return value < section.startBeat; });
    if (found == sections_.begin())
        return sections_.front();
    return *(found - 1);
}

const TempoMap::Section& TempoMap::sectionForTime(double seconds) const {
    auto found = std::upper_bound(
        sections_.begin(), sections_.end(), seconds,
        [](double value, const Section& section) { return value < section.startTime; });
    if (found == sections_.begin())
        return sections_.front();
    return *(found - 1);
}

double TempoMap::beatToTime(double beat) const {
    const auto& section = sectionForBeat(beat);
    return section.startTime + (beat - section.startBeat) * section.secondsPerBeat;
}

double TempoMap::timeToBeat(double seconds) const {
    const auto& section = sectionForTime(seconds);
    return section.startBeat + (seconds - section.startTime) / section.secondsPerBeat;
}

double TempoMap::bpmAt(double beat) const {
    return sectionForBeat(beat).bpm;
}

BarsAndBeats TempoMap::barsAndBeatsAt(double beat) const {
    const auto& section = sectionForBeat(beat);
    const auto barBeats = barBeatsOf(section.numerator, section.denominator);
    const auto sinceSignature = beat - section.signatureStartBeat;
    const auto bars = std::floor(sinceSignature / barBeats);

    return {section.signatureStartBar + static_cast<int>(bars),
            (sinceSignature - bars * barBeats) / tickBeatsOf(section.denominator),
            section.numerator, section.denominator};
}

BeatTick TempoMap::tickAtOrAfter(double beat) const {
    const auto& section = sectionForBeat(beat);
    const auto tickBeats = tickBeatsOf(section.denominator);

    const auto index = std::ceil((beat - section.signatureStartBeat) / tickBeats - kBeatEpsilon);
    auto tickBeat = section.signatureStartBeat + index * tickBeats;

    // A signature change starts a bar, so it cuts the tick grid short wherever
    // it lands.
    if (tickBeat >= section.signatureEndBeat)
        return {section.signatureEndBeat,
                section.signatureEndBeat +
                    tickBeatsOf(sectionForBeat(section.signatureEndBeat).denominator),
                true};

    const auto whole = std::llround(index);
    const auto numerator = static_cast<long long>(section.numerator);
    const auto inBar = ((whole % numerator) + numerator) % numerator;

    return {tickBeat, std::min(tickBeat + tickBeats, section.signatureEndBeat), inBar == 0};
}

}  // namespace magda::engine
